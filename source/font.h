#ifndef FONT_H
#define FONT_H

#include "common.h"

#include <psxgpu.h>

//
// Monospaced 4BPP font format,
// basically just a TIM with specialized
// tile metadata.
//

typedef struct Font Font;
typedef struct Font_TIM_Image_PixelInformation Font_TIM_Image_PixelInformation;
typedef struct Font_TIM_Image Font_TIM_Image;

struct Font_TIM_Image_PixelInformation {
  uint32_t length;

  //
  // for VRAM location placement
  //
  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;

  uint16_t* pixels;
};

//
// This could eventually become a common type if it's
// reused again, which might so we can avoid having more
// files on the filesystem.
//
struct Font_TIM_Image {
  uint8_t tag;
  uint8_t version;
  uint8_t _pad0;
  uint8_t _pad1;
  
  uint32_t flags;

  Font_TIM_Image_PixelInformation clut;
  Font_TIM_Image_PixelInformation image;
};

struct Font {
  int8_t		glyph_width;
  int8_t		glyph_height;
  int8_t		columns;
  int8_t		rows;
  uint8_t		glyphmap[256];
  Font_TIM_Image	tim;
};

//
// the font does not have ownership of the
// memory for this call.
//
// the intended usage is mostly to just load into VRAM as fast
// as possible and forget about these pointers.
//
Font      font_load_from_memory(uint8_t* data, uint8_t data_size);

TIM_IMAGE font_get_tim_info(Font* font);
Vector2   font_get_cstr_dimensions(Font* font, const char* text);

//
// These two APIs give enough information to use the font structure
// to render glyphs, however I deliberately try to avoid having direct
// drawing code here, so that a proper rendering module can take the most
// advantage of this.
//
// Since I consider this a type of resource so the code here should be minimal
// in my opinion.
//
void          font_set_vram_information(Font* font, Vector2 vram_location, Vector2 clut_location);

//
// font metrics glyph rectangle
//
Rectangle32   font_get_glyph_rect(Font* font, uint8_t character);

//
// font glyph in vram installed location based on the TIM fields
//
Rectangle32   font_get_glyph_rect_vram(Font* font, uint8_t character);

#endif
