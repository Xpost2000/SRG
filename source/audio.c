#include "audio.h"
#include "spu.h"
#include "cdfs.h"
#include "memory_arena.h"

//
// .VAG header, 48 bytes, as psxavenc writes it. The size and sample rate are
// big-endian (Sony's PC tools were Mac-first). The magic is read as a
// little-endian word so the constant is reversed.
//
#define AUDIO_VAG_HEADER_SIZE (48)
#define AUDIO_VAG_MAGIC       (0x70474156) // "VAGp": mono sample

typedef struct Audio_Vag_Header Audio_Vag_Header;

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

//
// The SPU DMA moves data in 64-byte blocks, so every sample in sound RAM
// starts and ends on a 64-byte boundary.
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
//                    audio_note_play() and to each track of a jingle
//
#define AUDIO_TRACK_MAX         (8)
#define AUDIO_MUSIC_VOICE_FIRST (0)
#define AUDIO_POOL_VOICE_FIRST  (AUDIO_MUSIC_VOICE_FIRST + AUDIO_TRACK_MAX)
#define AUDIO_POOL_VOICE_COUNT  (SPU_VOICE_COUNT - AUDIO_POOL_VOICE_FIRST)
#define AUDIO_JINGLE_MAX        (4) // jingles that can overlap

//
// Time. Songs count in ticks, 24 to a quarter note: fine enough for 32nd
// notes (3) and triplet eighths (8). The sequencer runs once per frame and
// adds a 16.16 fixed-point number of ticks to an accumulator, firing a tick
// every time it crosses 1.0:
//
//   ticks per frame = bpm * 24 / (60 seconds * 60 frames)
//
// so 120 bpm is 0.8 ticks a frame and a tick never fires more than twice in
// one frame below 300 bpm.
//
// ponytail: assumes 60 Hz (NTSC). A PAL build wants 50 * 60 in the divisor;
// read it from the video mode if we ever ship PAL.
//
#define AUDIO_TICKS_PER_QUARTER (24)
#define AUDIO_TICKS_PER_WHOLE   (AUDIO_TICKS_PER_QUARTER * 4)
#define AUDIO_FRAMES_PER_MINUTE (60 * 60)
#define AUDIO_TICK_ONE          (1 << 16)
#define AUDIO_TEMPO_DEFAULT     (120)

//
// Every note of every loaded song lives in one static pool, 6 bytes each.
// 1024 notes is 6 KB: the test theme is 147. Raise it when the songs need
// more; the boot log prints each song's event count.
//
#define AUDIO_EVENT_MAX       (1024)
#define AUDIO_NOTE_REST       (0)    // MIDI 0 is far below anything audible, so it is free
#define AUDIO_NOTE_END        (0xFF) // end of track
#define AUDIO_MML_VOLUME_MAX  (15)
#define AUDIO_MML_OCTAVE_INIT (4)    // o4 c is middle C, AUDIO_NOTE_ROOT

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

// ----------------------------------------------------------------------------
//  State
// ----------------------------------------------------------------------------

typedef struct Audio_Instrument_Def Audio_Instrument_Def;
typedef struct Audio_Sample         Audio_Sample;
typedef struct Audio_Event          Audio_Event;
typedef struct Audio_Score          Audio_Score;
typedef struct Audio_Fade           Audio_Fade;
typedef struct Audio_Track          Audio_Track;
typedef struct Audio_Player         Audio_Player;
typedef struct Audio_Mml            Audio_Mml;

struct Audio_Instrument_Def {
  const char* name;    // what an MML song writes after @
  const char* cd_path;
  uint16_t    adsr1;
  uint16_t    adsr2;
};

//
// The bank, the "sound font". Every sample here is recorded so that playing
// it at its own sample rate sounds middle C (MIDI 60): that is the whole
// tuning model, and it is why there is no root-note column. Sustained
// instruments are short single-cycle loops; percussion and the blip are
// one-shots. Same order as enum Audio_Instrument in audio.h.
//
// A new instrument is a .wav at C4, one srg_audio_sample line in
// CMakeLists.txt, one <file> in iso.xml, one row here and one enum entry.
//
static const Audio_Instrument_Def g_bank_defs[AUDIO_INSTRUMENT_COUNT] = {
  { "PIANO", "\\AUDIO\\PIANO.VAG", AUDIO_ADSR1_INSTANT, AUDIO_ADSR2_RELEASE_MID   },
  { "BASS",  "\\AUDIO\\BASS.VAG",  AUDIO_ADSR1_INSTANT, AUDIO_ADSR2_RELEASE_SHORT },
  { "LEAD",  "\\AUDIO\\LEAD.VAG",  AUDIO_ADSR1_INSTANT, AUDIO_ADSR2_RELEASE_SHORT },
  { "KICK",  "\\AUDIO\\KICK.VAG",  AUDIO_ADSR1_INSTANT, AUDIO_ADSR2_RELEASE_LONG  },
  { "SNARE", "\\AUDIO\\SNARE.VAG", AUDIO_ADSR1_INSTANT, AUDIO_ADSR2_RELEASE_LONG  },
  { "HAT",   "\\AUDIO\\HAT.VAG",   AUDIO_ADSR1_INSTANT, AUDIO_ADSR2_RELEASE_LONG  },
  { "BLIP",  "\\AUDIO\\BLIP.VAG",  AUDIO_ADSR1_INSTANT, AUDIO_ADSR2_RELEASE_LONG  },
};

struct Audio_Sample {
  uint32_t spu_addr;  // 0 = failed to load, notes on it are skipped
  uint16_t root_freq; // SPU pitch register value that plays AUDIO_NOTE_ROOT
};

struct Audio_Event {
  uint16_t ticks;      // until this track's next event
  uint8_t  note;       // MIDI note, AUDIO_NOTE_REST or AUDIO_NOTE_END
  uint8_t  instrument;
  uint8_t  volume;     // MML v, 0..AUDIO_MML_VOLUME_MAX
  uint8_t  _pad;
};

// A loaded song: where each track's events start in g_events, and its tempo.
struct Audio_Score {
  uint32_t tick_step; // 16.16 ticks per frame
  int      track_count;
  uint16_t first_event[AUDIO_TRACK_MAX];
};

//
// A linear fade to silence, counted in frames.
//
struct Audio_Fade {
  int total; // frames; 0 = no fade running
  int left;
};

struct Audio_Track {
  const Audio_Event* event;  // next to fire; NULL once the track has ended
  int                wait;   // ticks until it fires
  int                voice;
  int                volume; // MML volume of the note sounding now, for fades
};

//
// Plays one score. The music is one of these; each jingle is another.
//
struct Audio_Player {
  const Audio_Score* score; // NULL = idle
  int                loop;
  uint32_t           phase; // 16.16 ticks accumulated, fires a tick per 1.0
  int16_t            vol_l;
  int16_t            vol_r;
  Audio_Fade         fade;
  Audio_Track        tracks[AUDIO_TRACK_MAX];
};

static Audio_Sample g_bank[AUDIO_INSTRUMENT_COUNT];
static Memory_Bump  g_bank_ram; // the sample bank region of sound RAM, see spu.h
static Audio_Event  g_events[AUDIO_EVENT_MAX];
static int          g_event_count;
static Audio_Score  g_scores[AUDIO_SONG_MAX];
static int          g_score_count;
static Audio_Player g_music;
static Audio_Player g_jingles[AUDIO_JINGLE_MAX];
static int          g_next_jingle;
static int          g_next_pool_voice;

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

  uint32_t data_size   = __builtin_bswap32(header->size);
  int      sample_rate = (int) __builtin_bswap32(header->sample_rate);
  uint32_t spu_addr    = memory_bump_push(&g_bank_ram, data_size, AUDIO_SPU_ALIGN);

  if (!spu_addr) {
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
  spu_upload(spu_addr, adpcm, align_up(data_size, AUDIO_SPU_ALIGN));

  g_bank[instrument].spu_addr  = spu_addr;
  g_bank[instrument].root_freq = getSPUSampleRate(sample_rate);

  _debugprintf("[AUDIO] %s: %d Hz, %d bytes at 0x%05x", def->name, sample_rate, data_size, spu_addr);
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
// the semitone within the octave (this table, 2^(n/12) in 4.12):
//
static const uint16_t g_semitone_ratio[12] = {
  4096, 4340, 4598, 4871, 5161, 5468, 5793, 6137, 6502, 6889, 7298, 7732,
};

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
                 g_bank_defs[instrument].adsr1, g_bank_defs[instrument].adsr2);
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

  memory_bump_init(&g_bank_ram, SPU_SAMPLE_BANK_ADDR, SPU_SAMPLE_BANK_END);
  memory_zero(g_bank, sizeof(g_bank));
  memory_zero(&g_music, sizeof(g_music));
  memory_zero(g_jingles, sizeof(g_jingles));
  g_event_count     = 0;
  g_score_count     = 0;
  g_next_jingle     = 0;
  g_next_pool_voice = 0;

  g_music.vol_l = AUDIO_VOLUME_MAX;
  g_music.vol_r = AUDIO_VOLUME_MAX;

  for (int instrument = 0; instrument < AUDIO_INSTRUMENT_COUNT; ++instrument) {
    _bank_load(instrument);
  }

  _debugprintf("[AUDIO] bank: %d instruments, %d bytes of sound RAM", AUDIO_INSTRUMENT_COUNT, g_bank_ram.cursor - SPU_SAMPLE_BANK_ADDR);
}

// ----------------------------------------------------------------------------
//  Songs: the MML parser
// ----------------------------------------------------------------------------
//
// MML (Music Macro Language) is how 80s computers and a lot of chiptune
// trackers wrote music as text. Ours, one track per line prefix:
//
//   t120                            ; tempo in bpm, on a line of its own
//   A @LEAD v12 o4 l8 c e g >c< g4  ; track A: lead, volume 12, octave 4, eighths
//   A c2 r2                         ; more of track A, appended
//   B @BASS o2 l4 c c g g           ; track B, starts at the same time as A
//
//   A-H        first character of a line: which track it belongs to
//   @NAME      instrument, a name from g_bank_defs
//   c d e f g a b   a note; + or # sharpens, - flattens; then an optional
//                   length (4 = quarter, 8 = eighth, 3 = half-note triplet)
//                   and dots (. adds half again)
//   r          rest, same length rules
//   o n        octave (o4 c is middle C); < one down, > one up
//   l n        default length for notes without one
//   v n        volume 0-15
//   ; ...      comment to end of line
//
// The text is parsed once, at load, into fixed-size events. Each track is a
// separate pass over the whole file, which keeps each track's events
// contiguous in g_events even when its lines are interleaved with others'.
// Bad input is logged with its line number and skipped, never fatal.
//

struct Audio_Mml {
  Audio_Score* score;
  const char*  path;
  const char*  at;
  int          line;
  int          global;     // a line with no track letter: only t is allowed
  int          octave;
  int          length;     // ticks
  int          volume;
  int          instrument;
};

static int _mml_is_digit(char c)
{
  return c >= '0' && c <= '9';
}

static char _mml_lower(char c)
{
  return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

static void _mml_error(Audio_Mml* mml, const char* what)
{
  _debugprintf("[AUDIO] %s:%d: %s", mml->path, mml->line, what);
  UNUSED(mml);
  UNUSED(what);
}

static int _mml_number(Audio_Mml* mml, int fallback)
{
  if (!_mml_is_digit(*mml->at)) {
    return fallback;
  }
  int value = 0;
  while (_mml_is_digit(*mml->at)) {
    value = value * 10 + (*mml->at++ - '0');
  }
  return value;
}

//
// "8" is an eighth note, 96 / 8 = 12 ticks. Each dot adds half of what the
// previous one added: c4. is 24 + 12, c4.. is 24 + 12 + 6.
//
static int _mml_length(Audio_Mml* mml, int fallback)
{
  int ticks   = fallback;
  int divisor = _mml_number(mml, 0);

  if (divisor > 0 && divisor <= AUDIO_TICKS_PER_WHOLE) {
    ticks = AUDIO_TICKS_PER_WHOLE / divisor;
  } else if (divisor != 0) {
    _mml_error(mml, "length out of range");
  }

  for (int add = ticks / 2; *mml->at == '.'; add /= 2) {
    mml->at++;
    ticks += add;
  }
  return ticks;
}

static void _mml_instrument(Audio_Mml* mml)
{
  const char* name = mml->at;
  int         size = 0;

  while ((name[size] >= 'A' && name[size] <= 'Z') || _mml_is_digit(name[size]) || name[size] == '_') {
    size++;
  }
  mml->at += size;

  for (int i = 0; i < AUDIO_INSTRUMENT_COUNT; ++i) {
    const char* candidate = g_bank_defs[i].name;
    if (strncmp(candidate, name, size) == 0 && candidate[size] == '\0') {
      mml->instrument = i;
      return;
    }
  }
  _mml_error(mml, "unknown instrument");
}

//
// The last AUDIO_TRACK_MAX slots of the pool are kept back for end markers,
// so a track cut short by a full pool is still terminated and the player
// never runs off into the next track's notes.
//
static void _mml_emit(Audio_Mml* mml, int note, int ticks)
{
  if (mml->global) {
    _mml_error(mml, "note outside a track (start the line with A-H)");
    return;
  }
  if (note != AUDIO_NOTE_END && g_event_count >= AUDIO_EVENT_MAX - AUDIO_TRACK_MAX) {
    _mml_error(mml, "event pool full, note dropped");
    return;
  }

  Audio_Event* event = &g_events[g_event_count++];
  event->ticks      = (uint16_t) ticks;
  event->note       = (uint8_t) note;
  event->instrument = (uint8_t) mml->instrument;
  event->volume     = (uint8_t) mml->volume;
  event->_pad       = 0;
}

static void _mml_parse_line(Audio_Mml* mml)
{
  //                                    a  b  c  d  e  f  g
  static const int8_t semitone_of[7] = { 9, 11, 0, 2, 4, 5, 7 };

  while (*mml->at && *mml->at != '\n' && *mml->at != ';') {
    char c = _mml_lower(*mml->at++);

    if (c == ' ' || c == '\t' || c == '\r') {
      continue;
    }

    if (c == 't') {
      int tempo = _mml_number(mml, AUDIO_TEMPO_DEFAULT);
      if (tempo < 1 || tempo > 600) {
        _mml_error(mml, "tempo out of range");
      } else {
        mml->score->tick_step = (uint32_t) tempo * AUDIO_TICKS_PER_QUARTER * AUDIO_TICK_ONE / AUDIO_FRAMES_PER_MINUTE;
      }
    } else if (mml->global) {
      _mml_error(mml, "only t (tempo) may appear outside a track");
      while (*mml->at && *mml->at != '\n') {
        mml->at++;
      }
    } else if (c >= 'a' && c <= 'g') {
      int note = (mml->octave + 1) * 12 + semitone_of[c - 'a'];
      for (; *mml->at == '+' || *mml->at == '#' || *mml->at == '-'; mml->at++) {
        note += (*mml->at == '-') ? -1 : 1;
      }
      int ticks = _mml_length(mml, mml->length);
      if (note < 1 || note > 127) {
        _mml_error(mml, "note out of range");
      } else {
        _mml_emit(mml, note, ticks);
      }
    } else if (c == 'r') {
      _mml_emit(mml, AUDIO_NOTE_REST, _mml_length(mml, mml->length));
    } else if (c == 'o') {
      mml->octave = _mml_number(mml, mml->octave);
    } else if (c == '<') {
      mml->octave--;
    } else if (c == '>') {
      mml->octave++;
    } else if (c == 'l') {
      mml->length = _mml_length(mml, mml->length);
    } else if (c == 'v') {
      mml->volume = clamp(_mml_number(mml, mml->volume), AUDIO_MML_VOLUME_MAX, 0);
    } else if (c == '@') {
      _mml_instrument(mml);
    } else {
      _mml_error(mml, "unexpected character");
    }
  }
}

static const char* _mml_next_line(const char* line)
{
  while (*line && *line != '\n') {
    line++;
  }
  return *line ? line + 1 : line;
}

static int _mml_is_track_letter(char c)
{
  return c >= 'A' && c < 'A' + AUDIO_TRACK_MAX;
}

//
// One pass over the file for one track letter. Lines with no track letter
// (tempo, blank, comment) are read on track A's pass only.
//
static void _mml_parse_track(Audio_Score* score, const char* text, const char* path, int track)
{
  Audio_Mml mml = {
    .score      = score,
    .path       = path,
    .octave     = AUDIO_MML_OCTAVE_INIT,
    .length     = AUDIO_TICKS_PER_QUARTER,
    .volume     = AUDIO_MML_VOLUME_MAX,
    .instrument = 0,
  };
  int first = g_event_count;
  int found = 0;

  for (const char* line = text; *line; line = _mml_next_line(line)) {
    mml.line++;
    if (line[0] == 'A' + track) {
      found      = 1;
      mml.global = 0;
      mml.at     = line + 1;
      _mml_parse_line(&mml);
    } else if (track == 0 && !_mml_is_track_letter(line[0])) {
      mml.global = 1;
      mml.at     = line;
      _mml_parse_line(&mml);
    }
  }

  if (found) {
    mml.global = 0;
    _mml_emit(&mml, AUDIO_NOTE_END, 0);
    score->first_event[score->track_count++] = (uint16_t) first;
  }
}

Audio_Song audio_song_load(const char* cd_path)
{
  if (g_score_count >= AUDIO_SONG_MAX) {
    _debugprintf("[AUDIO] song table full (%d), dropping %s", AUDIO_SONG_MAX, cd_path);
    return AUDIO_SONG_INVALID;
  }

  size_t size;
  char*  text = (char*) _file_load(cd_path, &size);
  if (!text) {
    return AUDIO_SONG_INVALID;
  }
  text[size] = '\0';

  Audio_Score* score       = &g_scores[g_score_count];
  int          first_event = g_event_count;

  score->track_count = 0;
  score->tick_step   = (uint32_t) AUDIO_TEMPO_DEFAULT * AUDIO_TICKS_PER_QUARTER * AUDIO_TICK_ONE / AUDIO_FRAMES_PER_MINUTE;

  for (int track = 0; track < AUDIO_TRACK_MAX; ++track) {
    _mml_parse_track(score, text, cd_path, track);
  }
  free(text);

  if (score->track_count == 0) {
    _debugprintf("[AUDIO] no tracks in %s (lines must start with A-H)", cd_path);
    g_event_count = first_event;
    return AUDIO_SONG_INVALID;
  }

  _debugprintf("[AUDIO] song %d: %s, %d tracks, %d events", g_score_count, cd_path, score->track_count, g_event_count - first_event);
  return g_score_count++;
}

// ----------------------------------------------------------------------------
//  Fades
// ----------------------------------------------------------------------------

static void _fade_start(Audio_Fade* fade, int frames)
{
  fade->total = frames;
  fade->left  = frames;
}

static int _fade_is_running(Audio_Fade* fade)
{
  return fade->total != 0;
}

// Advances one frame. Returns 1 once the fade has reached silence.
static int _fade_step(Audio_Fade* fade)
{
  fade->left--;
  return fade->left <= 0;
}

// volume * left / total: full volume on the first frame, silence on the last.
static int16_t _fade_apply(Audio_Fade* fade, int16_t volume)
{
  return (int16_t) ((int) volume * fade->left / fade->total);
}

// ----------------------------------------------------------------------------
//  The sequencer
// ----------------------------------------------------------------------------
//
// A player walks each track's events in step. A track is just a pointer and
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

static int16_t _player_volume(Audio_Player* player, int16_t side, int mml_volume)
{
  int16_t volume = (int16_t) ((int) side * mml_volume / AUDIO_MML_VOLUME_MAX);

  if (_fade_is_running(&player->fade)) {
    volume = _fade_apply(&player->fade, volume);
  }
  return volume;
}

static void _track_fire(Audio_Player* player, Audio_Track* track)
{
  const Audio_Event* event = track->event;

  if (event->note == AUDIO_NOTE_END) {
    spu_voice_stop(track->voice);
    track->event = NULL;
    return;
  }

  if (event->note == AUDIO_NOTE_REST) {
    spu_voice_stop(track->voice);
  } else {
    _voice_play_note(track->voice, event->instrument, event->note,
                     _player_volume(player, player->vol_l, event->volume),
                     _player_volume(player, player->vol_r, event->volume));
  }

  track->volume = event->volume;
  track->wait   = event->ticks;
  track->event++;
}

// One tick of every track. Returns 1 while any track still has events.
static int _player_tick(Audio_Player* player)
{
  int running = 0;

  for (int i = 0; i < player->score->track_count; ++i) {
    Audio_Track* track = &player->tracks[i];

    if (!track->event) {
      continue;
    }
    if (track->wait > 0) {
      track->wait--;
    }
    if (track->wait == 0) {
      _track_fire(player, track);
    }
    running |= track->event != NULL;
  }
  return running;
}

// Points every track back at its first event and plays tick 0 immediately.
static void _player_rewind(Audio_Player* player)
{
  for (int i = 0; i < player->score->track_count; ++i) {
    player->tracks[i].event = &g_events[player->score->first_event[i]];
    player->tracks[i].wait  = 0;
  }
  _player_tick(player);
}

static void _player_start(Audio_Player* player, Audio_Song song, int loop)
{
  player->score = &g_scores[song];
  player->loop  = loop;
  player->phase = 0;
  _player_rewind(player);
}

static void _player_stop(Audio_Player* player)
{
  if (!player->score) {
    return;
  }
  for (int i = 0; i < player->score->track_count; ++i) {
    spu_voice_stop(player->tracks[i].voice);
  }
  player->score = NULL;
}

static void _player_update(Audio_Player* player)
{
  if (!player->score) {
    return;
  }

  player->phase += player->score->tick_step;
  while (player->phase >= AUDIO_TICK_ONE) {
    player->phase -= AUDIO_TICK_ONE;
    if (!_player_tick(player)) {
      if (!player->loop) {
        player->score = NULL;
        return;
      }
      _player_rewind(player);
    }
  }
}

// Re-applies volume to the notes already sounding, for fades and volume changes.
static void _player_apply_volume(Audio_Player* player)
{
  for (int i = 0; i < player->score->track_count; ++i) {
    Audio_Track* track = &player->tracks[i];
    spu_voice_set_volume(track->voice,
                         _player_volume(player, player->vol_l, track->volume),
                         _player_volume(player, player->vol_r, track->volume));
  }
}

static int _song_is_valid(Audio_Song song)
{
  return song >= 0 && song < g_score_count;
}

// ----------------------------------------------------------------------------
//  Sound effects
// ----------------------------------------------------------------------------

int audio_note_play(int instrument, int note, int16_t vol_l, int16_t vol_r)
{
  if (instrument < 0 || instrument >= AUDIO_INSTRUMENT_COUNT) {
    return AUDIO_VOICE_NONE;
  }
  int voice = _pool_voice_next();
  _voice_play_note(voice, instrument, note, vol_l, vol_r);
  return voice;
}

void audio_note_stop(int voice)
{
  if (voice >= AUDIO_POOL_VOICE_FIRST && voice < SPU_VOICE_COUNT) {
    spu_voice_stop(voice);
  }
}

//
// Jingles round-robin over their own few players, and each track borrows a
// pool voice for the jingle's whole run.
//
void audio_sfx_play(Audio_Song jingle)
{
  if (!_song_is_valid(jingle)) {
    return;
  }

  Audio_Player* player = &g_jingles[g_next_jingle];
  g_next_jingle = (g_next_jingle + 1) % AUDIO_JINGLE_MAX;

  _player_stop(player);
  player->vol_l = AUDIO_VOLUME_MAX;
  player->vol_r = AUDIO_VOLUME_MAX;
  for (int i = 0; i < g_scores[jingle].track_count; ++i) {
    player->tracks[i].voice = _pool_voice_next();
  }
  _player_start(player, jingle, 0);
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

  for (int i = 0; i < AUDIO_TRACK_MAX; ++i) {
    g_music.tracks[i].voice = AUDIO_MUSIC_VOICE_FIRST + i;
  }
  _player_start(&g_music, song, loop);
}

void audio_music_stop(void)
{
  _player_stop(&g_music);
  g_music.fade.total = 0;
}

void audio_music_fade_out(int frames)
{
  if (!g_music.score || frames <= 0) {
    audio_music_stop();
    return;
  }
  _fade_start(&g_music.fade, frames);
}

//
// The user's chosen volume. A fade scales it but leaves it alone, so the
// next song starts at this volume again.
//
void audio_music_set_volume(int16_t vol_l, int16_t vol_r)
{
  g_music.vol_l = vol_l;
  g_music.vol_r = vol_r;
  if (g_music.score) {
    _player_apply_volume(&g_music);
  }
}

int audio_music_is_playing(void)
{
  return g_music.score != NULL;
}

// ----------------------------------------------------------------------------
//  Per-frame
// ----------------------------------------------------------------------------

//
// Step the music fade, then advance every player by one frame's worth of
// ticks. Never blocks, never touches the disc.
//
void audio_update(void)
{
  if (g_music.score && _fade_is_running(&g_music.fade)) {
    if (_fade_step(&g_music.fade)) {
      audio_music_stop();
    } else {
      _player_apply_volume(&g_music);
    }
  }

  _player_update(&g_music);
  for (int i = 0; i < AUDIO_JINGLE_MAX; ++i) {
    _player_update(&g_jingles[i]);
  }
}
