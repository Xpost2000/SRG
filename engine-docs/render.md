# 2D Renderer and VRAM

`source/render.*` draws flat rectangles, sprites and text. `source/vram.*` decides where textures
live. Fonts are loaded by `source/font.*`, which holds data only and does no drawing.

## Using it

Load time, for anything that goes in VRAM:

```c
vram_reset();

// tallest first: walk texture_ordered_file_id_table from fileslist.g.h
GetTimInfo((const uint32_t*) file_data, &tim);
g_logo = vram_upload_tim(&tim);
```

Every frame, 3D first and then 2D:

```c
R_render_new_frame();
R_render_push_mesh(mesh);
R_render_end_frame();

render_tile(16, 16, 64, 32, 0, 0, 0, 2);
render_sprite(&g_logo, (Rectangle32) {0, 0, g_logo.width, g_logo.height}, position, color, 5);
render_font_text(&g_font, (Vector2) {8, 8}, "HELLO\nSECOND LINE", color, 0);

render_end_frame();
```

`z` runs from 0 to `RENDER_2D_OT_LENGTH - 1`, and 0 is the front. All of 2D is in front of all of 3D.

`render_text` still exists, but it uses the SDK's `FntSort` and is for debug overlays only.

## Things that will catch you out

**Two draws at the same z come out in reverse call order.** `addPrim` pushes onto the front of a
bucket, so the later draw is drawn first and ends up underneath. Use different z values if the
order matters. It is also why a `DR_TPAGE` is sorted after the sprites that need it.

**Sprite colour is a multiplier, and 128 means unchanged.** 255 is double brightness.

**`SPRT` cannot scale or rotate.** It copies texels 1:1.

**VRAM units are not texels.** VRAM is always counted in 16bpp units. A 4bpp image 252 texels wide
is 63 units wide. UVs are in texels, relative to the start of the texture page.

## Why direct packets

3D records commands and processes them in `R_render_end_frame` because that is where the GTE
transforms and culling happen. 2D has nothing to defer: the GPU packet is already the finished
command. A `Render_Command` is about 48 bytes because its union is sized for meshes, where a `SPRT`
is 20, so recording 2D would store everything twice.

## Why a separate 2D ordering table

Each buffer has `ordering_table` for 3D and `ordering_table_2d` for 2D. The 2D table is chained
behind entry 0 of the 3D table, so the GPU still gets one pointer and one kick. That guarantees the
UI is in front of the world without needing a depth convention, and layers cost 8 bytes each (one
entry per buffer) rather than a bigger 3D table.

The link is made right after clearing, not at the end of the frame. A triangle sorted into 3D entry
0 later is pushed in front of the link, so it still draws before the UI.

## Flushing instead of crashing

When a draw call would run out of its 24 KB packet arena, `_render_flush` submits what has been
built so far and starts over. Three details make this work:

1. While building frame N, the region it draws into holds frame N-2 and is what the screen is
   showing. The flush waits for frame N-1, displays it, and only then draws, so that the partial
   frame is off-screen.
2. `DrawOTagEnv` clears the screen when `isbg` is set, so only the first submission of a frame uses
   it. Later ones are plain `DrawOTag`.
3. The GPU reads packets as it goes, so the flush waits for it to finish before reusing them.

A flush costs a frame hitch, and whatever is drawn after it lands on top regardless of z. It is a
safety net. The debug line in the test scene shows the peak bytes used and the flush count, and if
the count is above 0 in normal play then `RENDER_2D_PACKET_BUFFER_SIZE` needs to go up. Turn on
**2D STRESS** in the pause menu to force flushes on purpose.

Every draw call reserves all of its bytes before writing anything, so that a flush can only happen
between draw calls and never separates a sprite from its `DR_TPAGE`.

## VRAM layout

```
 0          320                        960   1023
 +-----------+--------------------------+-----+ 0
 | DRAW0     |                          | SDK |
 +-----------+     texture shelves      |font |
 | DRAW1     |                          |     |
 +-----------+                          |     | 480
 | CLUTs     |                          |     |
 +-----------+--------------------------+-----+ 512
```

Images are placed with next-fit shelf packing. `texture_ordered_file_id_table` is generated sorted
tallest first, so a shelf's first image decides its height, packing is O(n), and the result is the
same on every run. Adding an image to `iso.xml` adds it to that table, but `load_textures` in
`main.c` also needs a `case` saying where it goes.

The hardware adds two rules on top of normal shelf packing:

- An image has to fit inside one texture page window, which starts on a multiple of 64 and is 64
  units wide at 4bpp, 128 at 8bpp and 256 at 16bpp.
- A page is 256 tall and starts at y = 0 or y = 256, so nothing can cross y = 256.

CLUTs have to start on a multiple of 16 in x. They go in the strip below the framebuffers.

There is no freeing of individual textures. A new loadscreen calls `vram_reset` and uploads what it
needs. This matches how assets are meant to flow: read from disc, upload, and forget the file.

## Sources

- `system-docs/md/psn00b.md`: `ClearOTagR`, `DrawOTag`, `LoadImage`, `DrawSync`
- `system-docs/md/libover47.md`: how the GPU, texture pages and CLUTs work
- `toolchain-win64/include/libpsn00b/psxgpu.h`: primitive layouts and macros
- [PR #19](https://github.com/Xpost2000/SRG/pull/19): font format and the texture page notes
- [PR #26](https://github.com/Xpost2000/SRG/pull/26): why the texture list is sorted by height
