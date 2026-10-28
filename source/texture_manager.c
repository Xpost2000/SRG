#include <psxgpu.h>
#include "filesystem.h"
#include "texture_manager.h"
#include "fileslist.g.h"

#define MAX_TEXTURES		(256)

//
// Matches the framebuffer allocation scheme,
// CLUT entries are packed under the two framebuffers.
//
#define VRAM_ALLOC_START_X	(320)
#define VRAM_ALLOC_START_Y	(0)
#define VRAM_ALLOC_END_X	(1024)
#define VRAM_ALLOC_END_Y	(512)
#define VRAM_CLUT_ALLOC_START_X (0)
#define VRAM_CLUT_ALLOC_START_Y (480)

static Vector2 g_vram_cursor = {};
static Vector2 g_vram_permenant_cursor = {};

static uint16_t		g_permenant_queue_cursor  = 0;
static uint16_t		g_queue_cursor		  = 0;
static int32_t		g_load_queue[MAX_TEXTURES];
static Texture_Source	g_texture_sources[MAX_TEXTURES];
static uint32_t		g_permenant_texture_count = 0;
static uint32_t		g_texture_count		  = 0;

static void _texture_manager_execute_load(Texture_Source* output_texture_source, int32_t texture_id);
static void _texture_manager_pack_image_data_into_vram(uint32_t* pixel_data, RECT* rectangle);
static void _texture_manager_pack_clut_data_into_vram(uint32_t* clut_pixel_data, RECT* clut_rectangle);

void texture_manager_clear_all(void)
{
  g_vram_cursor = g_vram_permenant_cursor;
  g_texture_count = 0;
}

Texture_Handle texture_manager_load_request(uint8_t load_priority, int32_t texture_id)
{
  int32_t		insert_index;
  Texture_Handle	result;

  switch (load_priority) {
  case TEXTURE_LOAD_PRIORITY_PERMENANT: {
    insert_index = g_permenant_queue_cursor++;
    result	 = g_permenant_texture_count++ + 1;
  } break;
  case TEXTURE_LOAD_PRIORITY_SCREEN: {
    insert_index = ((MAX_TEXTURES-1) - g_queue_cursor++);
    result	 = ((MAX_TEXTURES-1) - g_texture_count++)+1;
  } break;
  default: {
    assert(0 && "[TEXTURE-MANAGER] invalid texture priority");
    return 0;
  } break;
  }

  g_load_queue[insert_index] = texture_id;
  return result;
}

static void _texture_manager_pack_image_data_into_vram(uint32_t* pixel_data, RECT* rectangle)
{
  assert(0 && "[TEXTURE_MANAGER] TODO _texture_manager_pack_image_data_into_vram");
}

static void _texture_manager_pack_clut_data_into_vram(uint32_t* clut_pixel_data, RECT* clut_rectangle)
{
  assert(0 && "[TEXTURE_MANAGER] TODO _texture_manager_pack_clut_data_into_vram");
}

static void _texture_manager_execute_load(Texture_Source* output_texture_source, int32_t texture_id)
{
  File_Handle	texture_file;
  //
  // TODO(jerry):
  // use a shared scratch / per-frame arena to hold
  // this instead of a static allocation since this is 64KiB!
  //
  // Fortunately this is a stack variable.
  //
  // This is large enough to fit a 128x256 16bpp, 256*256 8bpp, 512*256 4bpp
  // which is hopefully good enough for everyone's needs.
  //
  uint8_t	texture_load_buffer[256*256];
  TIM_IMAGE	tim_image;
  int           load_result;

  texture_file = filesystem_open(file_strings_table[texture_id]);
  assert(texture_file && "[TEXTURE-MANAGER] failed to load texture file?");

  filesystem_read(texture_file, texture_load_buffer, sizeof(texture_load_buffer));
  filesystem_close(texture_file);

  load_result = GetTimInfo((uint32_t*) texture_load_buffer, &tim_image);
  assert((load_result == 0) && "[TEXTURE_MANAGER] GetTimInfo failed on this image.");
  
  _texture_manager_pack_image_data_into_vram(tim_image.paddr, tim_image.prect);
  _texture_manager_pack_clut_data_into_vram(tim_image.caddr, tim_image.crect);
}

void texture_manager_execute_all_loads(void)
{
  int i;
  Texture_Source* texture_source;

  //
  // NOTE(jerry):
  // at the current moment trying to load more permenant textures
  // is going to cause undefined behavior if you've already loaded
  // screen / non-permenant textures.
  //
  for (i = 0; i < g_permenant_queue_cursor; ++i) {
    texture_source = texture_manager_get_texture_source(i+1);
    _texture_manager_execute_load(texture_source, g_load_queue[i]);
  }
  g_vram_permenant_cursor = g_vram_cursor;

  for (i = 0; i < g_queue_cursor; ++i) {
    texture_source = texture_manager_get_texture_source((MAX_TEXTURES-1 - i) + 1);
    _texture_manager_execute_load(texture_source, g_load_queue[MAX_TEXTURES-1 - i]);
  }

  g_permenant_queue_cursor = 0;
  g_queue_cursor	   = 0;
}

Texture_Source* texture_manager_get_texture_source(Texture_Handle handle)
{
  int32_t index;

  if (handle != 0) {
    index = handle - 1;
    return g_texture_sources + index;
  }

  return NULL;
}
