#ifndef WORLD_H
#define WORLD_H

#include "serializer.h"
#include "memory_arena.h"

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
#define WORLD_CURRENT_VERSION (WORLD_VERSION_0)

typedef struct World_Entity_Iterator         World_Entity_Iterator;
typedef struct World                         World;
typedef struct World_Entity                  World_Entity;
typedef struct World_Entity_Player_Spawn     World_Entity_Player_Spawn;
typedef struct World_Entity_Object_Prop      World_Entity_Object_Prop;
typedef struct World_Entity_Stage_End_Region World_Entity_Stage_End_Region;
typedef struct World_Tile                    World_Tile;

enum World_Data_Version {
  WORLD_VERSION_0,
  WORLD_VERSION_COUNT,
};

enum World_Entity_Type {
  WORLD_ENTITY_TYPE_NONE,
  WORLD_ENTITY_TYPE_PLAYER_SPAWN,
  WORLD_ENTITY_TYPE_OBJECT_PROP,
  WORLD_ENTITY_TYPE_STAGE_END_REGION,
  WORLD_ENTITY_TYPE_COUNT,
};

enum World_Entity_Object_Prop_Flags {
  WORLD_ENTITY_OBJECT_PROP_FLAGS_NONE  = 0x0,
  WORLD_ENTITY_OBJECT_PROP_FLAGS_SOLID = BIT(0),
  WORLD_ENTITY_OBJECT_PROP_FLAGS_COUNT = 2,
};

enum World_Tile_Flag {
  WORLD_TILE_FLAG_NONE  = 0,
  WORLD_TILE_FLAG_SOLID = BIT(0),
  WORLD_TILE_FLAG_COUNT = 2,
};

static const char* g_world_entity_type_strings[] = {
  "(none)", "player_spawn", "object_prop", "stage_end_region",
};

//
// The unit coordinates for everything assumes a 20.12 fixed point
// so that way it feeds into the renderer pretty easily.
//
// ONE == 4096. So just use WORLD_GRID_UNIT_ONE
//

struct World {
  int32_t	version;

  //
  // This is the total size of the file / data
  //
  uint32_t	total_size;

  //
  // This is the size that mainly matters for memory allocation
  // as the stuff that's actually kept after load is mainly the world
  // tiles which is mostly collision information.
  //
  uint32_t	size;

  uint32_t      entities_count;
  int32_t       world_w;
  int32_t       world_h;

#if 0
  //
  // For V1, the tiles are just an uint8_t,
  // but maybe this can be richer in the future.
  //
  uint8_t   tiles[world_h * world_w];

  //
  // NOTE(jerry):
  // this is not actually a perfectly
  // indexable array, because the entries
  // are variably sized so as to reduce
  // disk-space.
  //
  World_Entity entities[];
#else
  uint8_t      data[];
#endif
};

struct World_Entity {
  uint16_t	type;
  //
  // This includes the size of the header itself
  // which is just 4 bytes, including whatever variable payload needs to exist.
  //
  uint16_t	size;
};

struct World_Entity_Player_Spawn {
  World_Entity	base;
  int32_t	x;
  int32_t	y;

  //
  // For simplicity, this uses the same angle system as the GTE
  // 1024 = 90 degrees, 4096 = 360 degrees.
  //
  int16_t	look_angle;
};

struct World_Entity_Object_Prop {
  World_Entity	base;
  int32_t	x;
  int32_t	y;

  //
  // NOTE(jerry): this is *should* be the id
  // that's in fileslist.g.c, however we can't know
  // the ID before the fileslist.g.c is generated,
  // so maybe we should actually just make this the filehash
  //
  // it doesn't really matter, it's just a number, can resolve during discussion.
  //
  uint32_t      asset_id;
  uint32_t	flags;
  int16_t	angle;
};

struct World_Entity_Stage_End_Region {
  World_Entity base;
  int32_t      x;
  int32_t      y;
  int32_t      w;
  int32_t      h;

  //
  // For if we transition to another level,
  // references another level to load.
  // If -1, who knows? I think that might mean the end of the game
  // but also up to discussion for preliminary format.
  //
  int32_t      next_level_id;
};

struct World_Entity_Iterator {
  World*        world;
  Serializer*   serializer;

  //
  // I really doubt any of the data-store
  // entities will get this large, however
  // these iterators are not meant to be stored
  // for long, so I'm okay with fixed-sizing this.
  //
  // To simplify code.
  //
  uint8_t       _entity_buffer[1024];
  World_Entity* entity;
  
  int           read_count;
};

struct World_Tile {
  //
  // Grid Coordinates
  //
  int16_t	x;
  int16_t	y;

  //
  // Property flags. Mostly solid for now.
  //
  int16_t	flags;
};

World*                world_read_from_serializer(Memory_Arena* arena, Serializer* serializer);
World_Tile            world_get_tile_at(World* world, int32_t x, int32_t y);

//
// While this *sounds* like an unusual choice for the parameter, the justification is that
// the world data structure in memory doesn't really *hold* the memory for the data entities,
// this is because they're useless at runtime, and it is not worth the slight code complexity to
// *formally* load them and process them. (because then we need to deallocate them safely and such and such.)
//
// The data entities only really exist so that we can create *real* entities from them. In reality there is
// only one narrow correct way to use the API, and this is a *load* time API. It's not meant to be used to
// iterate over the world for the entities at gameplay or runtime.
//
World_Entity_Iterator world_entity_iterator_begin(World* world, Serializer* serializer);

int                   world_entity_iterator_complete(World_Entity_Iterator* iterator);
void                  world_entity_iterator_advance(World_Entity_Iterator* iterator);

#endif
