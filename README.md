# SRG

This is a Playstation 1 game that I would like to also be able to run on PC.

All dependencies are vendored in the repository for the supported development platforms,
and testing targets.

## Dependencies

- PSN00BSDK toolchain (mostly PSYQ compatible open source PS1 SDK)
- cmake
- PCSX-Redux

Everything above is vendored in `toolchain-win64/`, so there is nothing to install.

## Building and running

From VS Code:

| Shortcut | What it does |
| --- | --- |
| `Ctrl+Shift+B` | Build — produces `run-tree/game.bin` + `game.cue` |
| `Ctrl+F5` | Build and run in PCSX-Redux |

`Ctrl+F5` is a user-level keybinding for the "Run Test Task" command, so it lives in your
own `keybindings.json` rather than in this repo. Without it, the same task is reachable
via **Terminal → Run Task…** → *(Win64) Run With PCSX-Redux*.

From a terminal — these work from any directory:

| Command | What it does |
| --- | --- |
| `build.bat` | Configure the `default` CMake preset and build into `run-tree/` |
| `pcsx-run.bat` | Boot `run-tree/game.cue` in PCSX-Redux (builds first) |
| `gen-docs.bat` | Regenerate `system-docs/md/` from the SDK PDFs (only if a PDF changes) |

`run-tree/` is build output and is gitignored.

## Documentation

The SDK manuals in `system-docs/` are also available as searchable Markdown in
`system-docs/md/`. See `system-docs/md/README.md` — in particular the note that two of the
three manuals document Sony's PsyQ SDK, not PSn00bSDK, and that the headers in
`toolchain-win64/include/libpsn00b/` are the only authority on what actually exists.
