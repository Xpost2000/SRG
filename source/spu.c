#include "spu.h"
#include <psxetc.h>
#include <psxapi.h>
#include <hwregs_c.h>

//
// SPU_CTRL bit 6 enables the SPU IRQ. There is no separate acknowledge
// register: the only way to clear a pending SPU interrupt is to drop this bit
// and raise it again, which is why the handlers below toggle it.
//
#define SPU_CTRL_IRQ_ENABLE (1 << 6)

//
// ADSR for pre-rendered samples: instant attack, no decay, hold at full level,
// instant release on key-off. The envelope is baked into the sample already.
//
//   ADSR1 0x00FF: attack shift 0 (instant), decay shift 0xF, sustain level 0xF
//   ADSR2 0x0000: sustain holds, release shift 0 (instant)
//
#define SPU_ADSR1_INSTANT_FULL (0x00FF)
#define SPU_ADSR2_INSTANT_FULL (0x0000)

// ----------------------------------------------------------------------------
//  Stream state
// ----------------------------------------------------------------------------

typedef struct Spu_Stream Spu_Stream;

struct Spu_Stream {
  volatile int    active;      // voices are playing out of the double buffer
  volatile int    end_pending; // feeder has delivered the last byte of the song
  int             channels;
  size_t          chunk_size;  // channels * SPU_STREAM_INTERLEAVE
  uint32_t        voice_mask;
  uint16_t        freq;
  int16_t         vol_l;
  int16_t         vol_r;

  volatile size_t head;        // ring: where the feeder writes next
  volatile size_t tail;        // ring: where the IRQ reads next
  volatile size_t length;      // ring: bytes waiting to be played
  volatile int    half;        // which half of the SPU double buffer was filled last
  volatile int    underruns;
};

static Spu_Stream g_stream;

//
// The DMA engine reads the ring directly, so it has to be word aligned.
//
static uint32_t g_stream_ring_words[SPU_STREAM_RING_SIZE / 4];
#define g_stream_ring ((uint8_t*) g_stream_ring_words)

static void _voice_park(int voice)
{
  SPU_CH_ADDR(voice) = getSPUAddr(SPU_DUMMY_BLOCK_ADDR);
  SPU_CH_FREQ(voice) = SPU_PITCH_NORMAL;
}

// ----------------------------------------------------------------------------
//  Interrupt handlers
// ----------------------------------------------------------------------------

//
// How the double buffer works. Each stream voice plays a 4 KB slice whose last
// ADPCM block carries the "loop" flag. A looping voice jumps to whatever its
// LOOP_ADDR register says, and we are allowed to rewrite that register while
// the voice plays. So:
//
//   voice plays half A ---> hits loop flag ---> jumps to half B
//                                                   |
//                 SPU IRQ fires because half B first byte == SPU_IRQ_ADDR
//                                                   |
//                 handler: pull next chunk from ring, DMA it into half A,
//                          point LOOP_ADDR and SPU_IRQ_ADDR at half A
//
// The voice never knows it is streaming; it thinks it is looping a sample
// that happens to change under its feet. One IRQ every chunk (about 325 ms at
// 22.05 kHz), which is nothing.
//
// Gotcha from psx-spx: every voice reads SPU RAM all the time, even silent
// ones, and a DMA write that crosses SPU_IRQ_ADDR triggers the IRQ too. That
// is why idle voices sit on the dummy block and why the IRQ is disabled for
// the duration of the chunk upload.
//
static void _stream_irq_handler(void)
{
  SPU_CTRL &= ~SPU_CTRL_IRQ_ENABLE;

  if (!g_stream.active) {
    return;
  }

  int remaining = (int) g_stream.length - (int) g_stream.chunk_size;

  if (remaining < 0) {
    if (g_stream.end_pending) {
      //
      // The chunk that just started playing is the last one. Let it play out,
      // then land the voices on the silent block instead of looping a half.
      //
      for (int ch = 0; ch < g_stream.channels; ++ch) {
        SPU_CH_LOOP_ADDR(SPU_VOICE_STREAM_FIRST + ch) = getSPUAddr(SPU_DUMMY_BLOCK_ADDR);
      }
      g_stream.active = 0;
      return;
    }

    //
    // Underrun: the CD did not keep up. Re-arm the IRQ so the voices keep
    // looping the stale half (audible repeat) rather than hanging forever.
    //
    g_stream.underruns++;
    SPU_CTRL |= SPU_CTRL_IRQ_ENABLE;
    return;
  }

  g_stream.half ^= 1;

  uint8_t* chunk  = &g_stream_ring[g_stream.tail];
  g_stream.tail   = (g_stream.tail + g_stream.chunk_size) % SPU_STREAM_RING_SIZE;
  g_stream.length = remaining;

  uint32_t address = SPU_STREAM_BUFFER_ADDR + (g_stream.half ? g_stream.chunk_size : 0);

  SPU_IRQ_ADDR = getSPUAddr(address);
  for (int ch = 0; ch < g_stream.channels; ++ch) {
    SPU_CH_LOOP_ADDR(SPU_VOICE_STREAM_FIRST + ch) = getSPUAddr(address + ch * SPU_STREAM_INTERLEAVE);
  }

  SpuSetTransferStartAddr(address);
  SpuWrite((const uint32_t*) chunk, g_stream.chunk_size);
}

//
// Fires when any SPU DMA finishes, including sound effect uploads. The IRQ is
// only re-armed while a stream is live; otherwise a parked voice could never
// trigger it anyway and we would rather leave it off.
//
static void _stream_dma_handler(void)
{
  if (g_stream.active) {
    SPU_CTRL |= SPU_CTRL_IRQ_ENABLE;
  }
}

// ----------------------------------------------------------------------------
//  Init and uploads
// ----------------------------------------------------------------------------

void spu_initialize(void)
{
  //
  // SpuInit() resets the chip, uploads the silent dummy block at 0x1000 and
  // keys every voice onto it, and turns the master volume up. The IRQ enable
  // bit comes up clear.
  //
  SpuInit();
  SpuSetTransferMode(SPU_TRANSFER_BY_DMA);

  int was_enabled = EnterCriticalSection();
  InterruptCallback(IRQ_SPU, _stream_irq_handler);
  DMACallback(DMA_SPU, _stream_dma_handler);
  if (was_enabled) {
    ExitCriticalSection();
  }

  memory_zero(&g_stream, sizeof(g_stream));
  _debugprintf("[SPU] ready, %d voices, bank 0x%05x-0x%05x", SPU_VOICE_COUNT, SPU_SAMPLE_BANK_ADDR, SPU_SAMPLE_BANK_END);
}

//
// Blocking upload. Runs inside a critical section because the stream IRQ
// handler uses the same DMA channel: if it fired mid-upload the two transfers
// would stomp each other's SPU address register. Masking interrupts does not
// lose the IRQ, it only delays it (the SPU latches it in I_STAT), and a chunk
// is about 325 ms long while a 64 KB upload takes a few ms, so the stream
// never notices.
//
void spu_upload(uint32_t spu_addr, const void* data, size_t size)
{
  assert(spu_addr >= SPU_STREAM_BUFFER_ADDR && (spu_addr + size) <= SPU_RAM_SIZE && "[SPU] upload outside sound RAM.");
  assert((((uintptr_t) data) & 3) == 0 && "[SPU] upload source must be word aligned for DMA.");

  size = (size + 63) & ~((size_t) 63);

  int was_enabled = EnterCriticalSection();
  SpuIsTransferCompleted(SPU_TRANSFER_WAIT); // a stream chunk may still be in flight
  SpuSetTransferStartAddr(spu_addr);
  SpuWrite((const uint32_t*) data, size);
  SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
  if (was_enabled) {
    ExitCriticalSection();
  }
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
void spu_voice_play(int voice, uint32_t spu_addr, uint16_t freq, int16_t vol_l, int16_t vol_r)
{
  assert(voice >= 0 && voice < SPU_VOICE_COUNT && "[SPU] bad voice index.");

  SPU_CH_ADDR(voice)  = getSPUAddr(spu_addr);
  SPU_CH_FREQ(voice)  = freq;
  SPU_CH_VOL_L(voice) = vol_l;
  SPU_CH_VOL_R(voice) = vol_r;
  SPU_CH_ADSR1(voice) = SPU_ADSR1_INSTANT_FULL;
  SPU_CH_ADSR2(voice) = SPU_ADSR2_INSTANT_FULL;
  SpuSetKey(1, 1u << voice);
}

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

// ----------------------------------------------------------------------------
//  Stream
// ----------------------------------------------------------------------------

static void _stream_apply_volume(void)
{
  if (g_stream.channels == 1) {
    spu_voice_set_volume(SPU_VOICE_STREAM_FIRST, g_stream.vol_l, g_stream.vol_r);
  } else {
    // one voice per side: left voice only to the left speaker, right only to the right
    spu_voice_set_volume(SPU_VOICE_STREAM_FIRST,     g_stream.vol_l, 0);
    spu_voice_set_volume(SPU_VOICE_STREAM_FIRST + 1, 0,              g_stream.vol_r);
  }
}

int spu_stream_start(int channels, int sample_rate, int16_t vol_l, int16_t vol_r)
{
  assert(channels >= 1 && channels <= SPU_STREAM_CHANNELS_MAX && "[SPU] stream channel count out of range.");
  assert(!g_stream.active && "[SPU] stream already running; stop it first.");

  g_stream.channels    = channels;
  g_stream.chunk_size  = channels * SPU_STREAM_INTERLEAVE;
  g_stream.voice_mask  = ((1u << channels) - 1) << SPU_VOICE_STREAM_FIRST;
  g_stream.freq        = getSPUSampleRate(sample_rate);
  g_stream.vol_l       = vol_l;
  g_stream.vol_r       = vol_r;
  g_stream.half        = 0;
  g_stream.end_pending = 0;
  g_stream.underruns   = 0;

  assert((SPU_STREAM_RING_SIZE % g_stream.chunk_size) == 0 && "[SPU] ring must be a multiple of the chunk size.");

  if (g_stream.length < 2 * g_stream.chunk_size) {
    _debugprintf("[SPU] stream start needs two chunks buffered, have %d bytes", g_stream.length);
    return 0;
  }

  g_stream.active = 1;

  //
  // Prime: the handler flips to half 1 and uploads chunk 0 there. Voices are
  // pointed at half 1, then the handler is called again so chunk 1 lands in
  // half 0 with the IRQ and loop addresses aimed at it. Key on and the dance
  // in the handler comment takes over.
  //
  _stream_irq_handler();
  SpuIsTransferCompleted(SPU_TRANSFER_WAIT);

  uint32_t address = SPU_STREAM_BUFFER_ADDR + g_stream.chunk_size;

  SpuSetKey(0, g_stream.voice_mask);
  for (int ch = 0; ch < channels; ++ch) {
    int voice = SPU_VOICE_STREAM_FIRST + ch;
    SPU_CH_ADDR(voice)  = getSPUAddr(address + ch * SPU_STREAM_INTERLEAVE);
    SPU_CH_FREQ(voice)  = g_stream.freq;
    SPU_CH_ADSR1(voice) = SPU_ADSR1_INSTANT_FULL;
    SPU_CH_ADSR2(voice) = SPU_ADSR2_INSTANT_FULL;
  }
  _stream_apply_volume();

  _stream_irq_handler();
  SpuSetKey(1, g_stream.voice_mask);

  return 1;
}

void spu_stream_stop(void)
{
  g_stream.active = 0;
  SPU_CTRL &= ~SPU_CTRL_IRQ_ENABLE;

  //
  // Park the stream voices on silence. A keyed-off voice keeps reading RAM,
  // so leaving it in the double buffer could trip the IRQ next time around.
  //
  SpuSetKey(0, g_stream.voice_mask);
  for (int ch = 0; ch < g_stream.channels; ++ch) {
    _voice_park(SPU_VOICE_STREAM_FIRST + ch);
  }
  SpuSetKey(1, g_stream.voice_mask);

  SpuIsTransferCompleted(SPU_TRANSFER_WAIT); // let an in-flight chunk DMA finish before the ring is reused

  g_stream.head        = 0;
  g_stream.tail        = 0;
  g_stream.length      = 0;
  g_stream.end_pending = 0;
}

void spu_stream_set_end(void)
{
  g_stream.end_pending = 1;
}

int spu_stream_is_active(void)
{
  return g_stream.active;
}

void spu_stream_set_volume(int16_t vol_l, int16_t vol_r)
{
  g_stream.vol_l = vol_l;
  g_stream.vol_r = vol_r;
  if (g_stream.active) {
    _stream_apply_volume();
  }
}

//
// Ring bookkeeping. head/length are shared with the IRQ handler, so reads and
// updates happen with interrupts masked. The feeder only ever gets the
// contiguous run up to the end of the array; the wrap-around part comes on the
// next call.
//
size_t spu_stream_feed_ptr(uint8_t** ptr)
{
  size_t head;
  size_t length;

  FastEnterCriticalSection();
  head   = g_stream.head;
  length = g_stream.length;
  FastExitCriticalSection();

  size_t free_total  = SPU_STREAM_RING_SIZE - length;
  size_t free_to_end = SPU_STREAM_RING_SIZE - head;

  if (free_total == 0) {
    return 0;
  }

  *ptr = &g_stream_ring[head];
  return min(free_total, free_to_end);
}

void spu_stream_feed(size_t length)
{
  FastEnterCriticalSection();
  g_stream.head   = (g_stream.head + length) % SPU_STREAM_RING_SIZE;
  g_stream.length = g_stream.length + length;
  FastExitCriticalSection();
}

size_t spu_stream_free_bytes(void)
{
  return SPU_STREAM_RING_SIZE - g_stream.length;
}

size_t spu_stream_buffered_bytes(void)
{
  return g_stream.length;
}

int spu_stream_underruns(void)
{
  return g_stream.underruns;
}
