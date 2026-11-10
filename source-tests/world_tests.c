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

#undef RELEASE

#include "common.h"
#include "memory_arena.h"
#include "world.h"

#include <stdio.h>
#include <stdlib.h>

uint8_t g_memory_buffer[Kilobyte(100)];
Memory_Arena* arena;

//
// Hard-coded world for serialization testing.
//
uint8_t testworld1[8*8] = {
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 0, 0, 1, 1, 1,
  1, 1, 1, 0, 0, 1, 1, 1,
  1, 1, 1, 0, 0, 1, 1, 1,
  1, 1, 1, 0, 0, 1, 1, 1,
  1, 0, 0, 0, 0, 0, 0, 1,
  1, 0, 0, 1, 1, 0, 0, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
};


//
// NOTE(jerry):
// this is unhygenic as fuck
//
#define _new_entity_preamble(T, E)                              \
  T* entity;                                                    \
  size_t                     size;                              \
  size = sizeof(T);                                             \
  (*entity_count)++;                                            \
  (*total_size) += size;                                                    \
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
uint8_t* setup_simple_world_for_test(size_t* out_size, uint8_t* world, int32_t world_w, int32_t world_h)
{
  World* as_world;
  uint32_t total_size;
  uint32_t size;
  uint32_t entity_count;
  uint8_t* result;
  uint8_t* end_alloc;
  size_t   world_byte_size;

  world_byte_size = sizeof(*world) * world_w * world_h;

  result = memory_arena_push_unaligned(arena, sizeof(*as_world));
  as_world = (World*) result;
  entity_count = 0;
  as_world->version        = WORLD_CURRENT_VERSION;

  size       = sizeof(*as_world);
  total_size = sizeof(*as_world);

  as_world->world_w        = world_w;
  as_world->world_h        = world_h;

  //
  // "allocate" the world
  //
  {
    uint8_t* ptr;
    total_size += world_byte_size;
    size       += world_byte_size;
    ptr         = memory_arena_push_unaligned(arena, world_byte_size);
    memcpy(ptr, world, world_byte_size);
  }

  //
  // allocate test entities
  //
  add_player_spawn(&entity_count, &total_size, 4096 * 4, 4096 * 4, 1024);
  
  as_world->size           = size;
  as_world->total_size     = total_size;
  as_world->entities_count = entity_count;
  end_alloc = memory_arena_push_unaligned(arena, 0);
  *out_size = (end_alloc - result);

  return result;
}

void assert_tile_is_solid(World_Tile t)
{
  fprintf(stderr, "Testing tile at (%d, %d, %d flags).\n", t.x, t.y, t.flags);
  assert(((t.flags & WORLD_TILE_FLAG_SOLID)>0) && "A tile that should be solid is not!");
}

void assert_tile_is_not_solid(World_Tile t)
{
  fprintf(stderr, "Testing tile at (%d, %d, %d flags).\n", t.x, t.y, t.flags);
  assert(!((t.flags & WORLD_TILE_FLAG_SOLID)>0) && "A tile that should be not solid is not!");
}

//
// ATM cannot validate same width / height
// can only validate the slice that's in the world.
//
void assert_world_tiles_properties_are_correct(World* world)
{
  int        y;
  int        x;
  int        real_x;
  int        real_y;
  World_Tile current_tile;

  for (y = 0; y < world->world_h; ++y) {
    for (x = 0; x < world->world_w; ++x) {
      real_x = x * WORLD_GRID_UNIT_ONE;
      real_y = y * WORLD_GRID_UNIT_ONE;
      current_tile = world_get_tile_at(world, real_x, real_y);

      if (world->data[y*world->world_w+x] > 0) {
        assert_tile_is_solid(current_tile);
      } else {
        assert_tile_is_not_solid(current_tile);
      }

      assert(x == current_tile.x && "The tile coordinate does not appear correct");
      assert(y == current_tile.y && "The tile coordinate does not appear correct");
    }
  }
  
}

void test_entity_iteration(World* world, Serializer* serializer)
{
  World_Entity_Iterator iterator;

  for (iterator = world_entity_iterator_begin(world, serializer);
       !world_entity_iterator_complete(&iterator);
       world_entity_iterator_advance(&iterator)) {
    _debug_world_entity_printout(iterator.entity);
  }

  fprintf(stderr, "world->entities_count = %d vs. iterator.read_count = %d\n",
          world->entities_count, iterator.read_count);
  assert(world->entities_count == iterator.read_count && "Iterator terminated prematurely or otherwise had a wrong count.");
}

void test_world_serialization_and_iteration_usage_scenario(void)
{
  Serializer serializer;
  World*     world;
  uint8_t*   world_buffer;
  size_t     world_buffer_size;

  world_buffer = setup_simple_world_for_test(&world_buffer_size, testworld1, 8, 8);
  fprintf(stderr, "world size: %lld\n", world_buffer_size);

  serializer   = serializer_from_memory(world_buffer, world_buffer_size);

  world = world_read_from_serializer(arena, &serializer);

  assert((world->version == WORLD_CURRENT_VERSION) && "world version header is not correct?");
  assert((world->world_w == 8) && "simple world width is not correct");
  assert((world->world_h == 8) && "simple world height is not correct");

  //
  // for the simple world. I know I have this many entities so.
  //
  assert(world->entities_count == 1 && "simple world entity count is wrong");

  assert_world_tiles_properties_are_correct(world);

  test_entity_iteration(world, &serializer);
}

int main(void)
{
  arena = memory_arena_create("test-arena", g_memory_buffer, sizeof(g_memory_buffer));

  test_world_serialization_and_iteration_usage_scenario();

  return 0;
}
