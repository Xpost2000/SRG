#include "font.h"

static Font_TIM_Image_PixelInformation tim_pixel_information_load_from_memory(uint8_t* data, uint8_t data_size)
{
  Font_TIM_Image_PixelInformation result;
  uint8_t*       data_start = data;

  result.length = *(uint32_t*) data;
  data += sizeof(uint32_t);

  result.x = *(uint16_t*) data;
  data += sizeof(uint16_t);

  result.y = *(uint16_t*) data;
  data += sizeof(uint16_t);

  result.width = *(uint16_t*) data;
  data += sizeof(uint16_t);

  result.height = *(uint16_t*) data;
  data += sizeof(uint16_t);

  result.pixels = (uint16_t*) data;
  data += sizeof(uint16_t) * result.width * result.height;

  assert(((data - data_start) > data_size) && "[FONT] cursor exceeded specified data_size.");
  return result;
}

static Font_TIM_Image tim_load_from_memory(uint8_t* data, uint8_t data_size)
{
  Font_TIM_Image result = {};
  uint8_t*       data_start = data;

  result.tag = *(uint8_t*)data;
  data += sizeof(uint8_t);

  result.version = *(uint8_t*)data;
  data += sizeof(uint8_t);

  //
  // _pad0
  // _pad1
  //
  data += sizeof(uint8_t);
  data += sizeof(uint8_t);

  result.flags = *(uint32_t*)data;
  data += sizeof(uint32_t);

  tim_pixel_information_load_from_memory(data, data_size - (data - data_start));
  tim_pixel_information_load_from_memory(data, data_size - (data - data_start));

  assert(((data - data_start) > data_size) && "[FONT] cursor exceeded specified data_size.");
  return result;
}

Font font_load_from_memory(uint8_t* data, uint8_t data_size)
{
  Font		result = {};
  uint8_t*      data_start = data;

  result.glyph_width = *(int8_t*)data;
  data += sizeof(int8_t);

  result.glyph_height = *(int8_t*)data;
  data += sizeof(int8_t);

  result.columns = *(int8_t*)data;
  data += sizeof(int8_t);

  result.rows = *(int8_t*)data;
  data += sizeof(int8_t);

  memcpy(result.glyphmap, data, sizeof(result.glyphmap));
  data += sizeof(uint8_t) * 256;

  result.tim = tim_load_from_memory(data, data_size - (data - data_start));

  assert(((data - data_start) > data_size) && "[FONT] cursor exceeded specified data_size.");
  return result;
}

TIM_IMAGE font_get_tim_info(Font* font)
{
  TIM_IMAGE result;

  result.mode = font->tim.flags;

  result.crect = (RECT*) &font->tim.clut.x;
  result.caddr = (uint32_t*) font->tim.clut.pixels;

  result.prect = (RECT*) &font->tim.image.x;
  result.paddr = (uint32_t*) font->tim.image.pixels;

  return result;
}

Vector2 font_get_cstr_dimensions(Font* font, const char* text)
{
  Vector2 result;
  int cursor_x = 0;

  int i;
  int string_length = strlen(text);

  result.x = 0;
  result.y = 0;


  for (i = 0; i < string_length; ++i) {
    char ch;

    ch = text[i];

    if (ch == '\n') {
      result.x = max(cursor_x, result.x);
      result.y += font->glyph_height;
      cursor_x = 0;
    } else {
      cursor_x += font->glyph_width;
    }
  }

  return result;
}

void font_set_vram_information(Font* font, Vector2 vram_location, Vector2 clut_location)
{
  font->tim.clut.x  = clut_location.x;
  font->tim.clut.y  = clut_location.y;
  font->tim.image.x = vram_location.x;
  font->tim.image.y = vram_location.y;
}

Rectangle32 font_get_glyph_rect(Font* font, uint8_t character)
{
  Rectangle32	result;
  uint8_t	tileid;
  uint8_t	tile_column;
  uint8_t	tile_row;

  tileid      = font->glyphmap[character];
  tile_column = tileid % font->columns;
  tile_row    = tileid / font->columns;

  result.x = font->glyph_width * tile_column;
  result.y = font->glyph_height * tile_row;
  result.w = font->glyph_width;
  result.h = font->glyph_height;

  return result;
}

Rectangle32 font_get_glyph_rect_vram(Font* font, uint8_t character)
{
  Rectangle32 glyph_rect;

  glyph_rect	= font_get_glyph_rect(font, character);
  glyph_rect.x += font->tim.image.x;
  glyph_rect.y += font->tim.image.y;

  return glyph_rect;
}
