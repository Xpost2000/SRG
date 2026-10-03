#include "audio.h"
#include "spu.h"
#include "cdfs.h"
#include <psxcd.h>

//
// .VAG header, 48 bytes, as psxavenc writes it. The size and sample rate are
// big-endian (Sony's PC tools were Mac-first); interleave and channel count
// are little-endian additions for the interleaved "VAGi" stream flavour.
// The magic is read as a little-endian word so the constants are reversed.
//
#define AUDIO_VAG_HEADER_SIZE  (48)
#define AUDIO_VAG_MAGIC_SAMPLE (0x70474156) // "VAGp": mono sound effect
#define AUDIO_VAG_MAGIC_STREAM (0x69474156) // "VAGi": interleaved stream

typedef struct Audio_Vag_Header Audio_Vag_Header;

struct Audio_Vag_Header {
  uint32_t magic;
  uint32_t version;
  uint32_t interleave;  // VAGi: bytes per channel per chunk, little-endian
  uint32_t size;        // bytes of ADPCM per channel, big-endian
  uint32_t sample_rate; // Hz, big-endian
  uint16_t reserved[5];
  uint16_t channels;    // VAGi: little-endian, 0 means stereo
  char     name[16];
};

//
// How many sectors must be free before the feeder bothers the drive. One read
// of 24 sectors (48 KB) every half second beats a seek per frame.
//
#define AUDIO_REFILL_SECTORS (24)

#define AUDIO_SFX_VOICE_COUNT (SPU_VOICE_COUNT - SPU_VOICE_SFX_FIRST)

// ----------------------------------------------------------------------------
//  State
// ----------------------------------------------------------------------------

typedef struct Audio_Sample Audio_Sample;
typedef struct Audio_Music  Audio_Music;

struct Audio_Sample {
  uint32_t spu_addr;
  int      sample_rate;
};

enum Audio_Music_State {
  AUDIO_MUSIC_STOPPED,
  AUDIO_MUSIC_PLAYING,  // feeder is reading sectors
  AUDIO_MUSIC_DRAINING, // whole file fed, waiting for the ring to play out
};

struct Audio_Music {
  int          state;            // Audio_Music_State
  int          loop;
  int          start_lba;        // first data sector (the one after the header)
  int          sector_count;     // data sectors in the file
  int          next_sector;      // 0..sector_count, wraps when looping
  volatile int inflight_sectors; // size of the read the drive is working on
  int16_t      vol_l;
  int16_t      vol_r;
  int          fade_total;       // frames; 0 = no fade running
  int          fade_left;
};

static Audio_Sample g_sfx[AUDIO_SFX_MAX];
static int          g_sfx_count;
static uint32_t     g_sfx_bank_cursor;
static int          g_sfx_next_voice;
static Audio_Music  g_music;

//
// Every file goes through here on its way to the SPU: a whole sound effect,
// or just the header sector of a song. Word aligned because the SPU DMA
// reads straight out of it. 64 KB is the size cap on a single effect.
//
static uint32_t g_staging[AUDIO_SFX_FILE_MAX_BYTES / 4];

void audio_initialize(void)
{
  spu_initialize();

  g_sfx_count       = 0;
  g_sfx_bank_cursor = SPU_SAMPLE_BANK_ADDR;
  g_sfx_next_voice  = 0;
  memory_zero(&g_music, sizeof(g_music));
  g_music.vol_l = AUDIO_VOLUME_MAX;
  g_music.vol_r = AUDIO_VOLUME_MAX;
}

// ----------------------------------------------------------------------------
//  Sound effects
// ----------------------------------------------------------------------------

Audio_Sfx audio_sfx_load(const char* cd_path)
{
  CD_File file = cd_file_open(cd_path);

  if (!file.valid) {
    _debugprintf("[AUDIO] sfx not on disc: %s", cd_path);
    return AUDIO_SFX_INVALID;
  }
  if (g_sfx_count >= AUDIO_SFX_MAX) {
    _debugprintf("[AUDIO] sfx table full (%d), dropping %s", AUDIO_SFX_MAX, cd_path);
    return AUDIO_SFX_INVALID;
  }

  size_t read_size = cd_file_get_memory_required(&file);
  if (read_size > sizeof(g_staging)) {
    _debugprintf("[AUDIO] sfx too big for staging (%d > %d): %s", read_size, sizeof(g_staging), cd_path);
    return AUDIO_SFX_INVALID;
  }

  cd_file_read_sync_uncached(&file, (uint8_t*) g_staging, read_size);

  Audio_Vag_Header* header = (Audio_Vag_Header*) g_staging;
  if (header->magic != AUDIO_VAG_MAGIC_SAMPLE) {
    _debugprintf("[AUDIO] not a mono .VAG (magic %08x): %s", header->magic, cd_path);
    return AUDIO_SFX_INVALID;
  }

  uint32_t data_size   = __builtin_bswap32(header->size);
  int      sample_rate = __builtin_bswap32(header->sample_rate);
  uint32_t upload_size = (data_size + 63) & ~((uint32_t) 63);

  if (g_sfx_bank_cursor + upload_size > SPU_SAMPLE_BANK_END) {
    _debugprintf("[AUDIO] sound RAM bank full, dropping %s", cd_path);
    return AUDIO_SFX_INVALID;
  }

  //
  // The ADPCM starts right after the 48-byte header, which keeps it word
  // aligned for the DMA. psxavenc pads the data to 64 bytes so the rounded
  // upload never reads past the file.
  //
  spu_upload(g_sfx_bank_cursor, (uint8_t*) g_staging + AUDIO_VAG_HEADER_SIZE, upload_size);

  Audio_Sfx    id     = g_sfx_count++;
  Audio_Sample* sample = &g_sfx[id];

  sample->spu_addr     = g_sfx_bank_cursor;
  sample->sample_rate  = sample_rate;
  g_sfx_bank_cursor   += upload_size;

  _debugprintf("[AUDIO] sfx %d: %s, %d Hz, %d bytes at 0x%05x", id, cd_path, sample_rate, data_size, sample->spu_addr);
  return id;
}

//
// Silences the pool first: a voice still reading a sample we are about to
// overwrite would play garbage.
//
void audio_sfx_unload_all(void)
{
  for (int voice = SPU_VOICE_SFX_FIRST; voice < SPU_VOICE_COUNT; ++voice) {
    spu_voice_stop(voice);
  }
  g_sfx_count       = 0;
  g_sfx_bank_cursor = SPU_SAMPLE_BANK_ADDR;
}

int audio_sfx_play(Audio_Sfx sfx)
{
  return audio_sfx_play_ex(sfx, AUDIO_VOLUME_MAX, AUDIO_VOLUME_MAX, AUDIO_PITCH_NORMAL);
}

//
// ponytail: plain round-robin over the 22 pool voices, no priorities. A new
// sound only ever steals a voice that was keyed 22 sounds ago. If effects
// start getting cut off, scan SPU_CHAN_STATUS (ENDX bits) for a finished
// voice before falling back to round-robin.
//
int audio_sfx_play_ex(Audio_Sfx sfx, int16_t vol_l, int16_t vol_r, int pitch)
{
  if (sfx < 0 || sfx >= g_sfx_count) {
    return AUDIO_VOICE_NONE;
  }

  Audio_Sample* sample = &g_sfx[sfx];
  int           voice  = SPU_VOICE_SFX_FIRST + g_sfx_next_voice;

  g_sfx_next_voice = (g_sfx_next_voice + 1) % AUDIO_SFX_VOICE_COUNT;

  //
  // The SPU pitch register is 12-bit fixed point relative to 44.1 kHz, so a
  // 22.05 kHz sample plays at 0x800 and the caller's pitch just scales that.
  //
  uint32_t freq = ((uint32_t) getSPUSampleRate(sample->sample_rate) * (uint32_t) pitch) >> 12;
  if (freq > 0x3FFF) {
    freq = 0x3FFF;
  }

  spu_voice_play(voice, sample->spu_addr, (uint16_t) freq, vol_l, vol_r);
  return voice;
}

void audio_sfx_stop(int voice)
{
  if (voice >= SPU_VOICE_SFX_FIRST && voice < SPU_VOICE_COUNT) {
    spu_voice_stop(voice);
  }
}

// ----------------------------------------------------------------------------
//  Music
// ----------------------------------------------------------------------------

//
// Runs in the CD interrupt when a music read lands. The bytes are already in
// the ring (the drive DMAs them straight there); this just tells the stream
// driver they are valid.
//
static void _music_read_done(CdlIntrResult result, uint8_t* payload)
{
  UNUSED(payload);
  if (result != CdlDiskError) {
    spu_stream_feed(g_music.inflight_sectors * CD_SECTOR_SIZE);
  }
  g_music.inflight_sectors = 0;
}

static int _music_at_end(void)
{
  return !g_music.loop && g_music.next_sector >= g_music.sector_count;
}

//
// Kicks off one asynchronous read of as many sectors as fit contiguously in
// the ring, clipped to the end of the file. When looping, the next call simply
// starts over at sector 0, which is what makes the loop seamless: the ring
// never knows the file ended.
//
static int _music_issue_read(void)
{
  uint8_t* destination;
  int      sectors   = (int) (spu_stream_feed_ptr(&destination) / CD_SECTOR_SIZE);
  int      remaining = g_music.sector_count - g_music.next_sector;

  if (sectors <= 0) {
    return 0;
  }
  if (remaining <= 0) {
    if (!g_music.loop) {
      return 0;
    }
    g_music.next_sector = 0;
    remaining           = g_music.sector_count;
  }
  if (sectors > remaining) {
    sectors = remaining;
  }

  CdlLOC position;
  CdIntToPos(g_music.start_lba + g_music.next_sector, &position);

  g_music.inflight_sectors = sectors;
  g_music.next_sector     += sectors;

  CdControl(CdlSetloc, &position, 0);
  CdReadCallback(_music_read_done);
  if (!CdRead(sectors, (uint32_t*) destination, CdlModeSpeed)) {
    _debugprintf("[AUDIO] CdRead refused %d sectors at %d", sectors, g_music.next_sector - sectors);
    g_music.next_sector     -= sectors;
    g_music.inflight_sectors = 0;
    return 0;
  }
  return sectors;
}

int audio_music_play(const char* cd_path, int loop)
{
  audio_music_stop();

  CD_File file = cd_file_open(cd_path);
  if (!file.valid) {
    _debugprintf("[AUDIO] music not on disc: %s", cd_path);
    return 0;
  }

  //
  // Sector 0 of the file is the header (psxavenc pads it to 2048 bytes). The
  // data starts at sector 1 and is an exact number of chunks, each one being
  // [L slice][R slice] of SPU_STREAM_INTERLEAVE bytes apiece.
  //
  cd_file_read_sync_uncached(&file, (uint8_t*) g_staging, CD_SECTOR_SIZE);

  Audio_Vag_Header* header = (Audio_Vag_Header*) g_staging;
  if (header->magic != AUDIO_VAG_MAGIC_STREAM) {
    _debugprintf("[AUDIO] not an interleaved .VAG (magic %08x): %s", header->magic, cd_path);
    return 0;
  }

  int      channels    = header->channels ? header->channels : 2;
  int      interleave  = header->interleave;
  uint32_t data_size   = __builtin_bswap32(header->size);
  int      sample_rate = __builtin_bswap32(header->sample_rate);
  int      chunks      = (data_size + interleave - 1) / interleave;

  if (interleave != SPU_STREAM_INTERLEAVE || channels > SPU_STREAM_CHANNELS_MAX) {
    _debugprintf("[AUDIO] %s: interleave %d channels %d; encode with -i %d -c 1|2", cd_path, interleave, channels, SPU_STREAM_INTERLEAVE);
    return 0;
  }

  g_music.loop             = loop;
  g_music.start_lba        = CdPosToInt(&file.file_index.pos) + 1;
  g_music.sector_count     = (chunks * interleave * channels) / CD_SECTOR_SIZE;
  g_music.next_sector      = 0;
  g_music.inflight_sectors = 0;
  g_music.fade_total       = 0;
  g_music.state            = AUDIO_MUSIC_PLAYING;

  //
  // Fill the ring before the first note. Blocking, but 96 KB at 2x is about
  // a third of a second, and a song start is a natural place to spend it.
  //
  while (!_music_at_end() && spu_stream_free_bytes() >= CD_SECTOR_SIZE) {
    if (!_music_issue_read()) {
      break;
    }
    CdReadSync(0, 0);
  }

  if (!spu_stream_start(channels, sample_rate, g_music.vol_l, g_music.vol_r)) {
    g_music.state = AUDIO_MUSIC_STOPPED;
    return 0;
  }

  _debugprintf("[AUDIO] music: %s, %d Hz, %d ch, %d sectors, loop %d", cd_path, sample_rate, channels, g_music.sector_count, loop);
  return 1;
}

//
// Order matters: wait for any read still in flight (it is DMAing into the
// ring), unhook the callback so a later data load cannot trigger it, then
// stop the driver, which resets the ring.
//
void audio_music_stop(void)
{
  CdReadSync(0, 0);
  CdReadCallback(0);
  spu_stream_stop();

  g_music.state            = AUDIO_MUSIC_STOPPED;
  g_music.inflight_sectors = 0;
  g_music.fade_total       = 0;
}

void audio_music_fade_out(int frames)
{
  if (g_music.state == AUDIO_MUSIC_STOPPED || frames <= 0) {
    audio_music_stop();
    return;
  }
  g_music.fade_total = frames;
  g_music.fade_left  = frames;
}

void audio_music_set_volume(int16_t vol_l, int16_t vol_r)
{
  g_music.vol_l = vol_l;
  g_music.vol_r = vol_r;
  spu_stream_set_volume(vol_l, vol_r);
}

int audio_music_is_playing(void)
{
  return g_music.state != AUDIO_MUSIC_STOPPED;
}

size_t audio_music_buffered_bytes(void)
{
  return spu_stream_buffered_bytes();
}

size_t audio_music_ring_bytes(void)
{
  return SPU_STREAM_RING_SIZE;
}

int audio_music_underruns(void)
{
  return spu_stream_underruns();
}

// ----------------------------------------------------------------------------
//  Per-frame
// ----------------------------------------------------------------------------

//
// The feeder. CdReadSync(1, 0) is a non-blocking "sectors still to come"
// query, so this never waits on the drive; a frame where the drive is busy
// (ours or a blocking load from cdfs) is simply skipped. The drive is idle
// most of the time: a 22.05 kHz stereo song eats 12 sectors a second and the
// drive delivers 150.
//
void audio_update(void)
{
  if (g_music.state == AUDIO_MUSIC_STOPPED) {
    return;
  }

  if (g_music.fade_total) {
    g_music.fade_left--;
    if (g_music.fade_left <= 0) {
      audio_music_stop();
      return;
    }
    spu_stream_set_volume((int16_t) ((int) g_music.vol_l * g_music.fade_left / g_music.fade_total),
                          (int16_t) ((int) g_music.vol_r * g_music.fade_left / g_music.fade_total));
  }

  if (g_music.state == AUDIO_MUSIC_PLAYING) {
    int drive_busy = CdReadSync(1, 0) > 0;

    if (!drive_busy) {
      if (_music_at_end()) {
        // every byte has landed in the ring; the driver stops itself once it plays out
        spu_stream_set_end();
        g_music.state = AUDIO_MUSIC_DRAINING;
      } else if (spu_stream_free_bytes() >= AUDIO_REFILL_SECTORS * CD_SECTOR_SIZE) {
        _music_issue_read();
      }
    }
  }

  if (!spu_stream_is_active()) {
    audio_music_stop();
  }
}
