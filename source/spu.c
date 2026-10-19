#include "spu.h"
#include <hwregs_c.h>

// ----------------------------------------------------------------------------
//  Init and uploads
// ----------------------------------------------------------------------------

void spu_initialize(void)
{
  //
  // SpuInit() resets the chip, uploads the silent dummy block at 0x1000 and
  // keys every voice onto it, and turns the master volume up.
  //
  SpuInit();
  SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
  _debugprintf("[SPU] ready, %d voices, bank 0x%05x-0x%05x", SPU_VOICE_COUNT, SPU_SAMPLE_BANK_ADDR, SPU_SAMPLE_BANK_END);
}

//
// Blocking upload. Only ever called at boot, so there is nothing else on the
// SPU DMA channel to race with.
//
void spu_upload(uint32_t spu_addr, const void* data, size_t size)
{
  assert(spu_addr >= SPU_SAMPLE_BANK_ADDR && (spu_addr + size) <= SPU_RAM_SIZE && "[SPU] upload outside sound RAM.");
  assert((((uintptr_t) data) & 3) == 0 && "[SPU] upload source must be word aligned for DMA.");

  size = align_up(size, (size_t) 64);

  SpuSetTransferStartAddr(spu_addr);
  SpuWrite((const uint32_t*) data, size);
  SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
}

// ----------------------------------------------------------------------------
//  Voices
// ----------------------------------------------------------------------------

//
// Key-on alone restarts a voice from its start address and resets the
// envelope; keying off first is not needed and psx-spx warns the SPU only
// latches register writes once per 44.1 kHz tick, so off-then-on in the same
// tick is unreliable.
//
// The envelope (ADSR) is the instrument's: see the bank table in audio.c for
// what the two register values mean.
//
void spu_voice_play(int voice, uint32_t spu_addr, uint16_t freq, int16_t vol_l, int16_t vol_r, uint16_t adsr1, uint16_t adsr2)
{
  assert(voice >= 0 && voice < SPU_VOICE_COUNT && "[SPU] bad voice index.");

  SPU_CH_ADDR(voice)  = getSPUAddr(spu_addr);
  SPU_CH_FREQ(voice)  = freq;
  SPU_CH_VOL_L(voice) = vol_l;
  SPU_CH_VOL_R(voice) = vol_r;
  SPU_CH_ADSR1(voice) = adsr1;
  SPU_CH_ADSR2(voice) = adsr2;
  SpuSetKey(1, 1u << voice);
}

//
// Key-off does not silence the voice, it starts the envelope's release phase.
// How long the note rings on after this is the instrument's release rate.
//
void spu_voice_stop(int voice)
{
  assert(voice >= 0 && voice < SPU_VOICE_COUNT && "[SPU] bad voice index.");
  SpuSetKey(0, 1u << voice);
}

void spu_voice_set_volume(int voice, int16_t vol_l, int16_t vol_r)
{
  assert(voice >= 0 && voice < SPU_VOICE_COUNT && "[SPU] bad voice index.");
  SPU_CH_VOL_L(voice) = vol_l;
  SPU_CH_VOL_R(voice) = vol_r;
}
