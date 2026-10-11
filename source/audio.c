#include "audio.h"
#include "spu.h"
#include "cdfs.h"
#include "memory_arena.h"
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
typedef struct Audio_Vag_Info   Audio_Vag_Info;

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

// The header with the byte order sorted out.
struct Audio_Vag_Info {
  uint32_t data_size;   // ADPCM bytes per channel
  int      sample_rate;
  int      channels;
  int      interleave;
};

//
// The SPU DMA moves data in 64-byte blocks, so every sample in sound RAM
// starts and ends on a 64-byte boundary.
//
#define AUDIO_SPU_ALIGN (64)

//
// How many sectors must be free before the feeder bothers the drive. One read
// of 8 sectors (16 KB) at a time beats a seek per frame, and keeps the 40 KB
// ring at least 24 KB full between refills.
//
#define AUDIO_REFILL_SECTORS (8)

//
// Staging buffer size in sectors. Sound effects pass through it a piece at a
// time, so it does not cap how big an effect can be; it only trades RAM for
// seeks, since each piece is its own CdRead. 4 sectors = 8 KB, so a 64 KB
// effect is 8 reads.
//
#define AUDIO_STAGING_SECTORS (4)
#define AUDIO_STAGING_SIZE    (AUDIO_STAGING_SECTORS * CD_SECTOR_SIZE)

#define AUDIO_SFX_VOICE_COUNT (SPU_VOICE_COUNT - SPU_VOICE_SFX_FIRST)

// ----------------------------------------------------------------------------
//  State
// ----------------------------------------------------------------------------

typedef struct Audio_Sample    Audio_Sample;
typedef struct Audio_Fade      Audio_Fade;
typedef struct Audio_Sfx_Voice Audio_Sfx_Voice;
typedef struct Audio_Music     Audio_Music;

struct Audio_Sample {
  uint32_t spu_addr;
  int      sample_rate;
};

//
// A linear fade, counted in frames: up from silence to the volume the caller
// set (in), or down from it to silence (out). Not tied to music: anything
// with a volume can hold one, step it once a frame and scale its volume.
//
struct Audio_Fade {
  int total; // frames; 0 = no fade running
  int left;
  int in;    // 1 = silence -> volume, 0 = volume -> silence
};

//
// One per pool voice, so any effect that is playing can be faded. The
// volume is what audio_sfx_play_ex() was given; the fade scales it.
//
struct Audio_Sfx_Voice {
  int16_t    vol_l;
  int16_t    vol_r;
  Audio_Fade fade;
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
  volatile int inflight_sectors; // size of our read the drive is working on; 0 = none
  int16_t      vol_l;
  int16_t      vol_r;
  Audio_Fade   fade;
};

static Audio_Sample    g_sfx[AUDIO_SFX_MAX];
static int             g_sfx_count;
static Memory_Bump     g_sfx_bank; // the sample bank region of sound RAM, see spu.h
static int             g_sfx_next_voice;
static Audio_Sfx_Voice g_sfx_voices[AUDIO_SFX_VOICE_COUNT]; // index = voice - SPU_VOICE_SFX_FIRST
static Audio_Music     g_music;

//
// Every file goes through here on its way to the SPU: a sound effect 8 KB at
// a time, or just the header sector of a song. Word aligned because the SPU
// DMA reads straight out of it.
//
static uint32_t g_staging[AUDIO_STAGING_SIZE / 4];

static void _music_read_done(CdlIntrResult result, uint8_t* payload);

void audio_initialize(void)
{
  spu_initialize();

  g_sfx_count      = 0;
  g_sfx_next_voice = 0;
  memory_bump_init(&g_sfx_bank, SPU_SAMPLE_BANK_ADDR, SPU_SAMPLE_BANK_END);
  memory_zero(&g_sfx_voices, sizeof(g_sfx_voices));

  memory_zero(&g_music, sizeof(g_music));
  g_music.vol_l = AUDIO_VOLUME_MAX;
  g_music.vol_r = AUDIO_VOLUME_MAX;

  //
  // The read-complete callback is one global hook shared by every CD read.
  // We install it once and leave it: _music_read_done ignores any read it
  // did not issue (inflight_sectors == 0), so cdfs loads pass straight
  // through without anyone having to unhook or restore it.
  //
  CdReadCallback(_music_read_done);
}

//
// Checks the magic and byte-swaps the big-endian fields. Returns 0 if the
// file is not the flavour the caller expected.
//
static int _vag_read_header(const void* bytes, uint32_t magic, Audio_Vag_Info* info)
{
  const Audio_Vag_Header* header = (const Audio_Vag_Header*) bytes;

  if (header->magic != magic) {
    _debugprintf("[AUDIO] wrong .VAG flavour (magic %08x, want %08x)", header->magic, magic);
    return 0;
  }

  info->data_size   = __builtin_bswap32(header->size);
  info->sample_rate = __builtin_bswap32(header->sample_rate);
  info->channels    = header->channels ? header->channels : 2;
  info->interleave  = header->interleave;
  return 1;
}

// ----------------------------------------------------------------------------
//  Fades
// ----------------------------------------------------------------------------

static void _fade_start(Audio_Fade* fade, int frames, int in)
{
  fade->total = frames;
  fade->left  = frames;
  fade->in    = in;
}

static int _fade_is_running(Audio_Fade* fade)
{
  return fade->total != 0;
}

//
// Advances one frame. Returns 1 once the fade has reached its end, and
// leaves fade->in set so the caller knows which end: full volume (keep
// playing) or silence (stop the voice).
//
static int _fade_step(Audio_Fade* fade)
{
  fade->left--;
  if (fade->left > 0) {
    return 0;
  }
  fade->total = 0;
  return 1;
}

//
// The volume to send this frame. Out: volume * left / total, full on the
// first frame and silent on the last. In: the same ramp run backwards.
//
static int16_t _fade_apply(Audio_Fade* fade, int16_t volume)
{
  int level = fade->in ? fade->total - fade->left : fade->left;
  return (int16_t) ((int) volume * level / fade->total);
}

// ----------------------------------------------------------------------------
//  Sound effects
// ----------------------------------------------------------------------------

//
// Blocking read of `sectors` sectors, starting `first` sectors into the file.
// cdfs only reads a file from its start, and an effect now arrives a piece at
// a time. Waits out any music read still in flight first. inflight_sectors
// stays 0 for this read, so _music_read_done lets it pass.
//
static int _sfx_read_sectors(CD_File* file, int first, int sectors)
{
  CdlLOC position;

  CdReadSync(0, 0);
  CdIntToPos(CdPosToInt(&file->file_index.pos) + first, &position);
  CdControl(CdlSetloc, &position, 0);
  if (!CdRead(sectors, g_staging, CdlModeSpeed)) {
    return 0;
  }
  return CdReadSync(0, 0) >= 0;
}

//
// The effect goes to sound RAM one staging buffer at a time, header and all,
// so the bytes land in sound RAM exactly as they sit in the file:
//
//   bank allocation (64-aligned)
//   | 16 B pad | 48 B header | ADPCM ...                         |
//              ^ upload_addr ^ spu_addr (64-aligned, the sample)
//
// Uploading the header too means every piece starts at a sector boundary of
// the file and lands on an 8-byte SPU address, with no shuffling in main
// RAM. The 64 bytes ahead of the sample are dead space no voice points at.
//
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

  int file_sectors = (int) (cd_file_get_memory_required(&file) / CD_SECTOR_SIZE);

  if (!_sfx_read_sectors(&file, 0, min(file_sectors, AUDIO_STAGING_SECTORS))) {
    _debugprintf("[AUDIO] read failed: %s", cd_path);
    return AUDIO_SFX_INVALID;
  }

  Audio_Vag_Info info;
  if (!_vag_read_header(g_staging, AUDIO_VAG_MAGIC_SAMPLE, &info)) {
    _debugprintf("[AUDIO] not a mono .VAG: %s", cd_path);
    return AUDIO_SFX_INVALID;
  }

  // psxavenc pads the data to 64 bytes, so this never reads past the file
  uint32_t upload_size = align_up(AUDIO_VAG_HEADER_SIZE + info.data_size, AUDIO_SPU_ALIGN);
  uint32_t bank_addr   = memory_bump_push(&g_sfx_bank, AUDIO_SPU_ALIGN - AUDIO_VAG_HEADER_SIZE + upload_size, AUDIO_SPU_ALIGN);
  if (!bank_addr) {
    _debugprintf("[AUDIO] sound RAM bank full, dropping %s", cd_path);
    return AUDIO_SFX_INVALID;
  }

  uint32_t spu_addr    = bank_addr + AUDIO_SPU_ALIGN;
  uint32_t upload_addr = spu_addr - AUDIO_VAG_HEADER_SIZE;

  for (uint32_t offset = 0; offset < upload_size; offset += AUDIO_STAGING_SIZE) {
    int first = (int) (offset / CD_SECTOR_SIZE);

    // the first piece is already in staging from the header read
    if (first > 0 && !_sfx_read_sectors(&file, first, min(file_sectors - first, AUDIO_STAGING_SECTORS))) {
      _debugprintf("[AUDIO] read failed: %s", cd_path);
      return AUDIO_SFX_INVALID; // ponytail: the bank space leaks until audio_sfx_unload_all()
    }
    spu_upload(upload_addr + offset, g_staging, min(upload_size - offset, (uint32_t) AUDIO_STAGING_SIZE));
  }

  Audio_Sfx     id     = g_sfx_count++;
  Audio_Sample* sample = &g_sfx[id];

  sample->spu_addr    = spu_addr;
  sample->sample_rate = info.sample_rate;

  _debugprintf("[AUDIO] sfx %d: %s, %d Hz, %d bytes at 0x%05x", id, cd_path, info.sample_rate, info.data_size, spu_addr);
  return id;
}

//
// Silences the pool first: a voice still reading a sample we are about to
// overwrite would play garbage.
//
void audio_sfx_unload_all(void)
{
  for (int voice = SPU_VOICE_SFX_FIRST; voice < SPU_VOICE_COUNT; ++voice) {
    audio_sfx_stop(voice);
  }
  g_sfx_count = 0;
  memory_bump_reset(&g_sfx_bank);
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

  // a stolen voice must not inherit the previous sound's fade
  Audio_Sfx_Voice* state = &g_sfx_voices[voice - SPU_VOICE_SFX_FIRST];
  state->vol_l      = vol_l;
  state->vol_r      = vol_r;
  state->fade.total = 0;
  return voice;
}

static int _sfx_voice_is_valid(int voice)
{
  return voice >= SPU_VOICE_SFX_FIRST && voice < SPU_VOICE_COUNT;
}

void audio_sfx_stop(int voice)
{
  if (_sfx_voice_is_valid(voice)) {
    spu_voice_stop(voice);
    g_sfx_voices[voice - SPU_VOICE_SFX_FIRST].fade.total = 0;
  }
}

//
// Fading in drops the voice to silence right away, so call it straight after
// audio_sfx_play_ex(): the SPU has had a few microseconds at full volume,
// far too short to hear. The fade then climbs back to the play volume.
//
// ponytail: a voice is just a pool slot, so 22 plays later it belongs to a
// different sound. Fade an effect's voice soon after playing it, or keep
// the voice number only as long as you know the sound is still yours.
//
void audio_sfx_fade_in(int voice, int frames)
{
  if (!_sfx_voice_is_valid(voice) || frames <= 0) {
    return;
  }
  Audio_Sfx_Voice* state = &g_sfx_voices[voice - SPU_VOICE_SFX_FIRST];
  _fade_start(&state->fade, frames, 1);
  spu_voice_set_volume(voice, 0, 0);
}

void audio_sfx_fade_out(int voice, int frames)
{
  if (!_sfx_voice_is_valid(voice)) {
    return;
  }
  if (frames <= 0) {
    audio_sfx_stop(voice);
    return;
  }
  _fade_start(&g_sfx_voices[voice - SPU_VOICE_SFX_FIRST].fade, frames, 0);
}

// ----------------------------------------------------------------------------
//  Music
// ----------------------------------------------------------------------------
//
// How a song gets from the disc to the speakers:
//
//   CD file ──CdRead (DMA)──► ring, 40 KB main RAM ──SPU IRQ──► SPU double buffer ──► voices 0,1
//            ^^^^^^^^^^^^^^                         ^^^^^^^^^
//            this file: a few big reads,            spu.c: one chunk every
//            only when the drive is idle            ~650 ms at 11 kHz, all by itself
//
// This file is only the left half: keep the ring topped up.
//

//
// Runs in the CD interrupt whenever ANY read lands, ours or a cdfs load. The
// bytes are already in the ring (the drive DMAs them straight there); this
// just tells the stream driver they are valid. inflight_sectors == 0 means
// the read was not ours, so there is nothing to feed.
//
static void _music_read_done(CdlIntrResult result, uint8_t* payload)
{
  UNUSED(payload);
  if (g_music.inflight_sectors == 0) {
    return;
  }
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
// Kicks off one asynchronous read: as many sectors as fit contiguously in the
// ring, clipped to the end of the file. Returns the sector count, 0 if no
// read was issued.
//
static int _music_read_next_sectors(void)
{
  uint8_t* destination;
  int      sectors = (int) (spu_stream_feed_ptr(&destination) / CD_SECTOR_SIZE);

  if (sectors <= 0) {
    return 0;
  }

  //
  // Looping: once the last sector has been read, just start reading from
  // sector 0 again. The ring never learns the file ended, which is what makes
  // the loop seamless.
  //
  if (g_music.next_sector >= g_music.sector_count) {
    if (!g_music.loop) {
      return 0;
    }
    g_music.next_sector = 0;
  }

  sectors = min(sectors, g_music.sector_count - g_music.next_sector);

  CdlLOC position;
  CdIntToPos(g_music.start_lba + g_music.next_sector, &position);

  // set before CdRead: the completion interrupt can fire before CdRead returns
  g_music.inflight_sectors = sectors;

  CdControl(CdlSetloc, &position, 0);
  if (!CdRead(sectors, (uint32_t*) destination, CdlModeSpeed)) {
    _debugprintf("[AUDIO] CdRead refused %d sectors at %d", sectors, g_music.next_sector);
    g_music.inflight_sectors = 0;
    return 0;
  }

  g_music.next_sector += sectors;
  return sectors;
}

//
// Fill the ring before the first note so the stream starts with ~3.2 seconds
// of slack (11 kHz stereo). Blocking, but 40 KB at 2x is about 0.15 seconds, and a
// song start is a natural place to spend it.
//
static void _music_prefill_ring(void)
{
  while (!_music_at_end() && spu_stream_free_bytes() >= CD_SECTOR_SIZE) {
    if (!_music_read_next_sectors()) {
      break;
    }
    CdReadSync(0, 0);
  }
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

  Audio_Vag_Info info;
  if (!_vag_read_header(g_staging, AUDIO_VAG_MAGIC_STREAM, &info)) {
    _debugprintf("[AUDIO] not an interleaved .VAG: %s", cd_path);
    return 0;
  }
  if (info.interleave != SPU_STREAM_INTERLEAVE || info.channels > SPU_STREAM_CHANNELS_MAX) {
    _debugprintf("[AUDIO] %s: interleave %d channels %d; encode with -i %d -c 1|2", cd_path, info.interleave, info.channels, SPU_STREAM_INTERLEAVE);
    return 0;
  }

  int chunks = (info.data_size + info.interleave - 1) / info.interleave;

  g_music.loop             = loop;
  g_music.start_lba        = CdPosToInt(&file.file_index.pos) + 1;
  g_music.sector_count     = (chunks * info.interleave * info.channels) / CD_SECTOR_SIZE;
  g_music.next_sector      = 0;
  g_music.inflight_sectors = 0;
  g_music.fade.total       = 0;
  g_music.state            = AUDIO_MUSIC_PLAYING;

  _music_prefill_ring();

  if (!spu_stream_start(info.channels, info.sample_rate, g_music.vol_l, g_music.vol_r)) {
    g_music.state = AUDIO_MUSIC_STOPPED;
    return 0;
  }

  _debugprintf("[AUDIO] music: %s, %d Hz, %d ch, %d sectors, loop %d", cd_path, info.sample_rate, info.channels, g_music.sector_count, loop);
  return 1;
}

//
// Wait for any read of ours still in flight (it is DMAing into the ring)
// before stopping the driver, which resets the ring under it.
//
void audio_music_stop(void)
{
  CdReadSync(0, 0);
  spu_stream_stop();

  g_music.state            = AUDIO_MUSIC_STOPPED;
  g_music.inflight_sectors = 0;
  g_music.fade.total       = 0;
}

void audio_music_fade_out(int frames)
{
  if (g_music.state == AUDIO_MUSIC_STOPPED || frames <= 0) {
    audio_music_stop();
    return;
  }
  _fade_start(&g_music.fade, frames, 0);
}

//
// Same deal as audio_sfx_fade_in(): call it right after audio_music_play().
// The song starts silent and climbs to the volume set with
// audio_music_set_volume().
//
void audio_music_fade_in(int frames)
{
  if (g_music.state == AUDIO_MUSIC_STOPPED || frames <= 0) {
    return;
  }
  _fade_start(&g_music.fade, frames, 1);
  spu_stream_set_volume(0, 0);
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
// Steps one pool voice's fade and pushes the scaled volume to it. A fade in
// that finishes lands on the play volume; a fade out that finishes keys the
// voice off.
//
static void _sfx_step_fade(int voice)
{
  Audio_Sfx_Voice* state = &g_sfx_voices[voice - SPU_VOICE_SFX_FIRST];

  if (!_fade_is_running(&state->fade)) {
    return;
  }
  if (_fade_step(&state->fade)) {
    if (state->fade.in) {
      spu_voice_set_volume(voice, state->vol_l, state->vol_r);
    } else {
      spu_voice_stop(voice);
    }
    return;
  }
  spu_voice_set_volume(voice, _fade_apply(&state->fade, state->vol_l),
                              _fade_apply(&state->fade, state->vol_r));
}

//
// Steps a running fade one frame and pushes the scaled volume to the stream.
// The user's chosen volume (vol_l/vol_r) is left alone so the next song
// starts at full volume again. Returns 1 when a fade out has hit silence.
//
static int _music_step_fade(void)
{
  if (!_fade_is_running(&g_music.fade)) {
    return 0;
  }
  if (_fade_step(&g_music.fade)) {
    if (g_music.fade.in) {
      spu_stream_set_volume(g_music.vol_l, g_music.vol_r);
      return 0;
    }
    return 1;
  }
  spu_stream_set_volume(_fade_apply(&g_music.fade, g_music.vol_l),
                        _fade_apply(&g_music.fade, g_music.vol_r));
  return 0;
}

//
// The feeder: decide whether to start one more CD read this frame.
//
//   drive busy?        skip. CdReadSync(1, 0) asks without blocking. The
//                      drive may be finishing our last read, or a scene's
//                      own cdfs load may own it; either way, try next frame.
//
//   whole file read?   tell the driver "no more data is coming" and go to
//                      DRAINING. Not a stop: the last ~3.2 s of the song is
//                      still sitting in the ring waiting to play.
//
//   ring has room?     only once 8 sectors are free. An 11 kHz stereo song
//                      eats ~6 sectors a second and the drive delivers 150,
//                      so one 16 KB read every ~1.3 s keeps up while
//                      leaving the drive idle nearly all the time.
//
static void _music_step_feeder(void)
{
  int drive_busy = CdReadSync(1, 0) > 0;

  if (drive_busy) {
    return;
  }

  if (_music_at_end()) {
    spu_stream_set_end();
    g_music.state = AUDIO_MUSIC_DRAINING;
  } else if (spu_stream_free_bytes() >= AUDIO_REFILL_SECTORS * CD_SECTOR_SIZE) {
    _music_read_next_sectors();
  }
}

//
// Music state over a song's life:
//
//            audio_music_play()                   last sector read
//   STOPPED ────────────────────► PLAYING ─────────────────────────► DRAINING
//      ▲                          feeder runs                        ring plays out,
//      │                                                             no more reads
//      └──── fade reached silence, audio_music_stop(), or the ────────┘
//            stream driver ran out of data and parked its voices
//
// Never blocks, so it is safe to call every frame and between files in a
// load loop.
//
void audio_update(void)
{
  for (int voice = SPU_VOICE_SFX_FIRST; voice < SPU_VOICE_COUNT; ++voice) {
    _sfx_step_fade(voice);
  }

  if (g_music.state == AUDIO_MUSIC_STOPPED) {
    return;
  }

  if (_music_step_fade()) {
    audio_music_stop();
    return;
  }

  if (g_music.state == AUDIO_MUSIC_PLAYING) {
    _music_step_feeder();
  }

  // the driver clears this itself after the final chunk of a DRAINING song
  if (!spu_stream_is_active()) {
    audio_music_stop();
  }
}
