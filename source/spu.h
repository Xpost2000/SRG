#ifndef SPU_H
#define SPU_H

//
// The low level sound chip (SPU) driver. Everything in here pokes hardware
// registers directly. Game code never includes this; it goes through audio.h,
// the same way input_action.h sits on top of input_pad.h.
//
// Three jobs:
//
//   1. upload ADPCM sample data into the SPU's own 512 KB of sound RAM
//   2. key voices on and off with a start address, pitch and volume
//   3. the stream driver: an SPU-IRQ driven double buffer that lets a song of
//      any length play out of a small ring buffer in main RAM
//
// NOTE:
// - cd_start() must run before spu_initialize(). Sony's docs say SPU transfer
//   completion can misfire if the SPU is reset before the CD controller.
// - Data handed to spu_upload() must not live on the stack and must be padded
//   to 64 bytes: the SPU DMA reads it straight out of main RAM in 64-byte
//   units and will happily read past a short buffer.
// - The ring buffer and the IRQ handler live here (not in audio.c) because the
//   handler is the thing draining it, and the handler is hardware.
//

#include "common.h"
#include <psxspu.h>

#define SPU_VOICE_COUNT  (24)
#define SPU_RAM_SIZE     (512 * 1024)
#define SPU_VOLUME_MAX   (0x3FFF)
#define SPU_PITCH_NORMAL (0x1000) // SPU_CH_FREQ units: 0x1000 = 44.1 kHz, 0x800 = 22.05 kHz

//
// Sound RAM map. The SPU has its own 512 KB that the CPU cannot address
// directly; everything gets there by DMA. We carve it up once, statically:
//
//   0x00000 +-----------------------------+
//           | capture buffers (hardware)  |  the SPU writes CD/voice taps here
//   0x01000 +-----------------------------+
//           | silent dummy block, 16 B    |  uploaded by SpuInit(); idle voices
//   0x01010 +-----------------------------+  loop on it so they never trigger
//           | stream double buffer        |  the SPU IRQ by accident
//           |   half 0: [L 4 KB][R 4 KB]  |
//           |   half 1: [L 4 KB][R 4 KB]  |  16 KB total (see spu.c)
//   0x05010 +-----------------------------+
//           | (gap, 64-byte alignment)    |
//   0x05100 +-----------------------------+
//           | sample bank                 |  sound effects, bump-allocated by
//           |   ...grows upward...        |  audio.c, reset per scene
//   0x7FFF0 +-----------------------------+
//           | reverb work area            |  SpuInit() parks it here, unused
//   0x80000 +-----------------------------+
//
// Voices: 0 and 1 belong to the stream (left, right). 2..23 are the sound
// effect pool. Nothing in the pool may ever be pointed into the stream region
// or it would trip the stream IRQ.
//
#define SPU_DUMMY_BLOCK_ADDR    (0x1000)
#define SPU_STREAM_BUFFER_ADDR  (0x1010)
#define SPU_STREAM_INTERLEAVE   (4096) // bytes per channel per chunk; psxavenc -i 4096
#define SPU_STREAM_CHANNELS_MAX (2)
#define SPU_STREAM_CHUNK_MAX    (SPU_STREAM_INTERLEAVE * SPU_STREAM_CHANNELS_MAX)
#define SPU_STREAM_BUFFER_SIZE  (SPU_STREAM_CHUNK_MAX * 2)
#define SPU_SAMPLE_BANK_ADDR    (0x5100)
#define SPU_SAMPLE_BANK_END     (0x7FFF0)

#define SPU_VOICE_STREAM_FIRST  (0)
#define SPU_VOICE_SFX_FIRST     (2)

//
// Main RAM ring buffer the stream plays out of. 96 KB is about 3.8 seconds
// of 22.05 kHz stereo: that is how long a blocking CD load may take while
// music is playing before the song stutters. Must be a multiple of the chunk
// size so a chunk never straddles the wrap.
//
#define SPU_STREAM_RING_SIZE (0x18000)

void spu_initialize(void);
void spu_upload(uint32_t spu_addr, const void* data, size_t size);

void spu_voice_play(int voice, uint32_t spu_addr, uint16_t freq, int16_t vol_l, int16_t vol_r);
void spu_voice_stop(int voice);
void spu_voice_set_volume(int voice, int16_t vol_l, int16_t vol_r);

//
// Stream driver. The caller (audio.c) owns the CD side: it asks for a pointer
// into the ring with spu_stream_feed_ptr(), DMAs CD sectors straight into it,
// then calls spu_stream_feed() to say how many bytes landed. The IRQ handler
// pulls one chunk at a time out of the other end.
//
// spu_stream_start() needs at least two chunks already fed. Once the caller
// has fed the last bytes of a song it calls spu_stream_set_end() and the
// driver parks the voices on silence after the final chunk plays out.
//
int      spu_stream_start(int channels, int sample_rate, int16_t vol_l, int16_t vol_r);
void     spu_stream_stop(void);
void     spu_stream_set_end(void);
int      spu_stream_is_active(void);
void     spu_stream_set_volume(int16_t vol_l, int16_t vol_r);
size_t   spu_stream_feed_ptr(uint8_t** ptr);
void     spu_stream_feed(size_t length);
size_t   spu_stream_free_bytes(void);
size_t   spu_stream_buffered_bytes(void);
int      spu_stream_underruns(void);

#endif
