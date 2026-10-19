#include "mml.h"
#include "audio.h"

//
// The MML dialect, one track per line prefix (full reference in
// engine-docs/audio.md):
//
//   t120                            ; tempo in bpm, on a line of its own
//   A @LEAD v12 o4 l8 c e g >c< g4  ; track A: lead, volume 12, octave 4, eighths
//   A c2 r2                         ; more of track A, appended
//   B @BASS o2 l4 c c g g           ; track B, starts at the same time as A
//
// The text is parsed once, at load, into fixed-size events. Each track is a
// separate pass over the whole file, which keeps each track's events
// contiguous in g_events even when its lines are interleaved with others'.
//
// ponytail: assumes 60 Hz (NTSC). A PAL build wants 50 * 60 in the divisor;
// read it from the video mode if we ever ship PAL.
//
#define MML_TICKS_PER_WHOLE   (MML_TICKS_PER_QUARTER * 4)
#define MML_FRAMES_PER_MINUTE (60 * 60)
#define MML_TEMPO_DEFAULT     (120)
#define MML_TEMPO_MAX         (600)
#define MML_OCTAVE_INIT       (4) // o4 c is middle C, AUDIO_NOTE_ROOT

//
// Every note of every loaded song lives in one static pool, 6 bytes each.
// 1024 notes is 6 KB: the test theme is 147. Raise it when the songs need
// more; the boot log prints each song's event count.
//
#define MML_EVENT_MAX (1024)

typedef struct Mml_Parser Mml_Parser;

// Where one pass over the text is, and the state its commands have set.
struct Mml_Parser {
  Mml_Song*   song;
  const char* path;
  const char* at;
  int         line;
  int         global;     // a line with no track letter: only t is allowed
  int         octave;
  int         length;     // ticks
  int         volume;
  int         instrument;
};

static Mml_Event g_events[MML_EVENT_MAX];
static int       g_event_count;

static int         _mml_is_digit(char c);
static char        _mml_lower(char c);
static void        _mml_error(Mml_Parser* mml, const char* what);
static uint32_t    _mml_tick_step(int tempo);
static int         _mml_number(Mml_Parser* mml, int fallback);
static int         _mml_length(Mml_Parser* mml, int fallback);
static void        _mml_instrument(Mml_Parser* mml);
static void        _mml_emit(Mml_Parser* mml, int note, int ticks);
static void        _mml_parse_line(Mml_Parser* mml);
static const char* _mml_next_line(const char* line);
static int         _mml_is_track_letter(char c);
static void        _mml_parse_track(Mml_Song* song, const char* text, const char* path, int track);

static int _mml_is_digit(char c)
{
  return c >= '0' && c <= '9';
}

static char _mml_lower(char c)
{
  return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

static void _mml_error(Mml_Parser* mml, const char* what)
{
  _debugprintf("[MML] %s:%d: %s", mml->path, mml->line, what);
  UNUSED(mml);
  UNUSED(what);
}

static uint32_t _mml_tick_step(int tempo)
{
  return (uint32_t) tempo * MML_TICKS_PER_QUARTER * MML_TICK_ONE / MML_FRAMES_PER_MINUTE;
}

static int _mml_number(Mml_Parser* mml, int fallback)
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
static int _mml_length(Mml_Parser* mml, int fallback)
{
  int ticks   = fallback;
  int divisor = _mml_number(mml, 0);

  if (divisor > 0 && divisor <= MML_TICKS_PER_WHOLE) {
    ticks = MML_TICKS_PER_WHOLE / divisor;
  } else if (divisor != 0) {
    _mml_error(mml, "length out of range");
  }

  for (int add = ticks / 2; *mml->at == '.'; add /= 2) {
    mml->at++;
    ticks += add;
  }
  return ticks;
}

// @NAME: a name from the bank, AUDIO_INSTRUMENTS in audio.h.
static void _mml_instrument(Mml_Parser* mml)
{
  const char* name = mml->at;
  int         size = 0;

  while ((name[size] >= 'A' && name[size] <= 'Z') || _mml_is_digit(name[size]) || name[size] == '_') {
    size++;
  }
  mml->at += size;

  for (int i = 0; i < AUDIO_INSTRUMENT_COUNT; ++i) {
    const char* candidate = g_audio_instrument_names[i];
    if (strncmp(candidate, name, size) == 0 && candidate[size] == '\0') {
      mml->instrument = i;
      return;
    }
  }
  _mml_error(mml, "unknown instrument");
}

//
// The last MML_TRACK_MAX slots of the pool are kept back for end markers,
// so a track cut short by a full pool is still terminated and the player
// never runs off into the next track's notes.
//
static void _mml_emit(Mml_Parser* mml, int note, int ticks)
{
  if (mml->global) {
    _mml_error(mml, "note outside a track (start the line with A-H)");
    return;
  }
  if (note != MML_NOTE_END && g_event_count >= MML_EVENT_MAX - MML_TRACK_MAX) {
    _mml_error(mml, "event pool full, note dropped");
    return;
  }

  Mml_Event* event = &g_events[g_event_count++];
  event->ticks      = (uint16_t) ticks;
  event->note       = (uint8_t) note;
  event->instrument = (uint8_t) mml->instrument;
  event->volume     = (uint8_t) mml->volume;
  event->_pad       = 0;
}

static void _mml_parse_line(Mml_Parser* mml)
{
  //                                    a  b  c  d  e  f  g
  static const int8_t semitone_of[7] = { 9, 11, 0, 2, 4, 5, 7 };

  while (*mml->at && *mml->at != '\n' && *mml->at != ';') {
    char c = _mml_lower(*mml->at++);

    if (c == ' ' || c == '\t' || c == '\r') {
      continue;
    }

    if (c == 't') {
      int tempo = _mml_number(mml, MML_TEMPO_DEFAULT);
      if (tempo < 1 || tempo > MML_TEMPO_MAX) {
        _mml_error(mml, "tempo out of range");
      } else {
        mml->song->tick_step = _mml_tick_step(tempo);
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
      _mml_emit(mml, MML_NOTE_REST, _mml_length(mml, mml->length));
    } else if (c == 'o') {
      mml->octave = _mml_number(mml, mml->octave);
    } else if (c == '<') {
      mml->octave--;
    } else if (c == '>') {
      mml->octave++;
    } else if (c == 'l') {
      mml->length = _mml_length(mml, mml->length);
    } else if (c == 'v') {
      mml->volume = clamp(_mml_number(mml, mml->volume), MML_VOLUME_MAX, 0);
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
  return c >= 'A' && c < 'A' + MML_TRACK_MAX;
}

//
// One pass over the file for one track letter. Lines with no track letter
// (tempo, blank, comment) are read on track A's pass only.
//
static void _mml_parse_track(Mml_Song* song, const char* text, const char* path, int track)
{
  Mml_Parser mml = {
    .song       = song,
    .path       = path,
    .octave     = MML_OCTAVE_INIT,
    .length     = MML_TICKS_PER_QUARTER,
    .volume     = MML_VOLUME_MAX,
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
    _mml_emit(&mml, MML_NOTE_END, 0);
    song->tracks[song->track_count++] = &g_events[first];
  }
}

int mml_song_parse(Mml_Song* song, const char* text, const char* path)
{
  int first_event = g_event_count;

  song->track_count = 0;
  song->tick_step   = _mml_tick_step(MML_TEMPO_DEFAULT);

  for (int track = 0; track < MML_TRACK_MAX; ++track) {
    _mml_parse_track(song, text, path, track);
  }

  if (song->track_count == 0) {
    _debugprintf("[MML] no tracks in %s (lines must start with A-H)", path);
    g_event_count = first_event;
    return 0;
  }

  _debugprintf("[MML] %s: %d tracks, %d events", path, song->track_count, g_event_count - first_event);
  return 1;
}
