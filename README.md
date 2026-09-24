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

## Vendored Tools
The vendored executables and toolchain are for a Windows 64bit development system and it's all in `toolchain-win64\bin`,
the `setvars.bat** batch script will setup your shell to have those in your path for usage.

It includes various tools for either general purpose development or PS1 specific format tools. More specifically:

### MIPS GCC Cross Compiler from: https://github.com/Lameguy64/PSn00bSDK/tree/master
This is required to build a working executable for the PS1 processor, which is a MIPS R3000 without
cache essentially.

### ImageMagick (A set of image manipulation CLI tools): https://imagemagick.org/command-line-tools/
It's like FFMPEG but for images. I include a complete distribution for 7.1.1, but the biggest use is `mogrify/magick` which
are mainly used to either resize images, or resample to a certain color palette (as the PS1 uses paletted / indexed image formats).

A quick cheatsheet for commands that work with the distribution are:

#### Resize an Image
This is the most common operation we may use as we need things to fit in TPAGEs which are either
256x256, 128x256, 64x256 depending on the bits per pixel format.

This is a general resize.
```
magick example.png -resize 256x256 example.jpg
```

This resizes an image to be 256 pixels high (with width calculated appropriately to fit.)
```
magick example.png -resize x256 example.jpg
```

Same as the above, but with width.
```
magick example.png -resize 256x example.jpg
```

#### Quantize an image
img2tim has a really simple quantization, but ImageMagick can do it better if we need a more limited
palette (256 colors or something similar.)

NOTE: img2tim by default generates a unique palette per image offered, so we can't really share palettes,
but it could be something neat to look into.

```
magick example.png -colors 16
```

You can also create a shared palette that you can then reuse for quantizing a set of images. The `-unique-colors` flag
will cause `magick` to output a color palette which can then be used for quantizing images with the same palette.

```
magick sprite1.png sprite2.png -colors 16 -unique-colors shared_palette.png

..
..

magick sprite1.png -remap shared_palette.png remapped.png
magick sprite2.png -remap shared_palette.png remapped2.png
```

### IMG2TIM: https://github.com/Lameguy64/img2tim/tree/master
This is a more modern recreation of an original PSYQ tool, it's a simple CLI. It's just used to convert modern images into TIM
which is a index based image format, it's the format that PSY-Q / PS1 SDKs tend to use.

NOTE: TIMs contain their VRAM offset information, however we don't really have a need for this as we are generally allocating the
VRAM ourselves, so our usage is pretty straightforward.

#### Convert Image

Here's a really basic usage for IMG2TIM, it's not super complicated.

Be mindful of transparency as on the PS1 it uses a bit for transparency vs colorkey.

```
img2tim -bpp 8|16|24 -usealpha -o out.tim picture.png
```

### TIM2VIEW: https://github.com/lab313ru/tim2view
This is just to debug whether our TIMs look correct or not. It's a straightforward gui program.


## Documentation

The SDK manuals in `system-docs/` are also available as searchable Markdown in
`system-docs/md/`. See `system-docs/md/README.md` — in particular the note that two of the
three manuals document Sony's PsyQ SDK, not PSn00bSDK, and that the headers in
`toolchain-win64/include/libpsn00b/` are the only authority on what actually exists.
