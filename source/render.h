#ifndef RENDER_H
#define RENDER_H

#include "common.h"
#include "font.h"
#include "vram.h"

#include <psxgte.h>

//
// Double-buffered, ordering-table based renderer. 3D goes through the R_render_*
// command list, 2D writes GPU packets directly because a 2D draw has nothing
// that benefits from being deferred.
//
// Frame shape:
//
//   R_render_new_frame();
//   R_render_push_mesh(...);               // 3D
//   R_render_end_frame();                  // sort the 3D BEFORE drawing 2D
//   render_tile(...); render_sprite(...);  // 2D, any order, sorted by z
//   render_end_frame();                    // waits for GPU + VSync, swaps,
//                                          // and clears for the next frame
//
// 2D z runs 0..RENDER_2D_OT_LENGTH-1. HIGHER z is drawn FIRST (further back), so
// z = 0 is the front-most layer. All of 2D is in front of all of 3D.
//
// NOTE:
// - Two draws at the same z come out in REVERSE call order, the later one ends
//   up underneath. If the order matters, give them different z.
// - Running out of 2D packets flushes instead of crashing (see render.c). It
//   hitches and breaks z ordering across the flush, so it is a safety net.
//

#define RENDER_SCREEN_WIDTH  (320)
#define RENDER_SCREEN_HEIGHT (240)
#define RENDER_2D_OT_LENGTH  (256)

typedef struct Render_Mesh         Render_Mesh;
typedef struct Render_Command      Render_Command;
typedef struct Render_Command_Mesh Render_Command_Mesh;
/* typedef struct Render_Command_Quad Render_Command_Quad; */
typedef struct Render_Color        Render_Color;
typedef struct Render_Stats        Render_Stats;

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
// Textured primitives multiply the texture by this colour with 128 as 1.0, so
// (128, 128, 128) draws a texture unchanged and 255 is double brightness.
//
struct Render_Color {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

struct Render_Stats {
  int packets_2d_peak;    // bytes, most ever used between two flushes
  int flushes_last_frame; // should be 0, otherwise raise the packet budget
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
// 2D
//

void render_initialize(void);
void render_tile(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, int z);

//
// source is in texels, relative to the top left of the texture. SPRT cannot
// scale or rotate, it copies texels 1:1.
//
void render_sprite(const Vram_Texture* texture, Rectangle32 source, Vector2 position, Render_Color color, int z);

//
// The font must have been uploaded and told where it went with
// font_set_vram_information. '\n' starts a new line.
//
void render_font_text(Font* font, Vector2 position, const char* text, Render_Color color, int z);

//
// Debug only: goes through FntSort and the SDK's font. Use render_font_text for
// anything a player is meant to see.
//
void render_text(int x, int y, int z, const char* text);

void         render_end_frame(void);
Render_Stats render_get_stats(void);

#endif
