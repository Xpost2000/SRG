#include "world_builder.h"

#define _new_entity_preamble(T, E)                              \
  T*     entity;                                                    \
  size_t size;                              \
  size                                        = sizeof(T);                                             \
  (builder->entity_count)++;                                            \
  (builder->entities_byte_size)                      += size;                                                    \
  entity                                      = (T*) memory_arena_push_unaligned(builder->arena, size);       \
  entity->base.type                           = E;                                  \
  entity->base.size                           = size; \
  builder->entities[builder->entity_count++]  = (World_Entity*) entity;\


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

  serializer_serialize_bytes(serializer, &world_for_header, sizeof(world_for_header));

  //
  // serialize can technically read/write back, so going to use this to readback values
  // just in that scenario
  //
  builder->width               = world_for_header.world_w;
  builder->height              = world_for_header.world_h;
  builder->entity_count        = world_for_header.entities_count;
  builder->entities_byte_size  = world_for_header.total_size - world_for_header.size;

  serializer_serialize_bytes(serializer, builder->tile_storage, builder->width * builder->height * sizeof(uint8_t));
}
