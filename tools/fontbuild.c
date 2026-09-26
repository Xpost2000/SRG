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

#if 0
fontbuild KageSans.png 9 9 fontmap.txt KageSans
fontbuild SaikyoSerif.png 9 9 fontmap2.txt SaikyoSerif
#endif

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
  uint16_t image_width;
  uint16_t image_height;

  uint8_t* image;
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
  // int row = tile/columns;
  // int col = tile%columns;
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
  if (color.a == 0) {
    return 0;
  }
  
  for (i = 0; i < g_palette_used; ++i) {
    if (color.data_u32 == g_palette[i].data_u32) {
      return i;
    }
  }

  return -1;
}

static void write_tim_to_FILE(TIM_Image* timimage, FILE* f)
{
  fwrite(&timimage->tag, sizeof(uint8_t), 1, f);
  fwrite(&timimage->version, sizeof(uint8_t), 1, f);
  fwrite(&timimage->_pad0, sizeof(uint8_t), 1, f);
  fwrite(&timimage->_pad1, sizeof(uint8_t), 1, f);

  fwrite(&timimage->flags, sizeof(uint32_t), 1, f);

  fwrite(&timimage->clut_length, sizeof(uint32_t), 1, f);
  fwrite(&timimage->clut_x, sizeof(uint16_t), 1, f);
  fwrite(&timimage->clut_y, sizeof(uint16_t), 1, f);
  fwrite(&timimage->clut_width, sizeof(uint16_t), 1, f);
  fwrite(&timimage->clut_height, sizeof(uint16_t), 1, f);
  fwrite(timimage->clut, sizeof(uint16_t) * timimage->clut_width * timimage->clut_height, 1, f);

  fwrite(&timimage->image_length, sizeof(uint32_t), 1, f);
  fwrite(&timimage->image_x, sizeof(uint16_t), 1, f);
  fwrite(&timimage->image_y, sizeof(uint16_t), 1, f);
  fwrite(&timimage->image_width, sizeof(uint16_t), 1, f);
  fwrite(&timimage->image_height, sizeof(uint16_t), 1, f);
  fwrite(timimage->image, sizeof(uint16_t) * timimage->image_width * timimage->image_height, 1, f);
}

static uint8_t* read_entire_file(char* filename)
{
  FILE* f;
  uint8_t* result;
  size_t length;


  f = fopen(filename, "rb+");

  fseek(f, 0, SEEK_END);
  length = ftell(f);
  fseek(f, 0, SEEK_SET);

  result = malloc(length+1);
  fread(result, 1, length, f);
  printf("%s (%d)\n", result, length);
  result[length] = 0;

  return result;
}

static void output_tim(TIM_Image* timimage, char* filebasename)
{
  char tmp[256];
  FILE* f;

  snprintf(tmp, 256, "%s.tim", filebasename);
  f = fopen(tmp, "wb+");

  write_tim_to_FILE(timimage, f);

  fclose(f);
}

static void output_srgfont(SRG_Font* font, char* filebasename)
{
  char tmp[256];
  FILE* f;

  snprintf(tmp, 256, "%s.srgfnt", filebasename);
  f = fopen(tmp, "wb+");

  fwrite(&font->glyph_width, sizeof(int8_t), 1, f);
  fwrite(&font->glyph_height, sizeof(int8_t), 1, f);
  fwrite(&font->columns, sizeof(int8_t), 1, f);
  fwrite(&font->rows, sizeof(int8_t), 1, f);
  fwrite(&font->glyphmap, sizeof(font->glyphmap), 1, f);

  write_tim_to_FILE(&font->image_contents, f);

  fclose(f);
}


void insert_palette_color(Color32u8 color)
{
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
  int           original_rows, original_cols;
  int           rows, cols;

  //
  // as 4bpp
  //
  int           tim_width, tim_height;
  int           tim_length;

  uint16_t* clut_data;
  uint8_t* tim_image_data;

  memset(&srgfont, 0, sizeof(srgfont));

  if (argc != 6) {
    fprintf(stderr, "usage:\n%s <inputimg> <glyphwidth> <glyphheight> <glyphmapfile>  <outputfile>", argv[0]);
    exit(-1);
  }

  input_image_filename	= argv[1];
  glyph_width_str	= argv[2];
  glyph_height_str	= argv[3];
  //
  // The glyph map file is just a string of the characters as they
  // appear within the font file in order 
  //
  glyph_map_filename    = argv[4];
  output_image_filename = argv[5];

  glyph_width		= atoi(glyph_width_str);
  glyph_height		= atoi(glyph_height_str);

  printf("input image: \"%s\"\n", input_image_filename);
  printf("output image: \"%s\"\n", output_image_filename);
  printf("glyph width: %s\n", glyph_width_str);
  printf("glyph height: %s\n", glyph_height_str);
  printf("glyph map filename: %s\n", glyph_map_filename);

  srgfont.glyph_width = glyph_width;
  srgfont.glyph_height = glyph_height;

  srgfont.image_contents.tag = 0x10;
  srgfont.image_contents.version = 0;
  srgfont.image_contents.flags = 0x0 | (1 << 3);

  image_pixel_data = stbi_load(input_image_filename, &image_width, &image_height, &image_channels, 4);
  printf("source image is %dx%d pixels\n", image_width, image_height);

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

    clut_data = (uint16_t*) malloc(sizeof(uint16_t) * 16);

    //
    // set all initial colors transparent
    //
    for (i = 0; i < 16; ++i) {
      clut_data[i] = color32u8_to_5551((Color32u8){0,0,0,0});
    }

    for (i = 0; i < g_palette_used; ++i) {
      clut_data[i] = color32u8_to_5551(g_palette[i]);
    }

    //
    // Includes the size of the header as well contents of the clut.
    //
    srgfont.image_contents.clut_length = sizeof(uint32_t) + sizeof(uint16_t) * 4 + sizeof(uint16_t)*16;
    srgfont.image_contents.clut_width = 16;
    srgfont.image_contents.clut_height = 1;
    srgfont.image_contents.clut = (uint16_t*)clut_data;
    printf("CLUT Length will be: %d\n", srgfont.image_contents.clut_length);
  }

  printf("Packing tpage atlas\n");
  {
    int cursor_x = 0;
    rows	 = 1;
    cols	 = 0;

    original_rows = image_height / glyph_height;
    original_cols = image_width / glyph_width;

    int tiles_to_pack = original_rows * original_cols;

    printf("original image based on given glyph dimensions was rows x cols, %d x %d = %d tiles to pack\n", original_rows, original_cols, tiles_to_pack);
    cols = TPAGE_PIXEL_WIDTH / glyph_width; 

    while (tiles_to_pack) {
      if (cursor_x+glyph_width >= TPAGE_PIXEL_WIDTH) {
	cursor_x = 0;
	rows++;
      } else {
	cursor_x += glyph_width;
      }

      --tiles_to_pack;
    }

    tim_width = cols * glyph_width;
    tim_height = rows * glyph_height;

    printf("pack estimation says: %d rows, %d (max) columns, %d x %d px 4bpp\n", rows, cols, tim_width, tim_height);
  }

  printf("Constructing indexed image\n");
  {
    int cursor_x      = 0;
    int cursor_y      = 0;
    int tiles_to_pack = original_rows * original_cols;
    int tile_index    = 0;

    tim_length = (cols * glyph_width/2) * (rows * glyph_height);
    tim_image_data = (uint8_t*) malloc(tim_length);
    memset(tim_image_data, 0, tim_length);
    printf("tim image region is: %d bytes long\n", tim_length);

    //
    // same loop as before but we are in packing mode.
    //
    for (; tile_index < tiles_to_pack; ++tile_index) {
      if (cursor_x+glyph_width >= TPAGE_PIXEL_WIDTH) {
	cursor_x = 0;
	cursor_y += glyph_height;
      }

      //
      // copy into image data from the original image
      //
      {
	int i;
	int j;

	int row_offset = tile_index / original_cols;
	int col_offset = tile_index % original_cols;

	for (i = 0; i < glyph_height; ++i) {
	  for (j = 0; j < glyph_width; ++j) {
	    Color32u8 original_pixel;
	    int       color_index;

	    original_pixel.r = image_pixel_data[((i + (row_offset * glyph_height)) * (image_width * 4) + (j + (col_offset * glyph_width)) * 4) + 0];
	    original_pixel.g = image_pixel_data[((i + (row_offset * glyph_height)) * (image_width * 4) + (j + (col_offset * glyph_width)) * 4) + 1];
	    original_pixel.b = image_pixel_data[((i + (row_offset * glyph_height)) * (image_width * 4) + (j + (col_offset * glyph_width)) * 4) + 2];
	    original_pixel.a = image_pixel_data[((i + (row_offset * glyph_height)) * (image_width * 4) + (j + (col_offset * glyph_width)) * 4) + 3];

	    //
	    // 4 bit index
	    //
	    color_index = get_color_index_from_palette(original_pixel) & 0xf;

	    if (((cursor_x + j) % 2) == 0) {
	      //
	      // even pixel offset means writing into the other nibble.
	      //
	      tim_image_data[(cursor_y + i) * (tim_width/2) + ((cursor_x + j) / 2)] |= color_index;
	    } else {
	      tim_image_data[(cursor_y + i) * (tim_width/2) + ((cursor_x + j) / 2)] |= color_index << 4;
	    }
	  }
	}
      }

      cursor_x += glyph_width;
    }
  }

  printf("Outputting TIM (for debugging) (tim data length: %d)\n", tim_length);
  {
    srgfont.image_contents.image_length = sizeof(uint32_t)*1 + sizeof(uint16_t) * 4 + tim_length;
    srgfont.image_contents.image_width = tim_width/4;
    srgfont.image_contents.image_height = tim_height;
    srgfont.image_contents.image = tim_image_data;
    printf("%d x %d\n", srgfont.image_contents.image_width, srgfont.image_contents.image_height);

    output_tim(&srgfont.image_contents, output_image_filename);
  }

  printf("Reading glyphmap and doing bindings\n");
  {
    uint8_t* glyph_map_string;
    int i;
    int slen;

    glyph_map_string = read_entire_file(glyph_map_filename);
    slen = strlen((const char*) glyph_map_string);

    printf("glyphmap is %d characters long.\n");
    memset(srgfont.glyphmap, -1, sizeof(srgfont.glyphmap));

    for (i = 0; i < slen; ++i) {
      unsigned c = glyph_map_string[i];

      //
      // for space you can just advance by glyph_width and
      // it is basically *not* noticable, so it's gonna be the "free"
      // character for if there is any padding.
      //
      if (c != ' ') {
	srgfont.glyphmap[c] = i;
	printf("glyphmap[%c] = tile %d\n", c, i);
      } else {
	srgfont.glyphmap[c] = (uint8_t) -1; // wrap around.
	printf("glyphmap[%c] = n/a\n", c);
     }
    }

    //
    // special case alphabetical characters
    //
    // usually font sets will have one or the other
    // so to allow strings to work for both, just remap them
    // to each other.
    //
    for (i = 0; i < 26; ++i) {
      if (srgfont.glyphmap[i+'a'] == (uint8_t) -1) {
	srgfont.glyphmap[i+'a'] = srgfont.glyphmap[i+'A'];
      } else if (srgfont.glyphmap[i+'A'] == (uint8_t) -1) {
	srgfont.glyphmap[i+'A'] = srgfont.glyphmap[i+'a'];
      }
    }
  }

  printf("Outputting SRGFONT\n");
  {
    srgfont.rows = rows;
    srgfont.columns = cols;
    srgfont.glyph_width = glyph_width;
    srgfont.glyph_height = glyph_height;

    output_srgfont(&srgfont, output_image_filename);

    printf("outputted srg font to: %s.srgfnt\n", output_image_filename);
    {
      int i;

      printf("glyph_width: %d\n", srgfont.glyph_width);
      printf("glyph_height: %d\n", srgfont.glyph_height);
      printf("columns: %d\n", srgfont.columns);
      printf("rows: %d\n", srgfont.rows);
      printf("--- glyphmap\n");
      for (i = 0; i < 256; ++i) {
	unsigned c = i;
	unsigned v = srgfont.glyphmap[i];

	//
	// for space you can just advance by glyph_width and
	// it is basically *not* noticable, so it's gonna be the "free"
	// character for if there is any padding.
	//
	if (c != ' ' && v != (uint8_t)-1) {
	  printf("glyphmap[%d(%c)] = tile %d\n", c, c, v);
	}
      }
      printf("--- end glyphmap\n");
      printf("tim.image_length: %d\n", srgfont.image_contents.image_length);
      printf("tim.image_width(16bpp): %d (actual): %d\n", srgfont.image_contents.image_width, srgfont.image_contents.image_width*4);
      printf("tim.image_height(16bpp): %d\n", srgfont.image_contents.image_height);
    }
  }

  return 0;
}
