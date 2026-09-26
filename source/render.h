#ifndef RENDER_H
#define RENDER_H

#include "common.h"

//
// The smallest thing that can put a rectangle and some debug text on the
// screen. Double-buffered, ordering-table based, lifted from the PSn00bSDK
// "hello" example. Sprites and textures are not here yet.
//
// Frame shape:
//
//   render_tile(...); render_text(...);   // any order, sorted by z
//   render_end_frame();                    // waits for GPU + VSync, swaps,
//                                          // and clears for the next frame
//
// z runs 0..RENDER_OT_LENGTH-1. HIGHER z is drawn FIRST (further back), so
// z = 0 is the front-most layer.
//

#define RENDER_SCREEN_WIDTH  (320)
#define RENDER_SCREEN_HEIGHT (240)
#define RENDER_OT_LENGTH     (16)

void render_initialize(void);
void render_tile(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, int z);
void render_text(int x, int y, int z, const char* text);
void render_end_frame(void);

#endif
