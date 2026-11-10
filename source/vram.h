#ifndef VRAM_H
#define VRAM_H

//
// This is where textures live.
//
// The intended flow is the one all of our images follow: at load time, read the
// file from disc, upload it here, and forget the file. The pixels stay in VRAM,
// we only need to remember where they went, which is what a Vram_Texture is.
//
// NOTE:
// - Upload in the order of texture_ordered_file_id_table (fileslist.g.h). It is
//   sorted tallest first, and that is what makes the shelves pack tightly.
// - There is no freeing individual textures. A new loadscreen calls vram_reset
//   and uploads everything it needs again.
// - Two coordinate systems are in play. VRAM is always measured in 16bpp units,
//   a texture is measured in its own texels. A 4bpp image that is 252 texels
//   wide takes up only 63 units of VRAM.
//

#include "common.h"

#include <psxgpu.h>

//
// The low bits of a TIM's flags. These happen to be the same values that
// getTPage takes: 0 = 4bpp, 1 = 8bpp, 2 = 16bpp
//
#define VRAM_TIM_MODE_MASK (0x7)
#define VRAM_TIM_HAS_CLUT  (0x8)

//
// A texture page starts on a multiple of these, in VRAM units.
//
#define VRAM_TPAGE_ALIGN_X (64)
#define VRAM_TPAGE_ALIGN_Y (256)

//
// Primitive UVs are in texels relative to the start of their texture page, so
// these turn a VRAM position into one. How many texels fit in a VRAM unit depends
// on the bit depth: 4 at 4bpp, 2 at 8bpp, 1 at 16bpp.
//
#define vram_tpage_u(vram_x, mode) (((vram_x) % VRAM_TPAGE_ALIGN_X) << (2 - (mode)))
#define vram_tpage_v(vram_y)       ((vram_y) % VRAM_TPAGE_ALIGN_Y)

typedef struct Vram_Texture Vram_Texture;

struct Vram_Texture {
  //
  // Where it is, in VRAM units
  //
  Rectangle32 image_rect;
  Vector2     clut_location;

  //
  // Ready to be written directly into primitives
  //
  uint16_t    tpage;
  uint16_t    clut;
  uint8_t     u;
  uint8_t     v;

  //
  // in texels
  //
  uint16_t    width;
  uint16_t    height;
};

void         vram_reset(void);

//
// Picks a spot and copies the image and its palette there. Blocks until the
// transfer is done, so the memory backing the TIM can be reused as soon as this
// returns.
//
Vram_Texture vram_upload_tim(const TIM_IMAGE* tim);

#endif
