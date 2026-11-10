#ifndef WORLD_H
#define WORLD_H

#include "serializer.h"

//
// The world format is basically just a block
// tilemap format for now, with a list of variably
// tagged entities based on their type.
//
// My expected usage of the API is essentially:
//
#if 0
  void load_map_example() {
    World_Entity_Iterator entity_iterator;
    File_Handle level_world_file = filesystem_open("gamelevel.lvl");

    //
    // Arena is given to allocate, this world structure is technically the
    // header of the world.
    //
    World*      world            = world_read_from_file(arena, level_world_file);

    //
    // To process all entities / instantiate all of them
    // into their runtime variations.
    //
    for (entity_iterator = world_entity_iterator_begin(world);
	 !world_entity_iterator_complete(&entity_iterator);
	 world_entity_iterator_advance(&entity_iterator)) {
      World_Entity* current_world_entity = entity_iterator.entity;

      //
      // dispatch on entity type
      //
      // these are sort of like "prefab" markers that we replace
      // with the actual entities when they're spawned.
      //
      switch (current_world_entity->type) {
      case XX: create_runtime_entity_xx(...);
      case YY:
      default:
	assert(0 && "Unknown entity type encountered while loading?");
	break;
      }
    }
  }

#endif

#define WORLD_GRID_UNIT_ONE (4096)

typedef struct World_Entity_Iterator         World_Entity_Iterator;
typedef struct World                         World;
typedef struct World_Entity                  World_Entity;
typedef struct World_Entity_Player_Spawn     World_Entity_Player_Spawn;
typedef struct World_Entity_Object_Prop      World_Entity_Object_Prop;
typedef struct World_Entity_Stage_End_Region World_Entity_Stage_End_Region;
typedef struct World_Tile                    World_Tile;

enum World_Entity_Type {
  WORLD_ENTITY_TYPE_NONE,
  WORLD_ENTITY_TYPE_PLAYER_SPAWN,
  WORLD_ENTITY_TYPE_OBJECT_PROP,
  WORLD_ENTITY_TYPE_STAGE_END_REGION,
  WORLD_ENTITY_TYPE_COUNT,
};

//
// TODO(jerry): avoid compiling in debug
//
static const char* g_world_entity_type_strings[] = {
  "(none)", "player_spawn", "object_prop", "stage_end_region",
};

struct World_Entity {
  uint16_t type;
  //
  // This includes the size of the header itself
  // which is just 4 bytes, including whatever variable payload needs to exist.
  //
  uint16_t size;
};

struct World_Tile {
  int16_t x;
  int16_t y;

  //
  // Property flags. Mostly solid for now.
  //
  int16_t flags;
};

World*                world_read_from_serializer(Serializer* serializer);
World_Entity_Iterator world_entity_iterator_begin(World* world);
int                   world_entity_iterator_complete(World_Entity_Iterator* iterator);
void                  world_entity_iterator_advance(World_Entity_Iterator* iterator);

//
// The tiles are expected to be in 22.10 fixed point
// ONE == 4096.
//
// Use WORLD_GRID_UNIT_ONE as the world units (or convert to it from some other fixedpt)
// unit.
//
World_Tile            world_get_tile_at(int32_t x, int32_t y);

#endif
