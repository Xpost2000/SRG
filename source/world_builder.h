#ifndef WORLD_BUILDER_H
#define WORLD_BUILDER_H

//
// This is code that's meant to only be
// imported used from tools / tests
//
// though this *could* run in engine technically
//
#include "common.h"

#include "memory_arena.h"
#include "world.h"
#include "serializer.h"

typedef struct World_Builder                     World_Builder;

//
// These are fixed size to keep allocation simple
// since these can be walked quite simply
//
struct World_Builder {
  Memory_Arena*   arena;
  int32_t         entity_count;
  int32_t         width;
  int32_t         height;

  int32_t         entities_byte_size;

  World_Entity*   entities[4096];
  uint8_t         tile_storage[Kilobyte(16)];
};

World_Builder* world_builder_create(Memory_Arena* arena);

//
// entity builders
//
void           world_builder_add_player_spawn(World_Builder* builder, int32_t x, int32_t y, int16_t look_angle);
void           world_builder_add_object_prop(World_Builder* builder, int32_t  x, int32_t  y, uint32_t asset_id, uint32_t flags, int16_t  angle);
void           world_builder_add_stage_end_region(World_Builder* builder, int32_t x, int32_t y, int32_t w, int32_t h, int32_t next_level_id);
//
// end entity builders
//

void           world_builder_set_dimensions(World_Builder* builder, int32_t w, int32_t h);
void           world_builder_set_tile(World_Builder* builder, int32_t x, int32_t y, uint8_t tile_id);
void           world_builder_set_world_tile_data(World_Builder* builder, int32_t w, int32_t h, uint8_t* tile_data);

void           world_builder_serialize_world(World_Builder* builder, Serializer* serializer);

//
// memory_size should at least be world_builder_get_memory_required
//
World*         world_builder_build_world_in_memory(World_Builder* builder, uint8_t* memory, uint32_t memory_size);
int32_t        world_builder_get_memory_required(World_Builder* builder);

#endif
