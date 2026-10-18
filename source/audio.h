#ifndef AUDIO_H
#define AUDIO_H

//
// The game-facing audio layer. Everything is resident: audio_initialize()
// uploads a small instrument bank into the SPU's sound RAM, audio_song_load()
// parses a song's text into a few KB of note events, and from then on the
// disc is never touched for sound again.
//
//   INSTRUMENTS    one short sample each (piano, bass, drums, a blip...),
//                  played at any pitch. Firing one directly is a sound
//                  effect: audio_note_play().
//
//   SONGS          a text file of notes (MML, see engine-docs/audio.md),
//                  up to 8 tracks. Played as music (voices 0-7, one at a
//                  time, can loop and fade) or as a jingle sound effect
//                  (borrowed pool voices, several at once).
//
// Everything is polled. Call audio_update() once per frame; it is the
// sequencer, and it is what keys notes on and off.
//
// Instrument samples come from res/audio/*.wav via the srg_audio_sample rules
// in CMakeLists.txt; songs are res/audio/*.mml, copied to the disc as is.
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
// Same order as g_bank in audio.c. MML songs name these as @PIANO, @BASS...
//
enum Audio_Instrument {
  AUDIO_INSTRUMENT_PIANO,
  AUDIO_INSTRUMENT_BASS,
  AUDIO_INSTRUMENT_LEAD,
  AUDIO_INSTRUMENT_KICK,
  AUDIO_INSTRUMENT_SNARE,
  AUDIO_INSTRUMENT_HAT,
  AUDIO_INSTRUMENT_BLIP,
  AUDIO_INSTRUMENT_COUNT,
};

typedef int Audio_Song; // handle from audio_song_load(), AUDIO_SONG_INVALID on failure

void audio_initialize(void);
void audio_update(void);

Audio_Song audio_song_load(const char* cd_path);

int  audio_note_play(int instrument, int note, int16_t vol_l, int16_t vol_r);
void audio_note_stop(int voice);
void audio_sfx_play(Audio_Song jingle);

void audio_music_play(Audio_Song song, int loop);
void audio_music_stop(void);
void audio_music_fade_out(int frames);
void audio_music_set_volume(int16_t vol_l, int16_t vol_r);
int  audio_music_is_playing(void);

#endif
