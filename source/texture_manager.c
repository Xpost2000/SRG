#include "texture_manager.h"
#include "fileslist.g.h"

#define MAX_TEXTURES (256)

static Vector2 g_vram_cursor = {};
static Vector2 g_vram_permenant_cursor = {};

static uint16_t g_permenant_queue_cursor = 0;
static uint16_t g_queue_cursor = 0;
static int32_t  g_load_queue[MAX_TEXTURES];

void texture_manager_clear_all(void)
{
  
}

Texture_Handle texture_manager_load_request(uint8_t load_priority, int32_t texture_id)
{
  
}

Texture_Handle texture_manager_execute_all_loads(void)
{
  
}

Texture_Source* texture_manager_get_texture_source(Texture_Handle handle)
{
  
}
