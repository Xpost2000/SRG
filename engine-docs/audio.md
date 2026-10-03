# Audio System

How sound works in SRG, why it is built the way it is, and how to use it. The background
reading (how the hardware works, what other PS1 games did, and why we chose this design) is in
[`audio-research.md`](audio-research.md).

## The one-paragraph version

The sound chip (SPU) has 24 **voices** and its own 512 KB of RAM. A voice plays a compressed
sample that must already be in that RAM. **Sound effects** are small, so we load each one whole
into a bank in SPU RAM once per scene and fire it from any of 22 voices. **Music** is big, so it
is never loaded: two voices play out of a 16 KB double buffer in SPU RAM that an interrupt
refills from a 96 KB ring buffer in main RAM, which `audio_update()` tops up from the CD a few
sectors at a time. The CD drive is idle most of the time, so normal file loads still work while
a song plays. Everything game code sees is polled; the interrupt is hidden in `spu.c`.

```
 res/audio/*.wav|mp3 ──psxavenc (build)──► run-tree/audio/*.VAG ──mkpsxiso──► disc

                CD                 main RAM                 SPU RAM (512 KB)        speakers
 SFX:   AUDIO/BLIP.VAG ──load──► staging 64 KB ──DMA──► sample bank ──► voice 2..23 ──►
 music: AUDIO/LOOP.VAG ──24 sectors at a time──► ring 96 KB ──IRQ+DMA──► double buffer
                                          audio_update()          16 KB ──► voice 0,1 ──►
```

## What the hardware actually does

Checked against `toolchain-win64/include/libpsn00b/psxspu.h`, `hwregs_c.h`, the Sony manuals
in `system-docs/md/` and psx-spx. Details and sources are in `audio-research.md`.

- **The SPU only plays from its own RAM.** 512 KB, reachable only by DMA. The first 4 KB are
  hardware capture buffers, `SpuInit()` puts a silent 16-byte block at `0x1000`, everything
  from `0x1010` is ours. `spu.h` has the full map as a diagram.
- **Samples are 4-bit ADPCM in 16-byte blocks of 28 samples**, 3.5:1 compression. Each block
  carries a flag byte: loop start, loop end, or "end and go silent". A voice never stops
  reading RAM, so a one-shot sample ends in a silent block that loops on itself.
- **Each voice has a start address, a pitch, left/right volume and a loop address.** Pitch is
  12-bit fixed point: `0x1000` plays at 44.1 kHz, `0x800` at 22.05 kHz. The loop address
  register can be rewritten while the voice plays; that is the trick streaming is built on.
- **The SPU can raise an interrupt when any voice reads a chosen address.** Set `SPU_IRQ_ADDR`
  to the start of buffer half B; when the voice loops from A into B the interrupt fires and
  the handler refills A. Idle voices must sit on the silent block or they would fire it too.
- **DMA moves data in 64-byte units** and reads main RAM directly, so sample buffers are padded
  to 64 bytes, word aligned, and never on the stack.
- **One CD pickup head.** The drive reads 150 sectors a second at 2x; a 22.05 kHz stereo song
  consumes 12. That spare capacity is what every other file load lives in.
- **`CdInit()` before `SpuInit()`**, or transfer-complete polling can misfire. `cd_start()`
  runs before `audio_initialize()` in `main.c` for that reason.

## The two layers

**`spu.c`** is hardware only: upload bytes to an SPU address, key a voice on with address,
pitch and volume, and the stream driver (ring buffer, SPU interrupt handler, DMA-done handler).
Game code never includes it.

**`audio.c`** is what the game calls: files, handles and frames. It loads `.VAG` files through
the CD module, hands out `Audio_Sfx` handles, owns the music state machine, and runs the CD
feeder from `audio_update()`. It never touches a register.

## Decisions and why

| Decision | Why |
| --- | --- |
| Music is SPU streaming, not CD-XA or CD-DA | Seamless loops, any sample rate (22 kHz halves the disc cost), and the drive is free between refills so scenes can keep loading. XA locks the drive to the stream and loops with a gap; CD-DA is 10 MB a minute. See `audio-research.md`. |
| Voices 0 and 1 are reserved for music, 2 to 23 are the effect pool | A burst of effects can never steal the song's voices and stutter it. 22 simultaneous effects is more than most commercial PS1 games had. |
| Sound effects are a per-scene bump allocator in SPU RAM | No free list, no fragmentation, no pager. A scene loads what it needs on entry and `audio_sfx_unload_all()` resets the cursor on exit. The ~490 KB bank holds about 40 seconds of 22 kHz effects. |
| One 64 KB staging buffer for all loads | Static, word aligned, reused. Caps a single effect at about 5 seconds; anything longer is music. |
| The feeder is polled from `audio_update()`, not timer driven | Matches the rest of the engine. It only issues a read when the drive is idle and the ring has room for 24 sectors, so it never blocks and never fights a scene's own loads. |
| `cdfs.c` clears the read callback before every blocking read | The CD read-complete callback is one global hook that the feeder installs per read. Without clearing it, a font load would feed its bytes to the stream. One line, in the module that owns the read. |
| Sample uploads run inside a critical section | The stream interrupt uses the same DMA channel. Masking interrupts defers the SPU IRQ rather than losing it, and an upload is milliseconds against a 325 ms chunk. |
| Assets are converted at build time by a vendored `psxavenc` | Nothing to install, nothing generated is committed, and a changed `.wav` rebuilds only its own `.VAG`. |
| 22.05 kHz is the default rate | Half the size of 44.1 kHz and indistinguishable through a PS1 and a TV for effects and most music. Per-asset override in `CMakeLists.txt`. |

## Using it

### 1. Add the source file and a build rule

Drop a `.wav` or `.mp3` into `res/audio/` and add one line to `CMakeLists.txt`:

```cmake
srg_audio_sfx  (JUMP  res/audio/jump.wav   22050)      # mono one-shot
srg_audio_sfx  (HUM   res/audio/hum.wav    11025 -L)   # extra flags pass through: -L forces a loop
srg_audio_music(THEME res/audio/theme.mp3  22050 2)    # streamed, 1 or 2 channels
```

A `.wav` with a `smpl` loop chunk (most DAWs can write one) loops at that point automatically.
Then list the output on the disc in `iso.xml`:

```xml
<dir name="AUDIO">
  <file name="JUMP.VAG"  type="data" source="${CMAKE_BINARY_DIR}/audio/JUMP.VAG"/>
  <file name="THEME.VAG" type="data" source="${CMAKE_BINARY_DIR}/audio/THEME.VAG"/>
</dir>
```

Names are 8.3. `build.bat` does the rest.

### 2. Start the system

```c
render_initialize();
cd_start();
audio_initialize();   // after cd_start, before anything plays
```

### 3. Load a scene's sound effects

```c
static Audio_Sfx g_sfx_jump;

void scene_enter(void) {
  audio_sfx_unload_all();                           // throw away the previous scene's bank
  g_sfx_jump = audio_sfx_load("\\AUDIO\\JUMP.VAG"); // AUDIO_SFX_INVALID if missing
}
```

### 4. Play effects

```c
audio_sfx_play(g_sfx_jump);                                           // full volume, centred
audio_sfx_play_ex(g_sfx_jump, AUDIO_VOLUME_MAX, 0, AUDIO_PITCH_NORMAL); // hard left
audio_sfx_play_ex(g_sfx_jump, AUDIO_VOLUME_MAX, AUDIO_VOLUME_MAX, AUDIO_PITCH_NORMAL / 2); // octave down

int voice = audio_sfx_play(g_sfx_hum);   // a looping sample keeps going...
audio_sfx_stop(voice);                   // ...until you stop its voice
```

Playing an invalid handle is a silent no-op, so a missing asset never crashes a scene.

### 5. Music

```c
audio_music_play("\\AUDIO\\THEME.VAG", 1);  // 1 = loop forever, 0 = play once and stop
audio_music_set_volume(AUDIO_VOLUME_MAX / 2, AUDIO_VOLUME_MAX / 2);
audio_music_fade_out(60);                    // one second, then stops itself
audio_music_stop();                          // immediate
if (audio_music_is_playing()) { ... }
```

Starting a song blocks for about a third of a second while the ring fills. Do it on a scene
transition, not mid-gameplay.

### 6. Frame loop

```c
for (;;) {
  scene_update(&scene);
  audio_update();        // never blocks: refills the ring if the drive is idle, steps fades
  scene_draw(&scene);
  input_pad_frame();
  render_end_frame();
}
```

### 7. Loading while music plays

A blocking `cd_file_read_sync_uncached()` is fine during music. The ring holds about 3.8
seconds, so one file of up to roughly 1 MB loads without a hiccup. For a *sequence* of files,
give the feeder a turn between them:

```c
for (int i = 0; i < file_count; ++i) {
  cd_file_read_sync_uncached(&files[i], buffer, size);
  audio_update();   // refill if low; the next read waits its turn behind the refill
}
```

Without that, twenty back-to-back seeks drain the ring (the test scene's CD READ TEST showed six
underruns before this line was added, zero after). If a load is genuinely bigger than the ring's
runway, `audio_music_stop()` first and start the next song when the screen comes back.

## The test scene

`source/main.c` wires it up. Every colour change plays the blip (COLOR- an octave down),
dropping a marker plays it panned to where the box is, and the pause menu has MUSIC (start or
fade out the looping test song) and CD READ TEST (twenty blocking font reads while the song
plays). The line under the status shows ring fill and the underrun count; the test should leave
it at zero.

## Rules of thumb

1. Game code calls `audio_*`, never `spu_*` or SPU registers.
2. `audio_initialize()` after `cd_start()`.
3. `audio_update()` once per frame, and once between files in a load loop.
4. Effects under 64 KB (about 5 seconds at 22 kHz). Longer is music.
5. Start music on a transition; it blocks briefly to prime the ring.
6. `audio_sfx_unload_all()` on scene exit, reload on entry. Handles do not survive it.
7. Music files must be encoded with `-i 4096` (the `srg_audio_music` rule does this).

## Further reading

- `engine-docs/audio-research.md`: the deep dive, the alternatives, and every source.
- `source/spu.h`: the sound RAM map and voice assignment.
- `toolchain-win64/share/psn00bsdk/examples/sound/cdstream/`: the SDK example `spu.c` is a
  port of, with a longer argument for SPU streaming over XA in its header comment.
- [psxavenc](https://github.com/WonderfulToolchain/psxavenc): the encoder, with every flag.
