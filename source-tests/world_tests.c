//
// This tests the serialization path and
// iterator primarily, for the world format
// code.
//
// But will also test the other logical parts of the system's API.
//
// Only really care about testing the happy-path
// since we should never get unformatted data in-engine.
//

#include "common.h"
#include "memory_arena.h"
#include "world.h"

#include <stdio.h>
#include <stdlib.h>

uint8_t g_memory_buffer[Kilobyte(100)];
Memory_Arena* arena;

//
// NOTE(jerry):
// this is unhygenic as fuck
//
#define _new_entity_preamble(T, E)                              \
  T* entity;                                                    \
  size_t                     size;                              \
  size = sizeof(T);                                             \
  *entity_count++;                                              \
  *total_size += size;                                          \
  entity = (T*) memory_arena_push_unaligned(arena, size);       \
  entity->base.type       = E;                                  \
  entity->base.size       = size;

void add_player_spawn(
  uint32_t*     entity_count,
  uint32_t*     total_size,
  int32_t       x,
  int32_t       y,
  int16_t       look_angle)
{
  _new_entity_preamble(World_Entity_Player_Spawn, WORLD_ENTITY_TYPE_PLAYER_SPAWN);
  entity->x          = x;
  entity->y          = y;
  entity->look_angle = look_angle;
}

void add_object_prop(
  uint32_t* entity_count,
  uint32_t* total_size,
  int32_t   x,
  int32_t   y,
  uint32_t  asset_id,
  uint32_t  flags,
  int16_t   angle)
{
  _new_entity_preamble(World_Entity_Object_Prop, WORLD_ENTITY_TYPE_OBJECT_PROP);
  entity->x        = x;
  entity->y        = y;
  entity->asset_id = asset_id;
  entity->flags    = flags;
  entity->angle    = angle;
}

void add_stage_end_region(
  uint32_t* entity_count,
  uint32_t* total_size,
  int32_t   x,
  int32_t   y,
  int32_t   w,
  int32_t   h,
  int32_t   next_level_id)
{
  _new_entity_preamble(World_Entity_Stage_End_Region, WORLD_ENTITY_TYPE_STAGE_END_REGION);
  entity->x = x;
  entity->y = y;
  entity->w = w;
  entity->h = h;
  entity->next_level_id = next_level_id;
}

#undef _new_entity_preamble

//
// Generates a raw memory buffer which should
// qualify for a world.
//
// Basically the same as if you had loaded from
// a file. Just skipping the file part.
//
uint8_t* setup_simple_world_for_test(void)
{
  World* as_world;
  uint32_t total_size;
  uint32_t size;
  uint32_t entity_count;
  uint8_t* result;

  //
  // Hard-coded world for serialization testing.
  //
  uint8_t world[8*8] = {
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 0, 0, 1, 1, 1,
    1, 1, 1, 0, 0, 1, 1, 1,
    1, 1, 1, 0, 0, 1, 1, 1,
    1, 1, 1, 0, 0, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 1, 1, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
  };

  result = memory_arena_push_unaligned(arena, sizeof(World));
  as_world = (World*) result;
  as_world->version        = WORLD_CURRENT_VERSION;

  size       = sizeof(*as_world);
  total_size = sizeof(*as_world);

  as_world->world_w        = 8;
  as_world->world_h        = 8;

  //
  // "allocate" the world
  //
  {
    uint8_t* ptr;
    total_size += sizeof(world);
    size       += sizeof(world);
    ptr         = memory_arena_push_unaligned(arena, sizeof(world));
    memcpy(ptr, world, sizeof(world));
  }

  //
  // allocate test entities
  //
  add_player_spawn(&entity_count, &total_size, 4096 * 4, 4096 * 4, 1024);
  
  as_world->size           = size;
  as_world->total_size     = total_size;
  as_world->entities_count = entity_count;
  return result;
}

void test_world_serialization_and_iteration_usage_scenario(void)
{
  setup_simple_world_for_test();
}

int main(void)
{
  arena = memory_arena_create("test-arena", g_memory_buffer, sizeof(g_memory_buffer));

  test_world_serialization_and_iteration_usage_scenario();

  return 0;
}
