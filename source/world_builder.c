#include "world_builder.h"

#define _new_entity_preamble(T, E)                              \
  T*     entity;                                                    \
  size_t size;                              \
  size                                        = sizeof(T);                                             \
  (builder->entities_byte_size)                      += size;                                                    \
  _debugprintf("Allocing entity type: %s", #E); \
  entity                                      = (T*) memory_arena_push_unaligned(builder->arena, size);       \
  entity->base.type                           = E;                                  \
  entity->base.size                           = size; \
  builder->entities[builder->entity_count++]  = (World_Entity*) entity;\
  _debugprintf("Finished setup for entity type: %s at (%p)", #E, builder->entities[builder->entity_count-1]); \


World_Builder* world_builder_create(Memory_Arena* arena)
{
  World_Builder* result;

  result = (World_Builder*) memory_arena_push_aligned(arena, sizeof(*result));
  result->arena = arena;

  return result;
}

void world_builder_add_player_spawn(World_Builder* builder,
                                    int32_t x,
                                    int32_t y,
                                    int16_t look_angle)
{
  _new_entity_preamble(World_Entity_Player_Spawn, WORLD_ENTITY_TYPE_PLAYER_SPAWN);
  entity->x          = x;
  entity->y          = y;
  entity->look_angle = look_angle;
}

void world_builder_add_object_prop(World_Builder* builder,
                                   int32_t  x,
                                   int32_t  y,
                                   uint32_t asset_id,
                                   uint32_t flags,
                                   int16_t  angle)
{
  _new_entity_preamble(World_Entity_Object_Prop, WORLD_ENTITY_TYPE_OBJECT_PROP);
  entity->x        = x;
  entity->y        = y;
  entity->asset_id = asset_id;
  entity->flags    = flags;
  entity->angle    = angle;
}

void world_builder_add_stage_end_region(World_Builder* builder,
                                        int32_t x,
                                        int32_t y,
                                        int32_t w,
                                        int32_t h,
                                        int32_t next_level_id)
{
  _new_entity_preamble(World_Entity_Stage_End_Region, WORLD_ENTITY_TYPE_STAGE_END_REGION);
  entity->x = x;
  entity->y = y;
  entity->w = w;
  entity->h = h;
  entity->next_level_id = next_level_id;
}

#undef _new_entity_preamble

void world_builder_set_dimensions(World_Builder* builder, int32_t w, int32_t h)
{
  builder->width  = w;
  builder->height = h;
}

void world_builder_set_tile(World_Builder* builder, int32_t x, int32_t y, uint8_t tile_id)
{
  builder->tile_storage[y * builder->width + x] = tile_id;
}

void world_builder_set_world_tile_data(World_Builder* builder, int32_t w, int32_t h, uint8_t* tile_data)
{
  world_builder_set_dimensions(builder, w, h);
  memcpy(builder->tile_storage, tile_data, w * h * sizeof(uint8_t));
}

void world_builder_serialize_world(World_Builder* builder, Serializer* serializer)
{
  World   world_for_header;
  int32_t world_tile_size  = sizeof(uint8_t) * builder->width * builder->height;
  int32_t base_size        = sizeof(World) + world_tile_size;
  int32_t total_world_size = base_size + builder->entities_byte_size;
  int     i;

  world_for_header.version        = WORLD_CURRENT_VERSION;
  world_for_header.total_size     = total_world_size;
  world_for_header.size           = base_size;
  world_for_header.entities_count = builder->entity_count;
  world_for_header.world_w        = builder->width;
  world_for_header.world_h        = builder->height;

  _debugprintf("Serialize world header: %d bytes", sizeof(world_for_header));
  serializer_serialize_bytes(serializer, (uint8_t*) &world_for_header, sizeof(world_for_header));

  //
  // serialize can technically read/write back, so going to use this to readback values
  // just in that scenario
  //
  builder->width               = world_for_header.world_w;
  builder->height              = world_for_header.world_h;
  builder->entity_count        = world_for_header.entities_count;
  builder->entities_byte_size  = world_for_header.total_size - world_for_header.size;

  _debugprintf("Serialize world tiles: %d bytes", builder->width * builder->height * sizeof(uint8_t));
  serializer_serialize_bytes(serializer, builder->tile_storage, builder->width * builder->height * sizeof(uint8_t));

  _debugprintf("begin to serialize entities (%d to do)", builder->entity_count);
  for (i = 0 ; i < builder->entity_count; ++i) {
    World_Entity  entity_header;
    World_Entity* e;

    //
    // I've never figured out a good solution to not make this abstraction leak a little
    // because you need to have memory allocated to read something, but writing does
    // not require allocation. Oh well.
    //
    if (serializer->mode == SERIALIZER_MODE_READ) {
      builder->entities[i] =
        (World_Entity*) memory_arena_push_unaligned(builder->arena, entity_header.size);
      memcpy(builder->entities[i], &entity_header, sizeof(entity_header));
    } else {
      //
      // copy over the header portion
      //
      entity_header = *builder->entities[i];
    }

    serializer_serialize_bytes(serializer, (uint8_t*) &entity_header, sizeof(entity_header));
    e = builder->entities[i];
    _debugprintf("e ptr: %p", e);

    serializer_serialize_bytes(serializer, (uint8_t*)(e) + sizeof(entity_header), e->size - sizeof(entity_header));
  }
}

int32_t world_builder_get_memory_required(World_Builder* builder)
{
  int32_t world_tile_size  = sizeof(uint8_t) * builder->width * builder->height;
  int32_t base_size        = sizeof(World) + world_tile_size;
  int32_t total_world_size = base_size + builder->entities_byte_size;
  _debugprintf("tile_size: %d, base_size: %d, entities_byte_size: %d, world_size: %d",
               world_tile_size,
               base_size,
               builder->entities_byte_size,
               total_world_size);
  return total_world_size;
}

World* world_builder_build_world_in_memory(World_Builder* builder, uint8_t* memory, uint32_t memory_size)
{
  World*     result;
  Serializer memory_serializer;
  int32_t    total_world_size = world_builder_get_memory_required(builder);

  assert((memory_size >= (uint32_t)total_world_size) && "[WORLD_BUILDER] not enough memory to store world");

  result = (World*) memory;
  memory_serializer = serializer_from_memory(memory, memory_size);
  serializer_set_mode(&memory_serializer, SERIALIZER_MODE_WRITE);

  world_builder_serialize_world(builder, &memory_serializer);

  return result;
}
