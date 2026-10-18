#ifndef RENDER_H
#define RENDER_H

#include "common.h"
#include <psxgte.h>

//
// The smallest thing that can put a rectangle and some debug text on the
// screen. Double-buffered, ordering-table based, lifted from the PSn00bSDK
// "hello" example. Sprites and textures are not here yet.
//
// Frame shape:
//
//   render_tile(...); render_text(...);   // any order, sorted by z
//   render_end_frame();                    // waits for GPU + VSync, swaps,
//                                          // and clears for the next frame
//
// z runs 0..RENDER_OT_LENGTH-1. HIGHER z is drawn FIRST (further back), so
// z = 0 is the front-most layer.
//

#define RENDER_SCREEN_WIDTH  (320)
#define RENDER_SCREEN_HEIGHT (240)

typedef struct Render_Mesh         Render_Mesh;
typedef struct Render_Command      Render_Command;
typedef struct Render_Command_Mesh Render_Command_Mesh;
/* typedef struct Render_Command_Quad Render_Command_Quad; */

enum Render_Command_Type {
  RENDER_COMMAND_MESH,
  RENDER_COMMAND_COUNT,
};

//
// NOTE(jerry):
// this is a somewhat temporary type, since I usually
// architect stuff to have a more centralized resource management
// but this requires a bit more code-infrastructure, and I consider
// it not in scope.
//
struct Render_Mesh {
  uint16_t vertex_count;

  //
  // TODO(jerry): I don't want these to be SVECTORs
  // to avoid coupling to PSX library, but for now I don't
  // have an easy way to generate 3D models we want to support so.
  //
  // This will have to be adjusted later.
  //
  SVECTOR* vertices;

  //
  // Optional, if they are not present, they do not exist.
  // If they are present, must be the same count as vertices
  //
  SVECTOR* normals;

  //
  // NOTE(jerry):
  // unused.
  //
  SVECTOR* colors;
};

struct Render_Command_Mesh {
  VECTOR position;
  VECTOR scale;
  SVECTOR rotation;
  Render_Mesh* mesh;
};

struct Render_Command {
  uint8_t type;
  union {
    Render_Command_Mesh mesh; 
    /* Render_Command_Quad quad; */
  };
};

//
// High Level Render Commands
//

//
// Pushing rendering commands
//
void R_render_push_mesh(Render_Command_Mesh mesh);

//
// Clearing rendering command buffer for new commands
//
void R_render_new_frame(void);

//
// Ending renderer frame and presenting / sorting current primitives.
//
void R_render_end_frame(void);

//
// Older Lower Level Primitives
//

void render_initialize(void);
void render_tile(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, int z);
void render_text(int x, int y, int z, const char* text);
void render_end_frame(void);

void* _new_primitive(int z, size_t size);
void  _sort_primitive(int z, void* prim);
void* _alloc_primitive(size_t size);

#endif
