#ifndef AUDIO_H
#define AUDIO_H

//
// The game-facing audio layer. Two things a game wants:
//
//   SOUND EFFECTS  short samples loaded whole into the SPU's sound RAM and
//                  fired from any of 22 voices, as many at once as you like.
//
//   MUSIC          one song at a time, streamed off the CD in small reads
//                  into a ring buffer, so a three minute track costs 96 KB
//                  of main RAM instead of three megabytes.
//
// Everything is polled. Call audio_update() once per frame; it is the only
// thing that talks to the CD drive for music, and it only does so when the
// drive is idle and the ring is running low. Game code never sees a voice
// register or a sector.
//
// Assets come from res/audio/*.wav|mp3 via the srg_audio_* rules in
// CMakeLists.txt, which run psxavenc at build time. See engine-docs/audio.md.
//
// NOTE:
// - audio_initialize() must run after cd_start().
// - A sound effect file is loaded through a 64 KB staging buffer, so a single
//   effect is capped at 64 KB (about 5 seconds at 22.05 kHz). Longer sounds
//   are music.
// - While music plays, a blocking CD load (cd_file_read_sync_uncached) is
//   fine as long as it finishes before the ring drains: about 3.8 seconds,
//   roughly 1 MB at 2x. Bigger than that: audio_music_stop() first.
// - audio_sfx_unload_all() throws away every loaded effect. Scenes load
//   their own set on entry; nothing persists across scenes.
//

#include "common.h"

#define AUDIO_SFX_MAX            (64)
#define AUDIO_SFX_FILE_MAX_BYTES (64 * 1024)
#define AUDIO_SFX_INVALID        (-1)
#define AUDIO_VOICE_NONE         (-1)
#define AUDIO_VOLUME_MAX         (0x3FFF)
#define AUDIO_PITCH_NORMAL       (0x1000) // 12-bit fixed point: 0x800 = one octave down, 0x2000 = one up

typedef int Audio_Sfx; // handle from audio_sfx_load(), AUDIO_SFX_INVALID on failure

void audio_initialize(void);
void audio_update(void);

Audio_Sfx audio_sfx_load(const char* cd_path);
void      audio_sfx_unload_all(void);
int       audio_sfx_play(Audio_Sfx sfx);
int       audio_sfx_play_ex(Audio_Sfx sfx, int16_t vol_l, int16_t vol_r, int pitch);
void      audio_sfx_stop(int voice);

int  audio_music_play(const char* cd_path, int loop);
void audio_music_stop(void);
void audio_music_fade_out(int frames);
void audio_music_set_volume(int16_t vol_l, int16_t vol_r);
int  audio_music_is_playing(void);

//
// For debug overlays: how much of the ring is full and how many times the
// stream has run dry since it started.
//
size_t audio_music_buffered_bytes(void);
size_t audio_music_ring_bytes(void);
int    audio_music_underruns(void);

#endif
