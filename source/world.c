#include "world.h"

World* world_read_from_serializer(Memory_Arena* arena, Serializer* serializer)
{
  World* result;
  size_t tile_memory_size;

  result = (World*) memory_arena_push_aligned(arena, sizeof(*result));
  serializer_read_into_bytes(serializer, (uint8_t*) result, sizeof(*result));

  tile_memory_size = sizeof(uint8_t) * result->world_w * result->world_h;
  serializer_read_into_bytes(serializer, (uint8_t*) result+sizeof(*result), tile_memory_size);

  return result;
}

World_Entity_Iterator world_entity_iterator_begin(World* world, Serializer* serializer)
{
  World_Entity_Iterator result;

  result.world = world;
  result.serializer = serializer;
  result.read_count = 0;
  world_entity_iterator_advance(&result);

  return result;
}

int world_entity_iterator_complete(World_Entity_Iterator* iterator)
{
  if (iterator->read_count > (int32_t)iterator->world->entities_count) {
    return 1;
  }

  return 0;
}

void world_entity_iterator_advance(World_Entity_Iterator* iterator)
{
  int32_t remaining_read_size;
  iterator->entity = (World_Entity*) iterator->_entity_buffer;

  if (!(iterator->read_count >= (int32_t)iterator->world->entities_count)) {
    serializer_read_into_bytes(iterator->serializer,
                               iterator->_entity_buffer,
                               sizeof(World_Entity));

    remaining_read_size = iterator->entity->size - sizeof(World_Entity);

    assert(remaining_read_size >= 0 && "[WORLD_ENTITY_ITERATOR] it should not be possible to have a remaining_read_size less than zero. Improperly formed entity?");

    serializer_read_into_bytes(iterator->serializer,
                               iterator->_entity_buffer + sizeof(World_Entity),
                               remaining_read_size);

  }

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

#ifndef RELEASE

//
// NOTE(jerry):
// man this really should just be doable
// through reflection but whatever.
//
void _debug_world_entity_printout(World_Entity* entity)
{
  _debugprintf("Entity(type=%d, size=%d)", entity->type, entity->size);

  switch (entity->type) {
  case WORLD_ENTITY_TYPE_PLAYER_SPAWN: {
    World_Entity_Player_Spawn* as_player_spawn = (World_Entity_Player_Spawn*) entity;
    _debugprintf("World_Entity_Player_Spawn {\nx=%d\ny=%d\nlook_angle=%d\n}",
                 as_player_spawn->x, as_player_spawn->y, as_player_spawn->look_angle);
  } break;
  case WORLD_ENTITY_TYPE_OBJECT_PROP: {
    World_Entity_Object_Prop* as_object_prop = (World_Entity_Object_Prop*) entity;
    _debugprintf("World_Entity_Object_Prop {\nx=%d\ny=%d\nangle=%d\nasset_id=%d\nflags=%u\n}",
                 as_object_prop->x,
                 as_object_prop->y,
                 as_object_prop->angle,
                 as_object_prop->asset_id,
                 as_object_prop->flags);
    
  } break;
  case WORLD_ENTITY_TYPE_STAGE_END_REGION: {
    World_Entity_Stage_End_Region* as_end_region = (World_Entity_Stage_End_Region*) entity;
    _debugprintf("World_Entity_Stage_End_Region {\nx=%d\ny=%d\nw=%d\nh=%d\nnext_level_id=%d\n}",
                 as_end_region->x,
                 as_end_region->y,
                 as_end_region->w,
                 as_end_region->h,
                 as_end_region->next_level_id);
  } break;
  }
}

#else

void _debug_world_entity_printout(World_Entity* entity) {}

#endif
