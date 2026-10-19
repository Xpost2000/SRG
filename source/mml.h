#ifndef MML_H
#define MML_H

//
// The MML song parser. MML (Music Macro Language) is how 80s computers and a
// lot of chiptune trackers wrote music as text; the format is documented in
// engine-docs/audio.md. mml_song_parse() turns a song's text into fixed-size
// note events once, at load, and audio.c's sequencer plays the events.
//
// Only audio.c includes this. Game code loads songs through
// audio_song_load().
//
// Parsed events live in one static pool in mml.c (6 bytes each), so a song
// costs main RAM once, at load, and never again.
//

#include "common.h"

#define MML_TRACK_MAX  (8)    // tracks A-H, one voice each
#define MML_NOTE_REST  (0)    // MIDI 0 is far below anything audible, so it is free
#define MML_NOTE_END   (0xFF) // end of track
#define MML_VOLUME_MAX (15)

//
// Time. Songs count in ticks, 24 to a quarter note: fine enough for 32nd
// notes (3) and triplet eighths (8). The sequencer runs once per frame and
// adds a 16.16 fixed-point number of ticks (tick_step) to an accumulator,
// firing a tick every time it crosses MML_TICK_ONE:
//
//   ticks per frame = bpm * 24 / (60 seconds * 60 frames)
//
// so 120 bpm is 0.8 ticks a frame and a tick never fires more than twice in
// one frame below 300 bpm.
//
#define MML_TICKS_PER_QUARTER (24)
#define MML_TICK_ONE          (1 << 16)

typedef struct Mml_Event Mml_Event;
typedef struct Mml_Song  Mml_Song;

struct Mml_Event {
  uint16_t ticks;      // until this track's next event
  uint8_t  note;       // MIDI note, MML_NOTE_REST or MML_NOTE_END
  uint8_t  instrument; // enum Audio_Instrument
  uint8_t  volume;     // MML v, 0..MML_VOLUME_MAX
  uint8_t  _pad;
};

// A parsed song: where each track's events start, and its tempo.
struct Mml_Song {
  uint32_t         tick_step; // 16.16 ticks per frame
  int              track_count;
  const Mml_Event* tracks[MML_TRACK_MAX];
};

//
// Parses zero-terminated MML text into song. path is only for error
// messages. Bad input is logged with its line number and skipped, never
// fatal. Returns 0 if the text has no tracks, and then takes no events.
//
int mml_song_parse(Mml_Song* song, const char* text, const char* path);

#endif
