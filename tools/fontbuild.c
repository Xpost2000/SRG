//
// This is a small and highly specific CLI tool that
// takes fixed size fonts (for now they should
// be known to be tightly packed together with 0 padding on any end
// {super easy to retrofit to add more, but it's not really necessary})
//
// and then it will repack it into some image (that will likely fit
// within a TPAGE)
//
//
// Eventually, we *could* replace this with a more complex tool, but
// font decisions can probably be made super early on tbh, and so that's
// why I don't think this tool should have a super high investment as we
// are probably not making / generating new fonts all the time.
//
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

//
// These are in 4BPP units.
//
#define TPAGE_PIXEL_WIDTH  (256)
#define TPAGE_PIXEL_HEIGHT (256)

typedef struct SRG_Font SRG_Font;
typedef struct TIM_Image TIM_Image;
typedef struct Color32u8 Color32u8;

struct Color32u8 {
  union {
    struct {
      uint8_t r;
      uint8_t g;
      uint8_t b;
      uint8_t a;
    };
    uint8_t data[4];
    uint32_t data_u32;
  };
};

struct TIM_Image {
  //
  // Header bytes
  //
  uint8_t tag; // 0x10
  uint8_t version;
  uint8_t _pad0;
  uint8_t _pad1;

  //
  // 2 bits for BPP
  //
  // 2 bits for Color Lookup table flag
  //
  uint32_t flags;

  uint32_t clut_length;
  uint16_t clut_x;
  uint16_t clut_y;
  uint16_t clut_width;
  uint16_t clut_height;

  uint16_t* clut;

  uint32_t image_length;
  uint16_t image_x;
  uint16_t image_y;

  // in units of 16 bpp
  // 1 unit here = 4 pixels for 4bpp
  uint32_t image_width;
  uint32_t image_height;
};

//
// engine will link with PSN00B and
// so the engine will use a slightly different
// version of this data type.
//
struct SRG_Font {
  int8_t glyph_width;
  int8_t glyph_height;
  int8_t columns; 
  int8_t rows;

  //
  // index by ascii char, output is the tile,
  // can find row and col easily from there.
  //
  // int row = tile/rows;
  // int col = tile%cols;
  //
  uint8_t glyphmap[256];

  TIM_Image image_contents;
};

//
// All finalized images are 4bpp
//
int       g_palette_used = 1; // the first color is always going to be the transparent chroma key.
Color32u8 g_palette[16];

uint16_t color32u8_to_5551(Color32u8 color)
{
  uint16_t result = (color.r >> 3) |
		    (color.g >> 3) << 5 |
		    (color.b >> 3) << 10;
  if (color.a == 0) {
    result |= 1 << 15;
  }

  return result;
}

int get_color_index_from_palette(Color32u8 color)
{
  int i;
  
  //
  // Any transparent color will be
  // using index 0 which we will always consider transparent.
  //
  if (color.a == 0)
    return 0;
  
  for (i = 0; i < g_palette_used; ++i) {
    if (color.data_u32 == g_palette[i].data_u32) {
      return i;
    }
  }

  return -1;
}

void insert_palette_color(Color32u8 color)
{
  int i;

  if (get_color_index_from_palette(color) == -1) {
    assert(g_palette_used < 16 && "insert_palette_color has ran out of colors, the supplied image has too many colors.");
    g_palette[g_palette_used++] = color;
  }
}

int main(int argc, char** argv)
{
  const char*	input_image_filename;
  const char*	output_image_filename;
  const char*   glyph_width_str;
  const char*   glyph_height_str;
  const char*   glyph_map_filename;

  int           glyph_width;
  int           glyph_height;

  SRG_Font	srgfont;
  uint8_t*	image_pixel_data;
  int		image_width, image_height, image_channels;

  uint8_t* clut_data;
  uint8_t* tim_image_data;

  if (argc != 6) {
    fprintf(stderr, "usage:\n%s <inputimg> <glyphwidth> <glyphheight> <glyphmapfile>  <outputfile>", argv[0]);
    exit(-1);
  }

  input_image_filename	= argv[1];
  output_image_filename = argv[2];
  glyph_width_str	= argv[3];
  glyph_height_str	= argv[4];
  glyph_width		= atoi(glyph_width_str);
  glyph_height		= atoi(glyph_height_str);

  //
  // The glyph map file is just a string of the characters as they
  // appear within the font file in order 
  //
  glyph_map_filename    = argv[5];

  printf("input image: \"%s\"\n", input_image_filename);
  printf("output image: \"%s\"\n", output_image_filename);
  printf("glyph width: %s\n", glyph_width_str);
  printf("glyph height: %s\n", glyph_height_str);
  printf("glyph map filename: %s\n", glyph_map_filename);

  srgfont.glyph_width = glyph_width;
  srgfont.glyph_height = glyph_height;

  srgfont.image_contents.tag = 0x10;
  srgfont.image_contents.version = 0;
  srgfont.image_contents.flags = 0x0 | (1 << 4);

  image_pixel_data = stbi_load(input_image_filename, &image_width, &image_height, &image_channels, 4);

  printf("Building color map / palette.\n");
  {
    int i;

    for (i = 0; i < image_width * image_height; ++i) {
      Color32u8 c;

      c.r = image_pixel_data[i * 4+0];
      c.g = image_pixel_data[i * 4+1]; 
      c.b = image_pixel_data[i * 4+2]; 
      c.a = image_pixel_data[i * 4+3]; 

      insert_palette_color(c);
    }

    printf("color packing found: %d unique colors\n", g_palette_used);

    printf("allocating CLUT for 4bpp image.\n");

    clut_data = (uint8_t*) malloc(sizeof(uint16_t) * 16);

    memset(clut_data, 0, sizeof(uint16_t) * 16);

    for (i = 0; i < g_palette_used; ++i) {
      clut_data[i] = color32u8_to_5551(g_palette[i]);
    }

    //
    // Includes the size of the header as well contents of the clut.
    //
    srgfont.image_contents.clut_length = sizeof(uint32_t) + sizeof(uint16_t) * 4 + sizeof(uint16_t)*16;
  }

  printf("Packing tpage atlas\n");
  {
    int cursor_x = 0;
    int cursor_y = 0;
    int rows     = 0;
    int cols     = 0;

    int original_rows = image_height / glyph_height;
    int original_cols = image_width / glyph_width;
    printf("original image based on given glyph dimensions was rows x cols, %d x %d\n", original_rows, original_cols);

  }

  printf("Constructing indexed image\n");
  {
    
  }

  printf("Outputting TIM (for debugging)\n");
  {
    
  }

  printf("Outputting SRGFONT\n");
  {
    
  }

  return 0;
}
