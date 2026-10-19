#ifndef AUDIO_H
#define AUDIO_H

//
// The game-facing audio layer. Everything is resident: audio_initialize()
// uploads the sound bank into the SPU's sound RAM, audio_song_load() parses a
// song's text into a few KB of note events, and from then on the disc is
// never touched for sound again.
//
// There is one concept, the sound bank (our "sound font"): a handful of
// short samples, each tuned so it sounds middle C at its own sample rate,
// and so playable at any pitch. Everything the game hears is a note on one
// of them:
//
//   SOUND EFFECTS  one note on one instrument, on a pool voice (8-23):
//                  audio_sfx_play(AUDIO_INSTRUMENT_BLIP, AUDIO_NOTE_ROOT, ...)
//
//   MUSIC          an MML song (text, see engine-docs/audio.md), up to 8
//                  tracks of notes on bank instruments, one track per voice
//                  (0-7). One song at a time; it can loop and fade.
//
// Everything is polled. Call audio_update() once per frame; it is the
// sequencer, and it is what keys the music's notes on and off. Parsing the
// MML text is mml.c's job; this file only plays what it produced.
//
// Instrument samples come from res/audio/*.wav via the srg_audio_sample rules
// in cmake/audio.cmake; songs are res/audio/*.mml, copied to the disc as is.
//
// NOTE:
// - audio_initialize() must run after cd_start(): it reads the bank off the
//   disc.
// - Load every song at boot. Loading reads the disc and parses text; playing
//   is free.
//

#include "common.h"

#define AUDIO_SONG_MAX     (32)
#define AUDIO_SONG_INVALID (-1)
#define AUDIO_VOICE_NONE   (-1)
#define AUDIO_VOLUME_MAX   (0x3FFF)
#define AUDIO_NOTE_ROOT    (60) // MIDI middle C: every instrument's natural pitch. +12 is an octave up.

//
// The bank, one line per instrument: X(NAME, RELEASE). NAME is the enum
// entry, what an MML song writes after @, and the file on the disc
// (\AUDIO\NAME.VAG). RELEASE is how long the note rings after key-off, one
// of SHORT / MID / LONG (see the envelope comment in audio.c).
//
// A new instrument is a .wav at C4, one srg_audio_sample line in
// cmake/audio.cmake, one <file> in iso.xml and one line here.
//
#define AUDIO_INSTRUMENTS \
  X(PIANO, MID)           \
  X(BASS,  SHORT)         \
  X(LEAD,  SHORT)         \
  X(KICK,  LONG)          \
  X(SNARE, LONG)          \
  X(HAT,   LONG)          \
  X(BLIP,  LONG)

enum Audio_Instrument {
#define X(name, release) AUDIO_INSTRUMENT_##name,
  AUDIO_INSTRUMENTS
#undef X
  AUDIO_INSTRUMENT_COUNT,
};

typedef int Audio_Song; // handle from audio_song_load(), AUDIO_SONG_INVALID on failure

extern const char* const g_audio_instrument_names[AUDIO_INSTRUMENT_COUNT]; // "PIANO", "BASS"...

void audio_initialize(void);
void audio_update(void);

Audio_Song audio_song_load(const char* cd_path);

int  audio_sfx_play(int instrument, int note, int16_t vol_l, int16_t vol_r); // returns the voice, AUDIO_VOICE_NONE on failure
void audio_sfx_stop(int voice);

void audio_music_play(Audio_Song song, int loop);
void audio_music_stop(void);
void audio_music_fade_in(int frames);
void audio_music_fade_out(int frames);
void audio_music_set_volume(int16_t vol_l, int16_t vol_r);
int  audio_music_is_playing(void);

#endif
