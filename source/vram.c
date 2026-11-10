#include "vram.h"

//
// What is free for us to use (see the VRAM map in render.c):
//
//   textures:  right of the framebuffers, stopping short of the SDK debug font.
//   CLUTs:     the strip left over below the framebuffers. A CLUT is only ever
//              one row tall so it fits there, and nothing else really would.
//
// x = 320 also happens to be exactly on a texture page boundary (5 * 64).
//
#define VRAM_TEXTURE_START_X (320)
#define VRAM_TEXTURE_END_X   (960)
#define VRAM_END_Y           (512)

#define VRAM_CLUT_END_X      (320)
#define VRAM_CLUT_START_Y    (480)

static int g_shelf_x;
static int g_shelf_y;
static int g_shelf_height;
static int g_clut_x;
static int g_clut_y;

void vram_reset(void)
{
  g_shelf_x      = VRAM_TEXTURE_START_X;
  g_shelf_y      = 0;
  g_shelf_height = 0;
  g_clut_x       = 0;
  g_clut_y       = VRAM_CLUT_START_Y;
}

//
// Shelf packing: fill rows left to right, and when a row is full start another
// one above the tallest thing on the row. Since uploads arrive tallest first,
// the first image on a shelf decides its height and nothing later wastes much.
//
// The hardware adds two rules that a textbook shelf packer does not have.
//
// A primitive samples from a single texture page, and UVs are 8 bits, so a page
// is at most 256 texels across. In VRAM units that window is:
//
//   4bpp:  64 wide    8bpp: 128 wide    16bpp: 256 wide
//
// An image must fit entirely inside of one window, starting on a multiple of 64.
// Pages are also 256 tall and start at y = 0 or y = 256, so nothing can straddle
// y = 256 either.
//
static Vector2 vram_allocate_image(int width, int height, int mode)
{
  Vector2 result;
  int     page_width;

  page_width = VRAM_TPAGE_ALIGN_X << mode;

  assert(width <= page_width && height <= VRAM_TPAGE_ALIGN_Y && "[VRAM] Image does not fit in a texture page.");

  if ((g_shelf_x % VRAM_TPAGE_ALIGN_X) + width > page_width) {
    g_shelf_x = align_up(g_shelf_x, VRAM_TPAGE_ALIGN_X);
  }

  if (g_shelf_x + width > VRAM_TEXTURE_END_X) {
    g_shelf_x       = VRAM_TEXTURE_START_X;
    g_shelf_y      += g_shelf_height;
    g_shelf_height  = 0;
  }

  if ((g_shelf_y % VRAM_TPAGE_ALIGN_Y) + height > VRAM_TPAGE_ALIGN_Y) {
    g_shelf_x      = VRAM_TEXTURE_START_X;
    g_shelf_y      = align_up(g_shelf_y, VRAM_TPAGE_ALIGN_Y);
    g_shelf_height = 0;
  }

  assert(g_shelf_y + height <= VRAM_END_Y && "[VRAM] Out of texture space.");

  result.x = g_shelf_x;
  result.y = g_shelf_y;

  g_shelf_x      += width;
  g_shelf_height  = max(g_shelf_height, height);

  return result;
}

//
// The hardware can only address a CLUT on a multiple of 16 in x. Palettes are
// either 16 or 256 entries, so packing them side by side keeps that true.
//
static Vector2 vram_allocate_clut(int width)
{
  Vector2 result;

  if (g_clut_x + width > VRAM_CLUT_END_X) {
    g_clut_x  = 0;
    g_clut_y += 1;
  }

  assert(g_clut_y < VRAM_END_Y && "[VRAM] Out of CLUT space.");

  result.x = g_clut_x;
  result.y = g_clut_y;

  g_clut_x += width;

  return result;
}

static void vram_copy_to_location(Vector2 location, const RECT* source_rect, const uint32_t* pixels)
{
  RECT destination;

  setRECT(&destination, location.x, location.y, source_rect->w, source_rect->h);
  LoadImage(&destination, pixels);
}

Vram_Texture vram_upload_tim(const TIM_IMAGE* tim)
{
  Vram_Texture result = {};
  Vector2      image_location;
  int          mode;

  mode = tim->mode & VRAM_TIM_MODE_MASK;
  assert(mode <= 2 && "[VRAM] Only 4bpp, 8bpp and 16bpp images can be used as textures.");

  image_location = vram_allocate_image(tim->prect->w, tim->prect->h, mode);
  vram_copy_to_location(image_location, tim->prect, tim->paddr);

  if (tim->mode & VRAM_TIM_HAS_CLUT) {
    //
    // ponytail: one palette per image. A TIM holding alternate palettes is
    // taller than one row, allocating those would need rows to be reserved.
    //
    assert(tim->crect->h == 1 && "[VRAM] TIMs with multiple palettes are not supported.");

    result.clut_location = vram_allocate_clut(tim->crect->w);
    result.clut          = getClut(result.clut_location.x, result.clut_location.y);
    vram_copy_to_location(result.clut_location, tim->crect, tim->caddr);
  }

  //
  // LoadImage only starts a DMA transfer. Callers are going to reuse the file
  // buffer for the next read, so it has to be finished by the time we return.
  //
  DrawSync(0);

  result.image_rect = (Rectangle32) { image_location.x, image_location.y, tim->prect->w, tim->prect->h };
  result.tpage      = getTPage(mode, 0, image_location.x, image_location.y);
  result.u          = vram_tpage_u(image_location.x, mode);
  result.v          = vram_tpage_v(image_location.y);
  result.width      = tim->prect->w << (2 - mode);
  result.height     = tim->prect->h;

  _debugprintf("[VRAM] image at (%d, %d) size (%d, %d), clut at (%d, %d), %d x %d texels",
               result.image_rect.x, result.image_rect.y, result.image_rect.w, result.image_rect.h,
               result.clut_location.x, result.clut_location.y, result.width, result.height);

  return result;
}
