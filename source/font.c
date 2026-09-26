#include "serializer.h"
#include "font.h"

static Font_TIM_Image_PixelInformation tim_pixel_information_load_from_serializer(Serializer* serializer)
{
  Font_TIM_Image_PixelInformation result;

  result.length = serializer_readu32(serializer);
  result.x	= serializer_readu16(serializer);
  result.y	= serializer_readu16(serializer);
  result.width	= serializer_readu16(serializer);
  result.height = serializer_readu16(serializer);
  result.pixels = (uint16_t*) serializer_read_bytes(serializer, sizeof(uint16_t) * result.width * result.height);

  _debugprintf("[FONT] tim.pixelinformation.length: %d (%x)", result.length, result.length);
  _debugprintf("[FONT] tim.pixelinformation.vramx:  %d (%x)", result.x, result.x);
  _debugprintf("[FONT] tim.pixelinformation.vramy:  %d (%x)", result.y, result.y);
  _debugprintf("[FONT] tim.pixelinformation.width:  %d (%x)", result.width, result.width);
  _debugprintf("[FONT] tim.pixelinformation.height: %d (%x)", result.height, result.height);

  return result;
}

static Font_TIM_Image tim_load_from_serializer(Serializer* serializer)
{
  Font_TIM_Image result = {};
  uint8_t _;

  UNUSED(_);

  result.tag	 = serializer_readu8(serializer);
  result.version = serializer_readu8(serializer);

  _ = serializer_readu8(serializer);
  _ = serializer_readu8(serializer);

  result.flags = serializer_readu32(serializer);

  _debugprintf("[FONT] tim.tag:      %d (%x)", result.tag, result.tag);
  _debugprintf("[FONT] tim.version:  %d (%x)", result.version, result.version);
  _debugprintf("[FONT] tim.flags:    %d (%x)", result.flags, result.flags);

  _debugprintf("[FONT] CLUT Pixel Information");
  result.clut  = tim_pixel_information_load_from_serializer(serializer);

  _debugprintf("[FONT] Image Pixel Information");
  result.image = tim_pixel_information_load_from_serializer(serializer);

  return result;
}

static Font font_load_from_serializer(Serializer* serializer)
{
  Font		result = {};

  result.glyph_width  = serializer_readu8(serializer);
  result.glyph_height = serializer_readu8(serializer);
  result.columns      = serializer_readi8(serializer);
  result.rows	      = serializer_readi8(serializer);

  serializer_read_into_bytes(serializer, result.glyphmap, sizeof(result.glyphmap));

  _debugprintf("[FONT] glyph_width:  %d (%x)", result.glyph_width, result.glyph_width);
  _debugprintf("[FONT] glyph_height: %d (%x)", result.glyph_height, result.glyph_height);
  _debugprintf("[FONT] columns:      %d (%x)", result.columns, result.columns);
  _debugprintf("[FONT] rows:         %d (%x)", result.rows, result.rows);

  result.tim = tim_load_from_serializer(serializer);

  return result;
}

Font font_load_from_memory(uint8_t* data, uint8_t data_size)
{
  Serializer serializer;

  serializer = serializer_from_memory(data, data_size);

  return font_load_from_serializer(&serializer);
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
