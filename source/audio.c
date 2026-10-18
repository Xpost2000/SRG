#include "audio.h"
#include "mml.h"
#include "spu.h"
#include "cdfs.h"

//
// .VAG header, 48 bytes, as psxavenc writes it. The size and sample rate are
// big-endian (Sony's PC tools were Mac-first). The magic is read as a
// little-endian word so the constant is reversed.
//
#define AUDIO_VAG_HEADER_SIZE (48)
#define AUDIO_VAG_MAGIC       (0x70474156) // "VAGp": mono sample

//
// The SPU DMA moves data in 64-byte blocks, so every sample in sound RAM
// starts and ends on a 64-byte boundary. SPU_SAMPLE_BANK_ADDR is already
// aligned and every sample's size is padded, so the cursor stays aligned
// without ever rounding its start.
//
#define AUDIO_SPU_ALIGN (64)

//
// Voice map. The SPU has 24 voices and no mixer to speak of: one voice is one
// note sounding. We split them once, statically:
//
//   voice  0 .. 7    music, track n of the song always plays on voice n, so a
//                    song can have at most 8 tracks and can never be cut off
//                    by a burst of sound effects
//   voice  8 .. 23   the effect pool, 16 voices handed out round-robin to
//                    audio_sfx_play()
//
#define AUDIO_MUSIC_VOICE_FIRST (0)
#define AUDIO_POOL_VOICE_FIRST  (AUDIO_MUSIC_VOICE_FIRST + MML_TRACK_MAX)
#define AUDIO_POOL_VOICE_COUNT  (SPU_VOICE_COUNT - AUDIO_POOL_VOICE_FIRST)

//
// Envelopes. Each voice runs its own ADSR generator, set by two 16-bit
// registers, and the level it produces multiplies the sample. From psx-spx:
//
//   ADSR1   15     attack mode, 0 linear / 1 exponential
//           14-8   attack rate (shift << 2 | step), 0 = fastest
//           7-4    decay shift
//           3-0    sustain level, (n + 1) * 0x800
//
//   ADSR2   15-6   sustain mode, direction and rate
//           5      release mode, 0 linear / 1 exponential
//           4-0    release shift
//
// Our samples carry their own shape (the piano's decay is baked in, the
// lead is a flat looping square), so the envelope only has two jobs:
// get out of the way while the key is down, and fade out after key-off
// instead of clicking. ADSR1 0x00FF is an instant attack to full and a
// sustain level that full already satisfies; ADSR2 with sustain 0 holds
// there. Only the release shift varies, and for a linear release from full
// level (0x7FFF) at 44.1 kHz:
//
//   shift 10   16 per sample        ~46 ms    sustained tones, crisp
//   shift 12    8 per 2 samples    ~186 ms    piano, lets the note ring a bit
//   shift 13    8 per 4 samples    ~371 ms    drums, a rest never chokes a hit
//
#define AUDIO_ADSR1_INSTANT       (0x00FF)
#define AUDIO_ADSR2_RELEASE_SHORT (0x000A)
#define AUDIO_ADSR2_RELEASE_MID   (0x000C)
#define AUDIO_ADSR2_RELEASE_LONG  (0x000D)

//
// ADPCM blocks are 16 bytes: a shift/filter byte, a flag byte, then 28
// samples. The flags tell the voice what to do after the block:
//
//   bit 0  end       jump to the repeat address after this block
//   bit 1  repeat    ...and keep playing (clear: go silent, release)
//   bit 2  start     latch this block's address as the repeat address
//
// psxavenc -L sets end + repeat on the last block but no start flag
// anywhere, and every .VAG begins with a silent block 0. Left like that the
// repeat address is whatever the voice last latched, at best block 0, and a
// single-cycle loop gets a 28-sample gap of silence every lap: a buzz. So
// for a looping sample, mark block 1, the first real one, as the start.
//
#define AUDIO_ADPCM_BLOCK       (16)
#define AUDIO_ADPCM_FLAG_END    (1 << 0)
#define AUDIO_ADPCM_FLAG_REPEAT (1 << 1)
#define AUDIO_ADPCM_FLAG_START  (1 << 2)
#define AUDIO_ADPCM_FLAG_LOOP   (AUDIO_ADPCM_FLAG_END | AUDIO_ADPCM_FLAG_REPEAT)

#define AUDIO_FADE_FRAMES_MAX (0x7FFF) // 15 bits, about 9 minutes at 60 Hz

// ----------------------------------------------------------------------------
//  Types
// ----------------------------------------------------------------------------

typedef struct Audio_Vag_Header     Audio_Vag_Header;
typedef struct Audio_Instrument_Def Audio_Instrument_Def;
typedef struct Audio_Sample         Audio_Sample;
typedef struct Audio_Fade           Audio_Fade;
typedef struct Audio_Track          Audio_Track;
typedef struct Audio_Music          Audio_Music;

struct Audio_Vag_Header {
  uint32_t magic;
  uint32_t version;
  uint32_t interleave;
  uint32_t size;        // bytes of ADPCM, big-endian
  uint32_t sample_rate; // Hz, big-endian
  uint16_t reserved[5];
  uint16_t channels;
  char     name[16];
};

struct Audio_Instrument_Def {
  const char* cd_path;
  uint16_t    adsr2;
};

struct Audio_Sample {
  uint32_t spu_addr;  // 0 = failed to load, notes on it are skipped
  uint16_t root_freq; // SPU pitch register value that plays AUDIO_NOTE_ROOT
};

//
// A linear fade, counted in frames. Only the music has one: a sound effect
// is a single short note, and its envelope release already ends it softly.
//
struct Audio_Fade {
  uint16_t total   : 15; // frames; 0 = no fade running
  uint16_t fade_in : 1;  // 1: silence to full, 0: full to silence
  uint16_t frame;        // frames done so far
};

struct Audio_Track {
  const Mml_Event* event;  // next to fire; NULL once the track has ended
  int              wait;   // ticks until it fires
  int              volume; // MML volume of the note sounding now, for fades
};

//
// The music player: walks one song's tracks in step. Track n plays on voice
// AUDIO_MUSIC_VOICE_FIRST + n.
//
struct Audio_Music {
  const Mml_Song* song; // NULL = idle
  int             loop;
  uint32_t        phase; // 16.16 ticks accumulated, fires a tick per 1.0
  int16_t         vol_l;
  int16_t         vol_r;
  Audio_Fade      fade;
  Audio_Track     tracks[MML_TRACK_MAX];
};

// ----------------------------------------------------------------------------
//  State
// ----------------------------------------------------------------------------

const char* const g_audio_instrument_names[AUDIO_INSTRUMENT_COUNT] = {
#define X(name, release) #name,
  AUDIO_INSTRUMENTS
#undef X
};

//
// The bank, the "sound font". Every sample is recorded so that playing it at
// its own sample rate sounds middle C (MIDI 60): that is the whole tuning
// model, and it is why there is no root-note column. Sustained instruments
// are short single-cycle loops; percussion and effects are one-shots.
// Generated from AUDIO_INSTRUMENTS in audio.h, so it is always in enum order.
//
static const Audio_Instrument_Def g_bank_defs[AUDIO_INSTRUMENT_COUNT] = {
#define X(name, release) { "\\AUDIO\\" #name ".VAG", AUDIO_ADSR2_RELEASE_##release },
  AUDIO_INSTRUMENTS
#undef X
};

// 2^(n/12) in 4.12 fixed point, one entry per semitone of an octave. See _note_freq().
static const uint16_t g_semitone_ratio[12] = {
  4096, 4340, 4598, 4871, 5161, 5468, 5793, 6137, 6502, 6889, 7298, 7732,
};

static Audio_Sample g_bank[AUDIO_INSTRUMENT_COUNT];
static uint32_t     g_bank_cursor; // next free byte of the sample bank region of sound RAM, see spu.h
static Mml_Song     g_songs[AUDIO_SONG_MAX];
static int          g_song_count;
static Audio_Music  g_music;
static int          g_next_pool_voice;

// ----------------------------------------------------------------------------
//  Prototypes
// ----------------------------------------------------------------------------

static uint8_t* _file_load(const char* cd_path, size_t* size);
static void     _adpcm_mark_loop_start(uint8_t* adpcm, uint32_t size);
static void     _bank_upload(int instrument, uint8_t* vag);
static void     _bank_load(int instrument);
static uint16_t _note_freq(uint16_t root_freq, int note);
static void     _voice_play_note(int voice, int instrument, int note, int16_t vol_l, int16_t vol_r);
static int      _pool_voice_next(void);
static void     _fade_start(Audio_Fade* fade, int frames, int fade_in);
static int      _fade_is_running(const Audio_Fade* fade);
static int      _fade_step(Audio_Fade* fade);
static int16_t  _fade_apply(const Audio_Fade* fade, int16_t volume);
static int16_t  _music_volume(int16_t side, int mml_volume);
static void     _music_fire(int track_index);
static int      _music_tick(void);
static void     _music_rewind(void);
static void     _music_apply_volume(void);
static int      _song_is_valid(Audio_Song song);

// ----------------------------------------------------------------------------
//  Loading
// ----------------------------------------------------------------------------

//
// Every file is loaded at boot into a temporary heap block of exactly its own
// size: a whole instrument sample on its way to the SPU, or a song's text on
// its way to the parser. The caller frees it straight after, so after boot
// audio holds no load buffer at all and there is no cap on file size beyond
// free RAM. malloc returns word-aligned memory, which the SPU DMA needs.
//
// Returns NULL on failure. One spare byte is allocated so text can be zero
// terminated.
//
static uint8_t* _file_load(const char* cd_path, size_t* size)
{
  CD_File file = cd_file_open(cd_path);

  if (!file.valid) {
    _debugprintf("[AUDIO] not on disc: %s", cd_path);
    return NULL;
  }

  size_t   read_size = cd_file_get_memory_required(&file);
  uint8_t* bytes     = (uint8_t*) malloc(read_size + 1);

  if (!bytes) {
    _debugprintf("[AUDIO] out of RAM loading %s (%d bytes)", cd_path, read_size);
    return NULL;
  }

  cd_file_read_sync_uncached(&file, bytes, read_size);
  *size = cd_file_get_size(&file);
  return bytes;
}

// ----------------------------------------------------------------------------
//  The bank
// ----------------------------------------------------------------------------

static void _adpcm_mark_loop_start(uint8_t* adpcm, uint32_t size)
{
  for (uint32_t block = AUDIO_ADPCM_BLOCK; block + AUDIO_ADPCM_BLOCK <= size; block += AUDIO_ADPCM_BLOCK) {
    if ((adpcm[block + 1] & AUDIO_ADPCM_FLAG_LOOP) == AUDIO_ADPCM_FLAG_LOOP) {
      adpcm[AUDIO_ADPCM_BLOCK + 1] |= AUDIO_ADPCM_FLAG_START;
      return;
    }
  }
}

static void _bank_upload(int instrument, uint8_t* vag)
{
  const Audio_Instrument_Def* def    = &g_bank_defs[instrument];
  const Audio_Vag_Header*     header = (const Audio_Vag_Header*) vag;

  if (header->magic != AUDIO_VAG_MAGIC) {
    _debugprintf("[AUDIO] not a mono .VAG (magic %08x): %s", header->magic, def->cd_path);
    return;
  }

  uint32_t data_size   = swap32(header->size);
  int      sample_rate = (int) swap32(header->sample_rate);
  uint32_t upload_size = align_up(data_size, AUDIO_SPU_ALIGN);

  if (g_bank_cursor + upload_size > SPU_SAMPLE_BANK_END) {
    _debugprintf("[AUDIO] sound RAM bank full, dropping %s", def->cd_path);
    return;
  }

  //
  // The ADPCM starts right after the 48-byte header, which keeps it word
  // aligned for the DMA. psxavenc pads the data to 64 bytes so the rounded
  // upload never reads past the file.
  //
  uint8_t* adpcm = vag + AUDIO_VAG_HEADER_SIZE;
  _adpcm_mark_loop_start(adpcm, data_size);
  spu_upload(g_bank_cursor, adpcm, upload_size);

  g_bank[instrument].spu_addr  = g_bank_cursor;
  g_bank[instrument].root_freq = getSPUSampleRate(sample_rate);
  g_bank_cursor += upload_size;

  _debugprintf("[AUDIO] %s: %d Hz, %d bytes at 0x%05x", g_audio_instrument_names[instrument], sample_rate, data_size, g_bank[instrument].spu_addr);
}

static void _bank_load(int instrument)
{
  size_t   size;
  uint8_t* vag = _file_load(g_bank_defs[instrument].cd_path, &size);

  if (vag) {
    _bank_upload(instrument, vag);
    free(vag);
  }
}

//
// Note to SPU pitch. The pitch register is 4.12 fixed point relative to
// 44.1 kHz, and every sample plays middle C at root_freq, so a note is
// root_freq times 2^(semitones / 12). Split that into octaves (a shift) and
// the semitone within the octave (g_semitone_ratio).
//
static uint16_t _note_freq(uint16_t root_freq, int note)
{
  int      offset = note - AUDIO_NOTE_ROOT + 10 * 12; // biased positive so / and % floor
  int      octave = offset / 12 - 10;
  uint32_t freq   = ((uint32_t) root_freq * g_semitone_ratio[offset % 12]) >> 12;

  freq = (octave >= 0) ? (freq << octave) : (freq >> -octave);
  return (uint16_t) min(freq, (uint32_t) SPU_PITCH_MAX);
}

static void _voice_play_note(int voice, int instrument, int note, int16_t vol_l, int16_t vol_r)
{
  const Audio_Sample* sample = &g_bank[instrument];

  if (!sample->spu_addr) {
    return;
  }
  spu_voice_play(voice, sample->spu_addr, _note_freq(sample->root_freq, note), vol_l, vol_r,
                 AUDIO_ADSR1_INSTANT, g_bank_defs[instrument].adsr2);
}

//
// ponytail: plain round-robin over the 16 pool voices, no priorities. A new
// sound only ever steals a voice that was handed out 16 sounds ago. If
// effects start getting cut off, scan SPU_CHAN_STATUS (ENDX bits) for a
// finished voice before falling back to round-robin.
//
static int _pool_voice_next(void)
{
  int voice = AUDIO_POOL_VOICE_FIRST + g_next_pool_voice;
  g_next_pool_voice = (g_next_pool_voice + 1) % AUDIO_POOL_VOICE_COUNT;
  return voice;
}

void audio_initialize(void)
{
  spu_initialize();

  g_bank_cursor = SPU_SAMPLE_BANK_ADDR;
  memory_zero(g_bank, sizeof(g_bank));
  memory_zero(&g_music, sizeof(g_music));
  g_song_count      = 0;
  g_next_pool_voice = 0;

  g_music.vol_l = AUDIO_VOLUME_MAX;
  g_music.vol_r = AUDIO_VOLUME_MAX;

  for (int instrument = 0; instrument < AUDIO_INSTRUMENT_COUNT; ++instrument) {
    _bank_load(instrument);
  }

  _debugprintf("[AUDIO] bank: %d instruments, %d bytes of sound RAM", AUDIO_INSTRUMENT_COUNT, g_bank_cursor - SPU_SAMPLE_BANK_ADDR);
}

// ----------------------------------------------------------------------------
//  Songs
// ----------------------------------------------------------------------------

Audio_Song audio_song_load(const char* cd_path)
{
  if (g_song_count >= AUDIO_SONG_MAX) {
    _debugprintf("[AUDIO] song table full (%d), dropping %s", AUDIO_SONG_MAX, cd_path);
    return AUDIO_SONG_INVALID;
  }

  size_t size;
  char*  text = (char*) _file_load(cd_path, &size);
  if (!text) {
    return AUDIO_SONG_INVALID;
  }
  text[size] = '\0';

  int parsed = mml_song_parse(&g_songs[g_song_count], text, cd_path);
  free(text);
  return parsed ? g_song_count++ : AUDIO_SONG_INVALID;
}

static int _song_is_valid(Audio_Song song)
{
  return song >= 0 && song < g_song_count;
}

// ----------------------------------------------------------------------------
//  Fades
// ----------------------------------------------------------------------------

static void _fade_start(Audio_Fade* fade, int frames, int fade_in)
{
  fade->total   = min(frames, AUDIO_FADE_FRAMES_MAX);
  fade->fade_in = fade_in;
  fade->frame   = 0;
}

static int _fade_is_running(const Audio_Fade* fade)
{
  return fade->total != 0;
}

// Advances one frame. Returns 1 once the fade has reached its end.
static int _fade_step(Audio_Fade* fade)
{
  fade->frame++;
  return fade->frame >= fade->total;
}

// Fade out: full volume on the first frame, silence on the last. Fade in is the mirror.
static int16_t _fade_apply(const Audio_Fade* fade, int16_t volume)
{
  int level = fade->fade_in ? fade->frame : fade->total - fade->frame;
  return (int16_t) ((int) volume * level / fade->total);
}

// ----------------------------------------------------------------------------
//  The sequencer
// ----------------------------------------------------------------------------
//
// The music walks each track's events in step. A track is just a pointer and
// a countdown:
//
//   tick:  wait--  ──► wait == 0? ──► fire the event under the pointer:
//                                       note  key the voice on at that pitch
//                                       rest  key it off (release starts)
//                                       end   key it off, track is done
//                                     wait = event's ticks, pointer++
//
// A note is held until the track's next event; a new note keys straight over
// it (see spu_voice_play). When every track has hit its end the song is
// over, or, if it loops, every track rewinds together, so a short track sits
// silent until the longest one finishes rather than drifting out of time.
//

static int16_t _music_volume(int16_t side, int mml_volume)
{
  int16_t volume = (int16_t) ((int) side * mml_volume / MML_VOLUME_MAX);

  if (_fade_is_running(&g_music.fade)) {
    volume = _fade_apply(&g_music.fade, volume);
  }
  return volume;
}

static void _music_fire(int track_index)
{
  Audio_Track*     track = &g_music.tracks[track_index];
  const Mml_Event* event = track->event;
  int              voice = AUDIO_MUSIC_VOICE_FIRST + track_index;

  if (event->note == MML_NOTE_END) {
    spu_voice_stop(voice);
    track->event = NULL;
    return;
  }

  if (event->note == MML_NOTE_REST) {
    spu_voice_stop(voice);
  } else {
    _voice_play_note(voice, event->instrument, event->note,
                     _music_volume(g_music.vol_l, event->volume),
                     _music_volume(g_music.vol_r, event->volume));
  }

  track->volume = event->volume;
  track->wait   = event->ticks;
  track->event++;
}

// One tick of every track. Returns 1 while any track still has events.
static int _music_tick(void)
{
  int running = 0;

  for (int i = 0; i < g_music.song->track_count; ++i) {
    Audio_Track* track = &g_music.tracks[i];

    if (!track->event) {
      continue;
    }
    if (track->wait > 0) {
      track->wait--;
    }
    if (track->wait == 0) {
      _music_fire(i);
    }
    running |= track->event != NULL;
  }
  return running;
}

// Points every track back at its first event and plays tick 0 immediately.
static void _music_rewind(void)
{
  for (int i = 0; i < g_music.song->track_count; ++i) {
    g_music.tracks[i].event = g_music.song->tracks[i];
    g_music.tracks[i].wait  = 0;
  }
  _music_tick();
}

// Re-applies volume to the notes already sounding, for fades and volume changes.
static void _music_apply_volume(void)
{
  for (int i = 0; i < g_music.song->track_count; ++i) {
    spu_voice_set_volume(AUDIO_MUSIC_VOICE_FIRST + i,
                         _music_volume(g_music.vol_l, g_music.tracks[i].volume),
                         _music_volume(g_music.vol_r, g_music.tracks[i].volume));
  }
}

// ----------------------------------------------------------------------------
//  Sound effects
// ----------------------------------------------------------------------------

int audio_sfx_play(int instrument, int note, int16_t vol_l, int16_t vol_r)
{
  if (instrument < 0 || instrument >= AUDIO_INSTRUMENT_COUNT) {
    return AUDIO_VOICE_NONE;
  }
  int voice = _pool_voice_next();
  _voice_play_note(voice, instrument, note, vol_l, vol_r);
  return voice;
}

void audio_sfx_stop(int voice)
{
  if (voice >= AUDIO_POOL_VOICE_FIRST && voice < SPU_VOICE_COUNT) {
    spu_voice_stop(voice);
  }
}

// ----------------------------------------------------------------------------
//  Music
// ----------------------------------------------------------------------------

void audio_music_play(Audio_Song song, int loop)
{
  audio_music_stop();
  if (!_song_is_valid(song)) {
    return;
  }

  g_music.song  = &g_songs[song];
  g_music.loop  = loop;
  g_music.phase = 0;
  _music_rewind();
}

void audio_music_stop(void)
{
  if (g_music.song) {
    for (int i = 0; i < g_music.song->track_count; ++i) {
      spu_voice_stop(AUDIO_MUSIC_VOICE_FIRST + i);
    }
  }
  g_music.song       = NULL;
  g_music.fade.total = 0;
}

// Call straight after audio_music_play(): the notes already keyed drop to silence and ramp up.
void audio_music_fade_in(int frames)
{
  if (!g_music.song || frames <= 0) {
    return;
  }
  _fade_start(&g_music.fade, frames, 1);
  _music_apply_volume();
}

void audio_music_fade_out(int frames)
{
  if (!g_music.song || frames <= 0) {
    audio_music_stop();
    return;
  }
  _fade_start(&g_music.fade, frames, 0);
}

//
// The user's chosen volume. A fade scales it but leaves it alone, so the
// next song starts at this volume again.
//
void audio_music_set_volume(int16_t vol_l, int16_t vol_r)
{
  g_music.vol_l = vol_l;
  g_music.vol_r = vol_r;
  if (g_music.song) {
    _music_apply_volume();
  }
}

int audio_music_is_playing(void)
{
  return g_music.song != NULL;
}

// ----------------------------------------------------------------------------
//  Per-frame
// ----------------------------------------------------------------------------

//
// Step the music fade, then advance the song by one frame's worth of ticks.
// Never blocks, never touches the disc.
//
void audio_update(void)
{
  if (!g_music.song) {
    return;
  }

  if (_fade_is_running(&g_music.fade)) {
    if (!_fade_step(&g_music.fade)) {
      _music_apply_volume();
    } else if (g_music.fade.fade_in) {
      g_music.fade.total = 0;
      _music_apply_volume();
    } else {
      audio_music_stop();
      return;
    }
  }

  g_music.phase += g_music.song->tick_step;
  while (g_music.phase >= MML_TICK_ONE) {
    g_music.phase -= MML_TICK_ONE;
    if (!_music_tick()) {
      if (!g_music.loop) {
        g_music.song = NULL;
        return;
      }
      _music_rewind();
    }
  }
}
