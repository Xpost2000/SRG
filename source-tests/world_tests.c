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
#include "world_builder.h"

#include <stdio.h>
#include <stdlib.h>

uint8_t g_memory_buffer[Kilobyte(100)];
Memory_Arena* arena;

World_Builder* world_builder;

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

World_Builder* make_test_world(uint8_t* world, int32_t world_w, int32_t world_h)
{
  World_Builder* world_builder = world_builder_create(arena);

  world_builder_set_world_tile_data(world_builder, world_w, world_h, world);

  //
  // allocate test entities
  //
  world_builder_add_player_spawn(world_builder, WORLD_GRID_UNIT_ONE * 4, WORLD_GRID_UNIT_ONE * 4, 1024);
  world_builder_add_object_prop(world_builder, WORLD_GRID_UNIT_ONE * 4, WORLD_GRID_UNIT_ONE * 9, 2, 0x0, 2048);
  world_builder_add_object_prop(world_builder, WORLD_GRID_UNIT_ONE * 4, WORLD_GRID_UNIT_ONE * 2, 2, 0x0, 2048);
  world_builder_add_object_prop(world_builder, WORLD_GRID_UNIT_ONE * 9, WORLD_GRID_UNIT_ONE * 2, 2, 0x0, 2048);
  world_builder_add_stage_end_region(world_builder, WORLD_GRID_UNIT_ONE * 7, WORLD_GRID_UNIT_ONE * 9, WORLD_GRID_UNIT_ONE * 10, WORLD_GRID_UNIT_ONE * 10, -1);

  return world_builder;
}

uint8_t* setup_simple_world_for_test(size_t* out_size, uint8_t* world, int32_t world_w, int32_t world_h)
{
  uint8_t* result;

  world_builder = make_test_world(world, world_w, world_h);
  *out_size = world_builder_get_memory_required(world_builder);

  {
    int i;
    for (i = 0; i < world_builder->entity_count; ++i) {
      _debugprintf("e[i] = %p (%d)", world_builder->entities[i], world_builder->entities[i]->size);
    }
  }

  _debugprintf("out_size = %d", *out_size);
  result = memory_arena_push_unaligned(arena, *out_size);
  _debugprintf("Try to build world in memory");
  world_builder_build_world_in_memory(world_builder, result, *out_size);
  _debugprintf("Finished world build in memory");

  return result;
}

void assert_tile_is_solid(World_Tile t)
{
  _debugprintf( "Testing tile at (%d, %d, %d flags).\n", t.x, t.y, t.flags);
  assert(((t.flags & WORLD_TILE_FLAG_SOLID)>0) && "A tile that should be solid is not!");
}

void assert_tile_is_not_solid(World_Tile t)
{
  _debugprintf( "Testing tile at (%d, %d, %d flags).\n", t.x, t.y, t.flags);
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
  int i = 0;

  for (iterator = world_entity_iterator_begin(world, serializer);
       !world_entity_iterator_complete(&iterator);
       ++i, world_entity_iterator_advance(&iterator)) {
    //
    // NOTE(jerry):
    // memcmp is normally not a good idea because of padding bytes causing things to break
    // however in this case, it's fine because I'm trying to compare essentially the same
    // memory regions.
    //
    // this is mainly exercising whether the packing works correctly since the entities are variably
    // sized and not uniformly sized to make the level files smaller, and also that they are not loaded
    // into memory, but "technically" streamed-in, and so it is possible to make a mistake doing this.
    //
    _debugprintf("from iterator");
    _debug_world_entity_printout(iterator.entity);
    _debugprintf("from source (world builder)");
    _debug_world_entity_printout(world_builder->entities[i]);
    assert((memcmp(iterator.entity, world_builder->entities[i], iterator.entity->size) == 0)
           && "iterator failed to read out entity correctly");
  }

  _debugprintf( "world->entities_count = %d vs. iterator.read_count = %d\n",
          world->entities_count, iterator.read_count);

  //
  // The minus one is because the iterator only knows to complete when it has read
  // past the end.
  //
  assert(world->entities_count == (iterator.read_count-1) && "Iterator terminated prematurely or otherwise had a wrong count.");
}

void setup_test_preamble(void)
{
  arena = memory_arena_create("test-arena", g_memory_buffer, sizeof(g_memory_buffer));
}

void test_world_serialization_and_iteration_usage_scenario(void)
{
  Serializer serializer;
  World*     world;
  uint8_t*   world_buffer;
  size_t     world_buffer_size;

  setup_test_preamble();
  
  world_buffer = setup_simple_world_for_test(&world_buffer_size, testworld1, 8, 8);
  _debugprintf( "world_buffer: %p, world size: %lld\n", world_buffer, world_buffer_size);

  serializer   = serializer_from_memory(world_buffer, world_buffer_size);
  world = world_read_from_serializer(arena, &serializer);

  assert((world->version == WORLD_CURRENT_VERSION) && "world version header is not correct?");
  assert((world->world_w == 8) && "simple world width is not correct");
  assert((world->world_h == 8) && "simple world height is not correct");

  //
  // for the simple world. I know I have this many entities so.
  //
  _debugprintf("Test that the entity count is serialized/synced correctly");
  assert(world->entities_count == world_builder->entity_count && "simple world entity count is wrong");

  _debugprintf("Test that the world tiles serialized correctly");
  assert_world_tiles_properties_are_correct(world);

  _debugprintf("Test that the variable record sized entities can be iterated correctly");
  test_entity_iteration(world, &serializer);
}

void test_world_serialization_from_a_file_scenario(void)
{
  Serializer serializer;
  World*     world;
  const char* test_world_file = "./world_tests_level.slvl";

  setup_test_preamble();
  
  _debugprintf("Writing test world into file.");
  world_builder = make_test_world(testworld1, 8, 8);
  serializer   = serializer_from_file(test_world_file);
  serializer_set_mode(&serializer, SERIALIZER_MODE_WRITE);
  world_builder_serialize_world(world_builder, &serializer);
  filesystem_close(serializer.file_handle);
  _debugprintf("Test world outputted as \"%s\"", test_world_file);

  _debugprintf("Reopening test world file.");
  serializer = serializer_from_file(test_world_file);
  serializer_set_mode(&serializer, SERIALIZER_MODE_READ);
  world = world_read_from_serializer(arena, &serializer);

  assert((world->version == WORLD_CURRENT_VERSION) && "world version header is not correct?");
  assert((world->world_w == 8) && "simple world width is not correct");
  assert((world->world_h == 8) && "simple world height is not correct");

  //
  // for the simple world. I know I have this many entities so.
  //
  _debugprintf("Test that the entity count is serialized/synced correctly");
  assert(world->entities_count == world_builder->entity_count && "simple world entity count is wrong");

  _debugprintf("Test that the world tiles serialized correctly");
  assert_world_tiles_properties_are_correct(world);

  _debugprintf("Test that the variable record sized entities can be iterated correctly");
  test_entity_iteration(world, &serializer);
  filesystem_close(serializer.file_handle);

  remove(test_world_file);
}

int main(void)
{

  _debugprintf("test_world_serialization_and_iteration_usage_scenario");
  test_world_serialization_and_iteration_usage_scenario();

  _debugprintf("test_world_serialization_from_a_file_scenario");
  test_world_serialization_from_a_file_scenario();

  return 0;
}
