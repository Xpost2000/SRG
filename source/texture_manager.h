#ifndef TEXTURE_MANAGER_H
#define TEXTURE_MANAGER_H

#include "common.h"

typedef int32_t               Texture_Handle;
typedef int16_t               CLUT_Handle;
typedef struct Texture_Source Texture_Source;

enum Texture_Source_BPP {
  TEXTURE_SOURCE_BPP_4,
  TEXTURE_SOURCE_BPP_8,
  TEXTURE_SOURCE_BPP_16,
  TEXTURE_SOURCE_BPP_COUNT,
};

enum Texture_Load_Priority {
  //
  // Permenant textures will never get unloaded by clear all
  // however they all must be loaded before any screen textures
  // show up (since it is still a linear allocation)
  //
  TEXTURE_LOAD_PRIORITY_PERMENANT,
  TEXTURE_LOAD_PRIORITY_SCREEN,
};

//
// NOTE(jerry):
// should these be allocated in fast ram permenantly?
//
struct Texture_Source {
  uint16_t    tpage;
  uint8_t     bpp;

  //
  // -1 if no clut
  //
  CLUT_Handle clut;

  //
  // These are the topleft UV coordinates are in units of 16bpp
  //
  uint16_t u;
  uint16_t v;
};

void           texture_manager_clear_all(void);

//
// Send a load request for a texture file as indicated by texture_id,
// so we cannot load unknown images. It will also handle CLUT allocation.
//
//
// TODO(jerry):
// What do I do about the images from a font? As the main appeal of this system
// is that it is possible to pack them "perfectly", however there's no path for
// handling fonts currently even though they should actually be packed as textures
// just like everything else.
//
Texture_Handle texture_manager_load_request(uint8_t load_priority, int32_t texture_id);

//
// Process all texture loading requests (the reason why is because we need to know
// all the textures that will be loaded, so that we can pack them "optimally")
//
void            texture_manager_execute_all_loads(void);
Texture_Source* texture_manager_get_texture_source(Texture_Handle handle);

#endif
