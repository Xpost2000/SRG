# Audio Research: how the PS1 makes sound and why we built it this way

This is the learning doc behind [`audio.md`](audio.md). It records what we found out about the
hardware, how commercial PS1 games handled music and effects, the sizes involved, the options we
weighed, and where every fact came from. Read `audio.md` to *use* the system; read this to
understand it or to revisit a decision.

## 1. How the PS1 makes sound, in plain terms

The PlayStation has a dedicated sound chip, the **SPU**. Think of it as a 24-track sample
player with its own private memory:

- **24 voices.** Each voice independently plays one sample at its own pitch and volume. All 24
  can sound at once; the chip mixes them. There is no "channel" concept beyond this: a voice is
  a channel.
- **512 KB of sound RAM.** The SPU cannot read main RAM. Every sample must be copied into sound
  RAM first, by DMA, in 64-byte units. Main RAM is 2 MB, so sound RAM is a quarter of that and
  it is the tighter budget.
- **Samples are ADPCM.** 16-byte blocks hold 28 samples each (4.57 bits per sample, about 3.5
  times smaller than 16-bit PCM). The decoder is in hardware; the CPU never touches sample
  data. The second byte of each block is a flag: `0x04` marks a loop start, `0x03` "end of loop,
  jump back", `0x01` "end, go silent". A voice is always reading *something*, so a one-shot
  sample is followed by a silent block that loops on itself.
- **Pitch is a 12-bit fixed point multiplier.** `0x1000` means 44.1 kHz. A sample recorded at
  22.05 kHz plays correctly at `0x800`, and scaling that value is free pitch shifting.
- **ADSR envelope.** Every voice has a hardware attack, decay, sustain, release envelope. For
  samples that already contain their own shape we set instant attack, full sustain, instant
  release and forget about it.
- **A rewritable loop address.** Each voice has a register saying where to jump when it hits a
  loop-end flag, and that register can be changed while the voice is playing.
- **An interrupt on address.** The SPU can raise an interrupt when any voice reads a chosen
  address. Any voice, including silent ones, which is why idle voices are parked on a silent
  block well away from that address.
- **CD audio input.** The CD controller's own audio output (CD-DA and XA, below) is mixed into
  the SPU's output digitally. PSn00bSDK enables it at full volume in `SpuInit()`.

### The sound RAM map we use

```
0x00000 +-----------------------------+
        | capture buffers (hardware)  |  the SPU writes taps of its own output here
0x01000 +-----------------------------+
        | silent dummy block, 16 B    |  uploaded by SpuInit(); idle voices loop on it
0x01010 +-----------------------------+
        | stream double buffer        |  half 0: [L 4 KB][R 4 KB]
        |                             |  half 1: [L 4 KB][R 4 KB]     16 KB
0x05100 +-----------------------------+
        | sample bank                 |  sound effects, bump-allocated per scene
        |   ... ~490 KB ...           |
0x7FFF0 +-----------------------------+
        | reverb work area            |  parked here by SpuInit(), unused
0x80000 +-----------------------------+
```

### The trick that makes streaming possible

Put the loop address and the interrupt together and a voice can play a song of any length out
of a buffer the size of two chunks:

```
   voice plays half A ---> hits loop flag at the end of A ---> jumps to half B
                                                                     |
          SPU interrupt fires because the first byte of B == SPU_IRQ_ADDR
                                                                     |
          handler: pull the next chunk from the ring in main RAM, DMA it into A,
                   point the loop address and SPU_IRQ_ADDR at A
```

The voice thinks it is looping a sample that keeps changing under it. The only costs are one
interrupt per chunk (325 ms at 22 kHz) and the ring buffer in main RAM. Sony's own `SpuSt*`
library did this, and so does PSn00bSDK's `cdstream` example, which `spu.c` is a port of.

## 2. How the CD drive delivers audio

The disc holds 2048-byte data sectors read at 150 per second at 2x speed, 300 KB/s. There is
exactly one pickup head, and it is either reading your song or reading your level, never both.
That constraint drives everything about music on this machine. Three ways audio can come off
the disc:

- **CD-DA ("Red Book").** The drive plays a raw 44.1 kHz stereo track itself, like an audio CD.
  Zero CPU, zero sound RAM, but 10 MB per minute and the head is fully occupied.
- **CD-XA.** A different sector format (Mode 2 Form 2) that the CD controller decodes in
  hardware and feeds straight to the SPU mixer, bypassing the CPU. Up to 8 stereo 37.8 kHz
  songs are interleaved in one file, one sector in eight each, and `CdlSetfilter` picks which
  one plays. The filter can be switched mid-stream with no gap. Needs `CdlModeRT|CdlModeSF`
  and `CdlReadS`; `CdlPause` stops it. No hardware loop: you seek back to the start, audibly.
  Any data you want loaded during the song has to be interleaved into the same file when the
  disc is authored. XA sectors have no error correction, so worn drives skip.
- **Plain data reads.** `CdRead` into main RAM, which is how `cdfs.c` loads everything and how
  music reaches the ring buffer.

## 3. The four ways PS1 games did music

| Approach | Games | Pros | Cons |
| --- | --- | --- | --- |
| CD-DA tracks | Ridge Racer, Wipeout, Ridge Racer Type 4 | Best quality, no CPU or SPU cost, players can play the disc in a CD player | 10 MB/min, **nothing else can load during music**, speed switch between 1x play and 2x read, seek gap between tracks |
| XA streaming | Silent Hill, Tomb Raider 3 onward, Mega Man Legends, every FMV (FF7, MGS) | 4:1 compression, no voices or sound RAM, 8 seamlessly switchable layers | 37.8 kHz only, gap on loop, drive locked to the stream, skips on worn drives, needs an interleaver tool |
| Sequenced (SEQ/VAB, Square's AKAO) | FF7 and FF8 background music, Crash Bandicoot, most Konami and Capcom titles | Tens of KB per song, CD completely free, perfect loops and tempo changes | Instrument bank takes 100 to 300 KB of sound RAM and most of the 24 voices, needs a sequencer and a composer pipeline, PSn00bSDK ships no player |
| SPU streaming (ring + interrupt) | Dance Dance Revolution 3rdMix onward, Metal Gear Solid and Tron Bonne dialogue, PSn00bSDK `cdstream` | Any sample rate (22 kHz halves the size), 2 voices and 16 KB sound RAM, **seamless loop**, crossfades, drive idle about 85% of the time so loads fit between refills, software error correction | 96 KB main RAM ring, one interrupt handler, load sequences must yield to the feeder |

### XA versus SPU streaming, the plain version

The difference is **who decodes the audio**.

With XA, the CD drive plays the music. The controller recognises each audio sector and pipes
the decoded sound into the mixer; the CPU never sees it and the SPU never stores it. It is a
record player: drop the needle and sound comes out, but the needle is now busy and cannot go
and read anything else on the disc. Looping means lifting the needle and setting it down at the
start again, with a small silence.

With SPU streaming, the CPU hands the music to the sound chip in slices. The song is just a
data file; every so often the game reads a few sectors into a ring buffer in main RAM, like any
other load. The SPU plays a tiny two-slice buffer and interrupts when it finishes a slice so the
next one can be copied in. Because the music comes off the disc in short bursts, the drive is
idle most of the time, and in those gaps the game can read fonts, levels, anything. Looping is
free: when the feeder reaches the end of the file it starts reading from the beginning again,
and the sound chip never knows.

A restaurant analogy: XA is a conveyor belt from the kitchen to your table, with the only
waiter tied to it. SPU streaming is a waiter who brings a tray every few minutes and is free to
fetch other things in between, as long as the tray is big enough that you never run out before
the next one.

Where XA genuinely wins is layered, adaptive music: eight stems of one song in one file, and
flipping the channel filter switches between them with zero gap. If SRG ever wants that, XA can
be added beside the current system without touching the sound effect side.

## 4. Voices and channels

Music streaming takes 2 of the 24 voices (one per stereo side), leaving 22 for effects playing
at the same time as the song. Points worth knowing:

- **22 simultaneous effects is a lot.** Most commercial PS1 games mixed sequenced music and
  effects on the same 24 voices, with music alone often taking 12 to 16 of them.
- **A voice is only busy while its sound is audible.** A 0.3 second blip holds a voice for
  0.3 seconds. The pool is 22 sounds *overlapping at one instant*, not 22 sounds total.
- **The music voices are reserved**, so a burst of effects can never stutter the song.
- **Mono music would use 1 voice** if a 23rd effect slot or half the disc space ever mattered.
- **The shared budget is sound RAM, not voices.** The stream buffer takes 16 KB and the rest,
  about 490 KB, is the effect bank, roughly 40 seconds of 22 kHz effects. That bank is per
  scene, so each scene loads only what it needs.

## 5. Size budget

Bytes per second of SPU ADPCM is `rate / 28 * 16 * channels`.

| Format | Per minute | Notes |
| --- | --- | --- |
| WAV 44.1 kHz stereo 16-bit (source) | 10.1 MB | |
| MP3 128 kbps (source) | 0.96 MB | the PS1 cannot decode it |
| SPU ADPCM 44.1 kHz stereo | 2.95 MB | |
| **SPU ADPCM 22.05 kHz stereo** | **1.48 MB** | our music default |
| SPU ADPCM 22.05 kHz mono | 0.74 MB | |
| XA 37.8 kHz stereo | 2.6 MB per song, file holds 8 slots | empty slots still cost disc space |
| CD-DA | 10.1 MB | |

Effects at 22.05 kHz mono cost 12.6 KB per second. The placeholder song, 6 seconds of 22 kHz
stereo, is 529 KB as a WAV, 73 KB as an MP3 and 154 KB as streamed ADPCM.

The main RAM side: the ring is 96 KB (about 3.8 seconds of 22 kHz stereo) and the staging
buffer 64 KB, 160 KB of the 2 MB in total.

## 6. Decisions and why

- **SPU streaming over XA.** Seamless loops, half-size files, and the drive stays available
  for loading, which matters for a game that will keep loading things while a song plays.
  XA's advantages (zero voices, layered stems) are not things this game needs yet, and XA
  would also have needed a sector interleaver that `psxavenc` does not provide.
- **psxavenc over a custom encoder or a Python wrapper.** It already decodes anything ffmpeg
  can, resamples, reads WAV loop points, and emits exactly the interleaved layout the SDK's
  streaming example expects. It is a statically linked exe under the Zlib license, which fits
  the repo's "everything is vendored in `toolchain-win64/`, nothing to install" rule. A pure
  Python encoder would take minutes per song and add a dependency; a C encoder would need a
  host compiler this machine does not have.
- **CMake rules rather than a separate script.** `build.bat` already runs CMake, so one
  `add_custom_command` per asset converts on demand and rebuilds only when the source changes.
  No manifest file, no second tool to learn.
- **22.05 kHz default.** Half the disc of 44.1 kHz and inaudible through a PS1 into a TV for
  effects and most music. Per-asset override in the build rule.
- **A per-scene bump allocator for effects.** The reference `audio.c` we started from had a
  1024-slot table with an eviction pager that re-read evicted samples from the CD *inside
  play()*, kept a pointer to another module's cache, and wiped sound RAM while voices could
  still be reading it. A cursor that resets on scene change has none of those problems and is
  a few lines.
- **A polled feeder.** The engine rule is polling over callbacks. `audio_update()` issues one
  asynchronous read when the drive is idle and the ring is low; the read-complete callback is
  the only callback, and it is the SDK's, not ours. The SPU interrupt handler is hardware and
  lives in `spu.c`.
- **Reserved voices.** Round-robin over all 24 voices (what the reference did) would hand the
  stream's voices to a sound effect sooner or later.
- **The one-line `cdfs.c` change.** The CD read callback is a global hook; the module that
  issues a read is the right place to clear it. The alternative, an audio-side "lock the drive"
  API, would have made two modules know about each other.
- **Critical sections around uploads.** Verified against the SDK source: `SpuWrite` does not
  wait for a previous DMA and the completion wait is a register poll, so masking interrupts
  for the upload is both necessary and safe.

### What we dropped from the reference code

The ABCDEFG `audio.c` was PSn00bSDK's `vagsample` example plus a slot table. We kept its VAG
header struct, the `0x1010` start address and the upload sequence. We dropped the eviction
pager (blocking CD read inside play, dangling cache pointer, wipes RAM under live voices), the
53 KB slot table, the round-robin over all 24 voices, and the fixed full-volume centred playback
(no pitch, no pan).

## 7. What we would revisit

- **XA for layered or adaptive music.** Add a `psxavenc -t xa` rule, a small 2336-byte sector
  interleaver (about thirty lines), `type="xa"` in `iso.xml`, and an `audio_music_play_xa()`
  that sets the filter. Nothing in the current code prevents it.
- **Voice priority.** If effects get cut off, scan `SPU_CHAN_STATUS` (the ENDX bits) for a
  finished voice before round-robin, or add a priority byte per effect.
- **Reverb.** The SPU has hardware reverb with a work area at the top of sound RAM. We leave it
  off; turning it on costs 2 to 60 KB of sound RAM depending on the preset.
- **Mono music** for a 23rd effect voice and half the disc space.
- **Lower-rate effects.** 11.025 kHz is fine for many effects and halves their size again.

## 8. Resources

Local, in this repo:

- `toolchain-win64/include/libpsn00b/psxspu.h`, `psxcd.h`, `hwregs_c.h`: the only authority on
  what functions and registers exist. Note there is no `SpuSetIRQ`; the IRQ is hooked with
  `InterruptCallback(IRQ_SPU, ...)` and enabled with bit 6 of `SPU_CTRL`.
- `toolchain-win64/share/psn00bsdk/examples/sound/cdstream/`: the SDK's CD-to-SPU streaming
  example. `spu.c` is a static-buffer port of its `stream.c`; the header comment of its
  `main.c` makes the case for SPU streaming over XA and CD-DA.
- `toolchain-win64/share/psn00bsdk/examples/sound/spustream/`: the same technique from an
  in-memory buffer, with `interleave.py` documenting the interleaved `.VAG` layout.
- `toolchain-win64/share/psn00bsdk/examples/sound/vagsample/`: the upload and key-on sequence
  for a plain sample.
- `toolchain-win64/share/psn00bsdk/examples/cdrom/cdxa/`: XA playback, for when we add it.
- `system-docs/md/libover47.md`, chapter 16 "Basic Sound Library" and chapter 11 CD-ROM: Sony's
  explanation of sound RAM, voices, VAG files, the SPU IRQ, XA interleave rules and the
  "CdInit before SpuInit" rule. Chapter 15 covers SEQ/VAB if sequenced music ever comes up.
- `system-docs/md/libref.md`: per-function PsyQ reference. `SpuWrite` (64-byte DMA units,
  no stack buffers), `SpuSetIRQAddr`, `CdRead`, `CdReadCallback`, `CdMix`. Many names there do
  not exist in PSn00bSDK; check the headers first.
- `toolchain-win64/bin/psxavenc.exe` with `psxavenc-LICENSE.txt`: the encoder, v0.3.1.

External:

- [psx-spx: Sound Processing Unit](https://psx-spx.consoledev.net/soundprocessingunitspu/):
  the hardware truth on RAM layout, ADPCM blocks and flags, voice registers, the IRQ and its
  "all voices always read" gotcha, and the one-write-per-44.1-kHz-tick timing note.
- [psx-spx: CD-ROM drive](https://psx-spx.consoledev.net/cdromdrive/): XA sector layout,
  interleave rates, the Setfilter and Setmode bits, CD-DA commands.
- [psxavenc](https://github.com/WonderfulToolchain/psxavenc) and its
  [releases](https://github.com/WonderfulToolchain/psxavenc/releases): formats, every flag,
  loop handling. Archived in January 2026; a mirror continues at zff.dev.
- [PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK): `libpsn00b/psxspu/common.c` is where we
  confirmed `SpuInit()` parks every voice on the dummy block and that the transfer-complete
  wait is a register poll.
- [mkpsxiso](https://github.com/Lameguy64/mkpsxiso): the `type="xa"` file and
  `<track type="audio">` syntax for the two paths we did not take.
- [candyk-psx xainterleave](https://github.com/ChenThread/candyk-psx/tree/master/toolsrc/xainterleave):
  the XA interleaver to copy if XA is ever added (note it writes 2352-byte sectors; mkpsxiso
  wants 2336).
- [Xpost2000/ABCDEFG `src/audio.c`](https://github.com/Xpost2000/ABCDEFG/blob/RYEGAR-JERRY/src/audio.c):
  the reference implementation this started from.
