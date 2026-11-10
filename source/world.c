#include "world.h"

World* world_read_from_serializer(Memory_Arena* arena, Serializer* serializer)
{
  World* result;

  result = (World*) memory_arena_push_aligned(arena, sizeof(*result));
  serializer_read_into_bytes(serializer, (uint8_t*) result, sizeof(*result));

  return result;
}

World_Entity_Iterator world_entity_iterator_begin(World* world, Serializer* serializer)
{
  World_Entity_Iterator result;

  result.world = world;
  result.serializer = serializer;
  result.read_count = -1;
  world_entity_iterator_advance(&result);

  return result;
}

int world_entity_iterator_complete(World_Entity_Iterator* iterator)
{
  if (iterator->read_count >= (int32_t)iterator->world->entities_count) {
    return 1;
  }

  return 0;
}

void world_entity_iterator_advance(World_Entity_Iterator* iterator)
{
  int32_t remaining_read_size;
  iterator->entity = (World_Entity*) iterator->_entity_buffer;

  serializer_read_into_bytes(iterator->serializer,
                             iterator->_entity_buffer,
                             sizeof(World_Entity));

  remaining_read_size = iterator->entity->size - sizeof(World_Entity);

  assert(remaining_read_size >= 0 && "[WORLD_ENTITY_ITERATOR] it should not be possible to have a remaining_read_size less than zero. Improperly formed entity?");

  serializer_read_into_bytes(iterator->serializer,
                             iterator->_entity_buffer + sizeof(World_Entity),
                             remaining_read_size);

  iterator->read_count++;
}

World_Tile world_get_tile_at(World* world, int32_t x, int32_t y)
{
  World_Tile result;
  int16_t    cell_x;
  int16_t    cell_y;
  uint8_t    cell_id;

  cell_x = x / WORLD_GRID_UNIT_ONE;
  cell_y = y / WORLD_GRID_UNIT_ONE;

  cell_id = world->data[cell_y * world->world_w + cell_x];

  result.flags = 0;
  result.x = cell_x;
  result.y = cell_y;

  //
  // For now, a non-zero cell id is gonna be solid
  //
  if (cell_id != 0) {
    result.flags |= WORLD_TILE_FLAG_SOLID;
  }

  return result;
}
