#ifndef SPU_H
#define SPU_H

//
// The low level sound chip (SPU) driver. Everything in here pokes hardware
// registers directly. Game code never includes this; it goes through audio.h,
// the same way input_action.h sits on top of input_pad.h.
//
// Two jobs:
//
//   1. upload ADPCM sample data into the SPU's own 512 KB of sound RAM
//   2. key voices on and off with a start address, pitch, volume and envelope
//
// That is the whole driver. Every sound the game makes is a sample that was
// uploaded once at boot and is played back at some pitch; the sequencer that
// decides which note goes where lives in audio.c.
//
// NOTE:
// - cd_start() must run before spu_initialize(). Sony's docs say SPU transfer
//   completion can misfire if the SPU is reset before the CD controller.
// - Data handed to spu_upload() must not live on the stack and must be padded
//   to 64 bytes: the SPU DMA reads it straight out of main RAM in 64-byte
//   units and will happily read past a short buffer.
//

#include "common.h"
#include <psxspu.h>

#define SPU_VOICE_COUNT  (24)
#define SPU_RAM_SIZE     (512 * 1024)
#define SPU_VOLUME_MAX   (0x3FFF)
#define SPU_PITCH_NORMAL (0x1000) // SPU_CH_FREQ units: 0x1000 = 44.1 kHz, 0x800 = 22.05 kHz
#define SPU_PITCH_MAX    (0x3FFF) // the register is 14 bits: just under 4x, two octaves up from 44.1 kHz

//
// Sound RAM map. The SPU has its own 512 KB that the CPU cannot address
// directly; everything gets there by DMA. We carve it up once, statically:
//
//   0x00000 +-----------------------------+
//           | capture buffers (hardware)  |  the SPU writes CD/voice taps here
//   0x01000 +-----------------------------+
//           | silent dummy block, 16 B    |  uploaded by SpuInit(); a keyed-off
//   0x01010 +-----------------------------+  voice is still reading *somewhere*
//           | (gap, 64-byte alignment)    |
//   0x01040 +-----------------------------+
//           | sample bank                 |  every instrument, uploaded once at
//           |   ...grows upward...        |  boot by audio_initialize(), packed
//           |                             |  end to end; never freed
//   0x7FFF0 +-----------------------------+
//           | reverb work area            |  SpuInit() parks it here, unused
//   0x80000 +-----------------------------+
//
// Which voice does what is audio.c's business, not the driver's.
//
#define SPU_DUMMY_BLOCK_ADDR (0x1000)
#define SPU_SAMPLE_BANK_ADDR (0x1040)
#define SPU_SAMPLE_BANK_END  (0x7FFF0)

void spu_initialize(void);
void spu_upload(uint32_t spu_addr, const void* data, size_t size);

void spu_voice_play(int voice, uint32_t spu_addr, uint16_t freq, int16_t vol_l, int16_t vol_r, uint16_t adsr1, uint16_t adsr2);
void spu_voice_stop(int voice);
void spu_voice_set_volume(int voice, int16_t vol_l, int16_t vol_r);

#endif
