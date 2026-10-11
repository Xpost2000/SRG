# SRG — Simple. Retro. Game.

A PlayStation 1 homebrew game written in C against **PSn00bSDK**, cross-compiled to MIPS
R3000 and run in the PCSX-Redux emulator. Every dependency — compiler, SDK, CMake, Ninja,
emulator — is vendored in `toolchain-win64/`, so there is nothing to install.

## Build and run

Run these from the repo root (they work from anywhere; each re-roots itself):

| Command | What it does |
| --- | --- |
| `build.bat` | Configures the `default` CMake preset and builds into `run-tree/`, producing `game.bin` + `game.cue`. Also converts `res/audio/*.wav|mp3` to `run-tree/audio/*.VAG` with the vendored `psxavenc` (see the `srg_audio_*` rules in `CMakeLists.txt`). |
| `pcsx-run.bat` | Boots `run-tree/game.cue` in PCSX-Redux. Builds first. |
| `gen-docs.bat` | Regenerates `system-docs/md/` from the PDFs. Only needed if a PDF changes. |

In VS Code: `Ctrl+Shift+B` builds, `Ctrl+F5` builds and runs. Both are tasks in
`.vscode/tasks.json`; the `Ctrl+F5` binding is user-level (it maps to "Run Test Task") and
so is not part of this repo.

`setvars.bat` is a shared include that puts the vendored toolchain on `PATH` and sets
`PSN00BSDK_LIBS`; the other scripts `CALL` it. `run-tree/` is build output and is
gitignored — never edit anything in there.

IntelliSense is driven by `run-tree/compile_commands.json`, which CMake generates
automatically. If `#include <psxgpu.h>` starts showing as unresolved, run `build.bat` once
to regenerate it.

## Layout

| Path | What |
| --- | --- |
| `source/` | Game source. `main.c` is currently the input test scene (a D-pad-driven box plus a fake pause menu). |
| `source/input_pad.*` | Low-level pad driver wrapper: raw button masks, held/pressed/released edges. Game code should not call it directly. |
| `source/input_action.*` | Action layer: named actions, one schema table per layout (UI, gameplay A, gameplay B), polling only, menu auto-repeat. |
| `source/spu.*` | Low-level sound chip driver: uploads to sound RAM, voice key-on/off, and the SPU-IRQ ring-buffer stream driver. Game code should not call it directly. |
| `source/audio.*` | Audio layer game code uses: `audio_sfx_load/play`, `audio_music_play/stop/fade_out`, and the per-frame `audio_update()` that feeds music from the CD. |
| `res/audio/` | Source `.wav`/`.mp3` audio. Converted at build time; the `.VAG` outputs live in `run-tree/audio/` and are never committed. |
| `engine-docs/` | Design docs per engine system (`input.md`, `audio.md`), the audio deep dive (`audio-research.md`) and the style guide (`code-guide.md`). |
| `source/render.*` | Minimal double-buffered ordering-table renderer: flat `TILE` rectangles and debug text. |
| `source/memory_card.*`, `source/cdfs.*`, `source/memory_arena.*` | Memory card save/load, CD file reads, bump allocator. |
| `system-docs/` | SDK manuals: PDFs plus converted Markdown in `md/`. |
| `toolchain-win64/` | Vendored GCC MIPS cross-compiler, PSn00bSDK, CMake, Ninja, PCSX-Redux. |
| `CMakeLists.txt` | Declares the `game` executable and the `iso` CD image target. |
| `CMakePresets.json` | The `default` preset; points CMake at PSn00bSDK's toolchain file. |
| `iso.xml` | mkpsxiso CD layout — what files land on the disc. |
| `system.cnf` | PS1 boot configuration (entry executable, stack address). |

## PS1 essentials

The hardware is small and strange, and most of it is load-bearing when writing code here:

- **CPU:** MIPS R3000A at 33 MHz. **There is no floating-point unit.** The build passes
  `-msoft-float`, so `float`/`double` still *compile* — they just get emulated in software
  and are ruinously slow. Use fixed-point arithmetic, and the **GTE** (coprocessor 2) for
  3D math and matrix work.
- **Memory:** 2 MB main RAM, 1 MB VRAM, 512 KB sound RAM. VRAM is a 1024×512 16-bit
  framebuffer that holds *both* your display/draw buffers and all your textures — they
  compete for the same space.
- **libc is freestanding.** Only what is in `toolchain-win64/include/libpsn00b/` exists.
  No `malloc`-heavy idioms, no host OS, no filesystem beyond the CD.
- **Rendering** is double-buffered: a `DISPENV` (what the video hardware scans out) and a
  `DRAWENV` (where the GPU draws) per buffer, swapped each frame. Drawing is done by
  building primitives into an ordering table and handing it to the GPU.
- **The disc** is ISO9660 with 8.3 filenames; the root directory holds at most 30 entries.
  Add files via `iso.xml`.
- **Input** is digital buttons only; this game does not read analog sticks. Button bits in the
  pad packet are active-low, the BIOS refreshes them during vblank so read pads *after*
  `VSync(0)`, and memory-card I/O stops the pad driver so call `input_pad_start()` after any
  card access. Game code goes through `input_action.h` (named actions, schemas), never raw
  `PAD_*` masks, so a player's chosen preset always applies.
- **Audio** is the SPU: 24 voices playing 4-bit ADPCM out of its own 512 KB sound RAM, which
  the CPU can only reach by DMA. Sound effects are uploaded whole (voices 2-23); music is
  streamed off the CD through a ring buffer into a 16 KB double buffer (voices 0-1), so the
  drive stays free for loads between refills. Call `audio_update()` every frame and between
  files in a load loop. `cd_start()` must run before `audio_initialize()`. Game code goes
  through `audio.h`, never `spu.h` or SPU registers. Assets are `.wav`/`.mp3` in `res/audio/`
  plus one `srg_audio_*` line in `CMakeLists.txt` and one `<file>` in `iso.xml`.

## Where to look things up

Precedence matters here, because two of the three manuals document a *different SDK*:

1. **`toolchain-win64/include/libpsn00b/`** — the actual headers. If a function is not
   declared here, it does not exist. This is the only authority.
2. **`system-docs/md/psn00b.md`** — PSn00bSDK's own reference manual.
3. **`system-docs/md/libref.md`** — Sony's PsyQ per-function reference. PSn00bSDK is
   *mostly* PsyQ-compatible, so names usually match, but **many functions documented here
   do not exist in PSn00bSDK**. Always confirm against the headers before using one.
4. **`system-docs/md/libover47.md`** — Sony's conceptual overview. The best explanation of
   how the GPU, GTE, SPU and CD subsystems actually work.

These total ~2 MB. **Grep them and read a slice around the hit; never read one whole.**
Entries begin with the bare function name on its own line, so anchor your search:

```sh
grep -n "^SetDefDrawEnv$" system-docs/md/libref.md
grep -rn "SetDefDrawEnv" toolchain-win64/include/libpsn00b/
```

See `system-docs/md/README.md` for more detail and the conversion's known limitations.

For audio specifically, read `engine-docs/audio-research.md` first: it explains the SPU and
CD audio paths in plain terms, compares how commercial games did music, and lists which of the
manuals' SPU functions actually exist in PSn00bSDK (there is no `SpuSetIRQ`, for example).

## Code conventions

The full guide is **`engine-docs/code-guide.md`**. Read it before writing code. The reference
implementation of the house style is `source/memory_card.c`; when unsure, match it. The three
rules that matter most:

- **C, not C++.** 2-space indent, K&R braces, `snake_case` functions as `module_verb_object`,
  `Pascal_Snake` types declared as `typedef struct X X;` then `struct X { ... };`, `g_` globals,
  `#define NAME (value)` constants, `int` as bool. **No `float`.**
- **Keep it simple.** Data tables over branching code, polling over callbacks, no abstraction
  with a single implementation, delete no-op functions.
- **Comment style is the distinctive thing here, and it should be preserved.** Heavy `//`
  banner blocks that explain *why* something is done and reason about the hardware, not just
  what the line does. ASCII diagrams are welcome — the VRAM map comment in `render.c` is the
  model to imitate. When adding code that touches hardware, explain the hardware.

Each engine system has a design doc in `engine-docs/` (currently `input.md`). Add one when you
add a system.
