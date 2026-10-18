# Audio System

How sound works in SRG, why it is built the way it is, and how to use it. The background
reading (how the hardware works, what other PS1 games did) is in
[`audio-research.md`](audio-research.md).

## The one-paragraph version

The sound chip (SPU) has 24 **voices** and its own 512 KB of RAM. A voice plays a compressed
sample that must already be in that RAM, at any pitch. So we keep a small **instrument bank**
(the "sound font": piano, bass, lead, kick, snare, hat, blip, about 10 KB), upload it once at
boot, and never touch it again. **Music and jingles are text**: short MML files listing which
note to play on which instrument and for how long. They are parsed once at boot into 6-byte
note events in main RAM, and a sequencer in `audio_update()` keys voices on and off as the
song plays. **A plain sound effect** is just one note on one instrument. After boot, audio
never reads the disc.

```
 res/audio/*.wav ──psxavenc (build)──► run-tree/audio/*.VAG ──mkpsxiso──► disc
 res/audio/*.mml ─────────────────────────────────────────────mkpsxiso──► disc

 boot:   *.VAG ──malloc, then free──DMA───► sound RAM bank (all instruments, forever)
         *.MML ──malloc, then free──parse─► g_events (main RAM, 6 B per note)

 frame:  audio_update() ── tick ──► key voice n on at note's pitch ──► speakers
```

## What the hardware actually does

Checked against `toolchain-win64/include/libpsn00b/psxspu.h`, `hwregs_c.h`, the Sony manuals
in `system-docs/md/` and psx-spx.

- **The SPU only plays from its own RAM.** 512 KB, reachable only by DMA. The first 4 KB are
  hardware capture buffers, `SpuInit()` puts a silent 16-byte block at `0x1000`, and the bank
  starts at `0x1040`. `spu.h` has the map.
- **Samples are 4-bit ADPCM in 16-byte blocks of 28 samples**, 3.5:1 compression. Each block
  has a flag byte: *loop start* (latch this block as the repeat address), *end* (jump to the
  repeat address) and *repeat* (keep playing after the jump; without it the voice goes
  silent).
- **Pitch is a 14-bit register**, 4.12 fixed point relative to 44.1 kHz. `0x1000` plays a
  sample at 44.1 kHz and `0x800` at 22.05 kHz. That one register is what turns one recorded
  note into a whole keyboard. The register maxes out at `0x3FFF`: about two octaves above the
  root for a 22 kHz sample, four for an 11 kHz one.
- **Every voice has its own ADSR envelope generator**: two 16-bit registers. Key-on starts the
  attack. Key-off starts the release, so a note fades out instead of clicking. The bit
  layout is in the banner above `AUDIO_ADSR1_INSTANT` in `audio.c`.
- **DMA moves data in 64-byte units** and reads main RAM directly, so sample buffers are padded
  to 64 bytes, word aligned, and never on the stack.
- **`CdInit()` before `SpuInit()`**, or transfer-complete polling can misfire. `cd_start()`
  runs before `audio_initialize()` for that reason, and the bank load needs the drive anyway.

## The two layers

**`spu.c`** is hardware only: upload bytes to a sound RAM address, key a voice on with
address, pitch, volume and envelope, key it off. It has no interrupts and no state. Game code
never includes it.

**`audio.c`** is what the game calls. It holds the bank table, the MML parser, the sequencer
and the voice map, and it never touches a register.

## Voices

```
 voice  0 ..  7   music. Track n of the song plays on voice n, always.
 voice  8 .. 23   effect pool, 16 voices round-robin: audio_note_play() and jingle tracks
```

Music can never be cut off by effects. A burst of more than 16 effects steals the oldest
pool voice, which is the same trade-off the old system made.

## The bank

| Instrument | Sample | Loop | Release |
| --- | --- | --- | --- |
| `PIANO` | 0.6 s additive tone at 11 kHz, decay baked in | no | ~186 ms |
| `BASS` | triangle, single cycle | yes | ~46 ms |
| `LEAD` | square, single cycle | yes | ~46 ms |
| `KICK`, `SNARE`, `HAT` | one-shot drums at 11 kHz | no | ~371 ms |
| `BLIP` | the old test sine, resampled to 11 kHz | no | ~371 ms |

**The tuning rule: every sample sounds middle C (MIDI 60) when played at its own sample rate.**
That is the whole tuning model. Any note is then the root pitch × 2^(semitones/12), using a
12-entry table and a shift. Drums ignore it: write them as `c` (middle C), the sample's
natural pitch.

The sustained instruments are a single cycle of 84 samples (three ADPCM blocks, so the loop
seam falls on a block boundary), repeated 4 times and looped with psxavenc `-L`. C4 is
261.63 Hz, so the sample rate is 261.63 × 84 ≈ **21977 Hz**. psxavenc writes the loop
*end* flag but no loop *start*, and every `.VAG` begins with a silent block. If nothing were
done, the silent block could end up inside the loop and you'd hear a buzz. `_bank_load()`
flags block 1 as the loop start before uploading.

The current `.wav`s were synthesized by a throwaway script. Swap any of them for a real
recording, as long as it sounds C4 at its own sample rate.

## Song format (MML)

MML (Music Macro Language) is how 80s computers and chiptune trackers wrote music as text.
A song is a file of lines, and the first character of each line says which track it belongs
to:

```
; res/audio/theme.mml
t120                                 ; tempo, on a line of its own
A @LEAD v9 o5 l8 e g >c< b a g e g   ; track A
A f a >c< a f a g f                  ; more of track A, appended
B @PIANO v8 l4 o4 c e g e            ; track B, starts at the same time as A
D v15 l8 @KICK c r r r @SNARE c r    ; switch instrument mid-track
```

| Token | Meaning |
| --- | --- |
| `A`–`H` | First character of a line: the track it belongs to. Up to 8 tracks. Lines can be interleaved. |
| `t120` | Tempo in bpm. Only on a line with no track letter. |
| `@NAME` | Instrument, a name from the bank: `@PIANO`, `@BASS`, `@LEAD`, `@KICK`, `@SNARE`, `@HAT`, `@BLIP`. |
| `c d e f g a b` | A note. `+` or `#` sharpens, `-` flattens. Then an optional length and dots. |
| `r` | Rest. Same length rules. |
| length | `1` whole, `2` half, `4` quarter, `8` eighth, `16`, `32`; `3`, `6`, `12` are triplets. `.` adds half again: `c4.` is a dotted quarter. |
| `o4` | Octave. `o4 c` is middle C. `<` goes one down, `>` one up. |
| `l8` | Default length for notes written without one. |
| `v12` | Volume, 0–15. |
| `;` | Comment to end of line. |

Defaults per track: `o4 l4 v15 @PIANO`. Time is 24 ticks to a quarter note.

**Not supported yet:** ties, repeats (`[ ... ]3`), pan, per-note velocity and tempo changes
mid-song. Add them when a song needs them.

**Looping:** a looped song restarts every track together once *all* of them have ended. A
shorter track sits silent until the longest one finishes, so tracks never drift out of time.

**Errors** are logged as `[AUDIO] \AUDIO\THEME.MML:12: note out of range`. The bad token is
skipped, so the song still plays.

**Size:** one event is 6 bytes. The test theme (5 tracks, 4 bars) is 147 events, under 1 KB.
All songs share a 1024-event pool (6 KB). Raise `AUDIO_EVENT_MAX` when the songs need more.

## Using it

### 1. Add an instrument

A `.wav` that sounds C4, then one line in each of four places:

```cmake
srg_audio_sample(ORGAN res/audio/organ.wav 21977 -L)   # CMakeLists.txt; -L to loop
```
```xml
<file name="ORGAN.VAG" type="data" source="${CMAKE_BINARY_DIR}/audio/ORGAN.VAG"/>  <!-- iso.xml -->
```
```c
{ "ORGAN", "\\AUDIO\\ORGAN.VAG", AUDIO_ADSR1_INSTANT, AUDIO_ADSR2_RELEASE_SHORT }, // g_bank_defs, audio.c
AUDIO_INSTRUMENT_ORGAN,                                                            // enum, audio.h, same position
```

### 2. Add a song

Write `res/audio/name.mml` and add one `<file name="NAME.MML" ... source="${PROJECT_SOURCE_DIR}/res/audio/name.mml"/>`
to `iso.xml`. Names are 8.3.

### 3. Start the system and load everything

```c
cd_start();
audio_initialize();                                   // after cd_start: uploads the bank
g_song_theme = audio_song_load("\\AUDIO\\THEME.MML"); // AUDIO_SONG_INVALID if missing
g_song_coin  = audio_song_load("\\AUDIO\\COIN.MML");
```

Load every song at boot. After that, nothing reads the disc.

### 4. Sound effects

```c
// a single note: instrument, note, left/right volume. Returns the voice.
audio_note_play(AUDIO_INSTRUMENT_BLIP, AUDIO_NOTE_ROOT,      AUDIO_VOLUME_MAX, AUDIO_VOLUME_MAX);
audio_note_play(AUDIO_INSTRUMENT_BLIP, AUDIO_NOTE_ROOT - 12, AUDIO_VOLUME_MAX, 0); // octave down, hard left

int voice = audio_note_play(AUDIO_INSTRUMENT_LEAD, AUDIO_NOTE_ROOT, AUDIO_VOLUME_MAX, AUDIO_VOLUME_MAX);
audio_note_stop(voice);   // looped instruments keep going until you key them off

// a jingle: a whole song on borrowed pool voices, up to 4 at once
audio_sfx_play(g_song_coin);
```

Playing an invalid song or instrument is a silent no-op.

### 5. Music

```c
audio_music_play(g_song_theme, 1);   // 1 = loop, 0 = play once
audio_music_set_volume(AUDIO_VOLUME_MAX / 2, AUDIO_VOLUME_MAX / 2);
audio_music_fade_out(60);            // one second, then stops itself
audio_music_stop();                  // immediate
if (audio_music_is_playing()) { ... }
```

Starting a song is instant: nothing is loaded.

### 6. Frame loop

```c
for (;;) {
  scene_update(&scene);
  audio_update();        // the sequencer: key the notes due this frame, step fades
  scene_draw(&scene);
  input_pad_frame();
  render_end_frame();
}
```

Timing assumes 60 frames a second (NTSC). See the `ponytail:` note on
`AUDIO_FRAMES_PER_MINUTE`.

## Decisions and why

| Decision | Why |
| --- | --- |
| Sequenced music, not streamed | A song is a few hundred bytes of notes instead of 1.5 MB a minute, everything fits in memory from boot, and the CD drive belongs to the game. We deleted the old ring buffer, SPU IRQ driver and CD feeder (96 KB of main RAM, 16 KB of sound RAM, 2 voices). The cost: music can only sound like the bank. Recorded vocals or orchestral tracks would need streaming back. |
| One global bank, loaded at boot, never freed | About 10 KB of 490 KB. There is no reason to page it. More "fonts" are just more rows in `g_bank_defs`. |
| MML text, parsed on the console | No host tool to build or vendor. Songs are diffable text, and parsing a few KB at boot costs nothing. |
| Every sample is tuned to C4 | No per-instrument root-note or fine-tune field. The sample rate *is* the tuning. |
| A track owns one voice | Simple, and a song can't starve itself. One note at a time per track, so a chord is several tracks. |
| ADSR only does key-off release | The samples carry their own shape. The envelope just stops cut notes from clicking. |
| One-shots at 11 kHz, loops at 22 kHz | Halves the sound RAM of a one-shot. Drums and blips lose nothing audible through a PS1 and a TV. The single-cycle loops stay at 21977 Hz because they're only 208 bytes, and the rate is what tunes them. |
| Files load into a temporary `malloc`, freed right away | After boot, audio holds no load buffer at all: about 7.6 KB of main RAM in total, mostly the event pool. A file can be any size that fits in free RAM. |
| Polled sequencer, 16.16 tick accumulator | Matches the rest of the engine. At 60 Hz, one frame is the timing resolution, which is plenty for chiptune-speed music. |

## The test scene

`source/main.c` wires it up. COLOR+ plays the blip, COLOR- plays it an octave lower, and
DROP MARKER plays it panned to where the box is. CLEAR MARKERS plays the coin jingle.
Pause > MUSIC starts the theme looping, or fades it out if it's already playing.

## Rules of thumb

1. Game code calls `audio_*`, never `spu_*` or SPU registers.
2. `audio_initialize()` after `cd_start()`, then `audio_song_load()` every song at boot.
3. `audio_update()` once per frame.
4. A new instrument's sample must sound C4 at its own rate. Loop it with `-L` if it's
   sustained.
5. A chord is several tracks. Music has 8, so plan arrangements around that.

## Further reading

- `engine-docs/audio-research.md`: the hardware deep dive and the alternatives.
- `source/spu.h`: the sound RAM map.
- `source/audio.c`: the bank table, ADSR bit layout, MML grammar and sequencer, each with its
  own banner comment.
- [psxavenc](https://github.com/WonderfulToolchain/psxavenc): the encoder, with every flag.
