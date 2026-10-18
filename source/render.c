#include "render.h"

#include <psxgpu.h>
#include <psxgte.h>

//
// GTE inline ASM macros
//
#include <inline_c.h>

#define RENDER_OT_LENGTH          (512)
#define RENDER_PACKET_BUFFER_SIZE (1024 *  256)
#define MAX_RENDER_COMMANDS       (8192)

typedef enum Clip_Edge_Result Clip_Edge_Result;

enum Clip_Edge_Result {
  CLIP_EDGE_RESULT_NONE   = 0,
  CLIP_EDGE_RESULT_LEFT   = 0x1,
  CLIP_EDGE_RESULT_RIGHT  = 0x2,
  CLIP_EDGE_RESULT_TOP    = 0x4,
  CLIP_EDGE_RESULT_BOTTOM = 0x8,
  CLIP_EDGE_RESULT_COUNT  = 4,
};

//
// These are the basic structures for
// setting up a "swapchain"(https://en.wikipedia.org/wiki/Swap_chain).
//
// Double-buffered / N-buffered rendering is a common good practice
// as it allows us to avoid image artifacting from drawing on the same
// framebuffer that's being displayed.
//
// 2 is enough for most games, we'll just flipflop between them.
//
// Each buffer also owns an ORDERING TABLE (OT) and a packet buffer. The GPU
// does not draw immediately: we build primitives into the packet buffer,
// link them into the OT (a list of linked lists, one per z), and hand the
// whole OT to the GPU at the end of the frame with DrawOTagEnv. The GPU then
// chews through it while we build the next frame into the other buffer.
//
typedef struct Render_Buffer {
  DISPENV  display_environment;
  DRAWENV  draw_environment;
  uint32_t ordering_table[RENDER_OT_LENGTH];
  uint8_t  packets[RENDER_PACKET_BUFFER_SIZE];
} Render_Buffer;

static Render_Buffer g_buffers[2];
static int           g_active_buffer;
static uint8_t*      g_next_packet;
uint32_t             g_used = 0;
uint32_t             g_peak_used = 0;

static uint32_t       g_render_command_count = 0; 
static Render_Command g_render_commands[MAX_RENDER_COMMANDS];

// TODO(jerry): remove hacky shit

// Light color matrix
// Each column represents the color matrix of each light source and is
// used as material color when using gte_ncs() or multiplied by a
// source color when using gte_nccs(). 4096 is 1.0 in this matrix
// A column of zeroes effectively disables the light source.
MATRIX color_mtx = {
  ONE, 0, 0,	// Red
  ONE, 0, ONE,	// Green
  ONE, ONE, 0	// Blue
};

// Light matrix
// Each row represents a vector direction of each light source.
// An entire row of zeroes effectively disables the light source.
MATRIX light_mtx = {
  /* X,  Y,  Z */
  /* 0 , -2048 , -2048, */
  0 ,     0, -1,
  -ONE	  , 0	  , 0,
  ONE	  , 0	  , 0
};

VECTOR cam_pos;


//
// Setting it up is pretty easy, we just pick a section
// of VRAM to mark as our draw (where rendering commands output)
// and display (where the display hardware reads from).
//
// use the opposite rectangles for opposing ones
//
//               < VRAM MAP (1024 x 512, 16bpp) >
// ---------------------------------------------------------------
// --               ------------------------------------- FONT ---
// --               ------------------------------------- (960,0)-
// --DRAW0 / DISP1  ----------------------------------------------
// --  320 x 240    ----------------------------------------------
// --               ----------------------------------------------
// ---------------------------------------------------------------
// --               ----------------------------------------------
// --               ----------------------------------------------
// --DRAW1/DISP0    ----------------------------------------------
// --  320 x 240    -ETC------------------------------------------
// --               ----------------------------------------------
// ---------------------------------------------------------------
//
// we'll make the tradeoff for visual smoothness for
// (at most)one potential frame of latency, which is fine.
//
// if you check the emulator's VRAM view, you should see that
// we've setup the framebuffers to essentially follow the order
// in the diagram. The SDK's debug font texture lives at (960, 0),
// far right, out of the way of anything we will put there later.
//
// The PS1 was known to support a few modes for PAL
// (EUROPE,SOUTH AMERICA,...) and NTSC (NORTH AMERICA,JAPAN,...)
//
// 640x480i
// 320x240p
// 320x288p
// 320x200
// 256x224
//
void render_initialize(void)
{
  //
  // Initialize the GPU
  //
  ResetGraph(0);

  SetDefDispEnv(&g_buffers[0].display_environment, 0, 0,                    RENDER_SCREEN_WIDTH, RENDER_SCREEN_HEIGHT);
  SetDefDrawEnv(&g_buffers[0].draw_environment,    0, 0,                    RENDER_SCREEN_WIDTH, RENDER_SCREEN_HEIGHT);

  SetDefDispEnv(&g_buffers[1].display_environment, 0, RENDER_SCREEN_HEIGHT, RENDER_SCREEN_WIDTH, RENDER_SCREEN_HEIGHT);
  SetDefDrawEnv(&g_buffers[1].draw_environment,    0, RENDER_SCREEN_HEIGHT, RENDER_SCREEN_WIDTH, RENDER_SCREEN_HEIGHT);

  //
  // While we're setting them up to have the display/draw overlap
  // the same region, we will display / draw from the opposite framebuffer
  //
  // because we will by induction assume that the opposite framebuffer is "completed".
  // of course, we can wait for the GPU to finish if we're too fast, but the other useful
  // point of double buffering, is that it allows us to get increased parallelism by preparing
  // N frames in advance (the GPU can display a frame at the same time to draws into another.)
  //
  // isbg = 1 makes the GPU clear to the environment colour before drawing
  // the OT, so we never need an explicit clear primitive.
  //
  setRGB0(&g_buffers[0].draw_environment, 32, 32, 48);
  setRGB0(&g_buffers[1].draw_environment, 32, 32, 48);
  g_buffers[0].draw_environment.isbg = 1;
  g_buffers[1].draw_environment.isbg = 1;

  //
  // Upload the SDK's built-in debug font to VRAM. FntSort() needs it there.
  //
  FntLoad(960, 0);

  g_active_buffer = 0;
  g_next_packet   = g_buffers[0].packets;
  ClearOTagR(g_buffers[0].ordering_table, RENDER_OT_LENGTH);

  //
  // Turn on display
  //
  SetDispMask(1);

  //
  // Initialize the GTE and configure some of the
  // default settings for it.
  //
  InitGeom();

  gte_SetGeomOffset(RENDER_SCREEN_WIDTH/2, RENDER_SCREEN_HEIGHT/2);
  gte_SetGeomScreen(RENDER_SCREEN_WIDTH/2);
  gte_SetBackColor(32,32,32);

  // temp camera setup
  cam_pos.vx = 0;
  cam_pos.vy = 0;
  cam_pos.vz = -ONE * 400;
}

//
// I hope these are inlined!
//
void _sort_primitive(int z, void* prim)
{
  Render_Buffer* buffer    = &g_buffers[g_active_buffer];
  assert(z >= 0 && z < RENDER_OT_LENGTH && "[RENDER] z outside ordering table.");
  addPrim(&buffer->ordering_table[z], prim);
}

void* _alloc_primitive(size_t size)
{
  Render_Buffer* buffer    = &g_buffers[g_active_buffer];
  uint8_t*       primitive = g_next_packet;
  g_next_packet += size;
  g_used	+= size;
  assert(g_next_packet <= &buffer->packets[RENDER_PACKET_BUFFER_SIZE] && "[RENDER] Out of packet buffer, raise RENDER_PACKET_BUFFER_SIZE.");
  return (void*) primitive;
}

void _dealloc(size_t size)
{
  g_next_packet -= size;
  g_used	-= size;
}

//
// Carve `size` bytes out of the active packet buffer and link the new
// primitive into the OT at depth z.
//
// ClearOTagR builds a REVERSED table: DrawOTagEnv is handed the LAST entry
// and walks backwards, so higher z is processed (drawn) first and ends up
// behind lower z.
//
void* _new_primitive(int z, size_t size)
{
  uint8_t* primitive = _alloc_primitive(size);
  _sort_primitive(z, primitive);
  return primitive;
}

void render_tile(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, int z)
{
  //
  // TILE = untextured, flat-coloured, axis-aligned rectangle. The cheapest
  // primitive the GPU has.
  //
  TILE* tile = (TILE*)_new_primitive(z, sizeof(TILE));
  setTile(tile);
  setXY0 (tile, x, y);
  setWH  (tile, w, h);
  setRGB0(tile, r, g, b);
}

void render_text(int x, int y, int z, const char* text)
{
  Render_Buffer* buffer = &g_buffers[g_active_buffer];

  //
  // FntSort writes the sprite primitives for each glyph into our packet
  // buffer and returns the new cursor.
  //
  g_next_packet = (uint8_t*)FntSort(&buffer->ordering_table[z], g_next_packet, x, y, text);
  assert(g_next_packet <= &buffer->packets[RENDER_PACKET_BUFFER_SIZE] && "[RENDER] Out of packet buffer, raise RENDER_PACKET_BUFFER_SIZE.");
}

void render_end_frame(void)
{
  Render_Buffer* draw_buffer;
  Render_Buffer* display_buffer;

  //
  // Complete GPU drawing commands and
  // wait on vblank / vsync
  //
  DrawSync(0);
  VSync(0);

  draw_buffer    = &g_buffers[g_active_buffer];
  display_buffer = &g_buffers[g_active_buffer ^ 1];

  //
  // Show the frame the GPU just finished, then kick off drawing of the OT
  // we built this frame. DrawOTagEnv applies the DRAWENV (clear colour,
  // clip rect) and then walks the table from its last entry.
  //
  PutDispEnv(&display_buffer->display_environment);
  DrawOTagEnv(&draw_buffer->ordering_table[RENDER_OT_LENGTH - 1], &draw_buffer->draw_environment);

  //
  // Flip. The buffer we were displaying is now free to build into.
  //
  g_active_buffer ^= 1;
  g_next_packet    = display_buffer->packets;
  g_peak_used      = max(g_used, g_peak_used);
  g_used           = 0;
  ClearOTagR(display_buffer->ordering_table, RENDER_OT_LENGTH);
}

//
// NOTE(jerry): new rendering code
//

void R_render_new_frame(void)
{
  g_render_command_count = 0;
}

void R_render_push_mesh(Render_Command_Mesh mesh)
{
  Render_Command* command;

  assert(g_render_command_count < array_count(g_render_commands) && "[RENDER] Out of rendering commands, either bump the number or fix your damn scene!");
  command = &g_render_commands[g_render_command_count++];

  command->type = RENDER_COMMAND_MESH;
  command->mesh = mesh;
}

static uint8_t R_point_clip(int x, int y)
{
  uint8_t outside_of = CLIP_EDGE_RESULT_NONE;

  outside_of |= (CLIP_EDGE_RESULT_LEFT)   * (x < 0);
  outside_of |= (CLIP_EDGE_RESULT_RIGHT)  * (x >= RENDER_SCREEN_WIDTH);
  outside_of |= (CLIP_EDGE_RESULT_BOTTOM) * (y >= RENDER_SCREEN_HEIGHT);
  outside_of |= (CLIP_EDGE_RESULT_TOP)    * (y < 0);

  return outside_of;
}

static int R_triangle_clip(POLY_G3* triangle, int avg_z)
{
  uint8_t clip0;
  uint8_t clip1;
  uint8_t clip2;

  uint8_t clip01;
  uint8_t clip12;
  uint8_t clip20;

  if (avg_z < 0 || avg_z >= RENDER_OT_LENGTH) {
    return 1;
  }

  clip0  = R_point_clip(triangle->x0, triangle->y0);
  clip1  = R_point_clip(triangle->x1, triangle->y1);
  clip2  = R_point_clip(triangle->x2, triangle->y2);

  clip01 = clip0 & clip1;
  clip12 = clip1 & clip2;
  clip20 = clip2 & clip0;

  if (clip0 & clip1 & clip2) {
    return 1;
  }

  if (clip01 == 0) {
    return 0;
  }

  if (clip12 == 0) {
    return 0;
  }

  if (clip20 == 0) {
    return 0;
  }

  return 1;
}

static void R_render_command_present_mesh(Render_Command_Mesh* mesh_command)
{
  //
  // NOTE(jerry): for basic testing, only gourand geometry for now.
  // untextured, but I can expand it in future PR, it's about the same
  // code...
  //
  Render_Mesh* mesh;
  POLY_G3*     triangle;  
  int          i;
  int          z;
  uint32_t     flg;

  MATRIX  xform_matrix;
  MATRIX  local_light_matrix;
  SVECTOR scale_vector;
  SVECTOR rotation_vector;
  VECTOR  translation_vector;

  mesh = mesh_command->mesh;
  assert(((mesh->vertex_count % 3) == 0) && "[RENDER] The engine only supports triangles (non-indexed for now :( )).");

  scale_vector	     = mesh_command->scale;
  rotation_vector    = mesh_command->rotation;
  translation_vector.vx = mesh_command->position.vx >> 12;
  translation_vector.vy = mesh_command->position.vy >> 12;
  translation_vector.vz = mesh_command->position.vz >> 12;

  translation_vector.vx += -cam_pos.vx >> 12;
  translation_vector.vy += -cam_pos.vy >> 12;
  translation_vector.vz += -cam_pos.vz >> 12;

  //
  // NOTE(jerry):
  // the fixed point dimensions are 4096 = 360 degrees
  //
  RotMatrix(&rotation_vector, &xform_matrix);
  TransMatrix(&xform_matrix, &translation_vector);
  MulMatrix0(&light_mtx, &xform_matrix, &local_light_matrix);

  //
  // NOTE(jerry):
  //
  // There are opportunities to optimize the instruction scheduling
  // on the assembly level to increase instruction throughput, but this
  // is easier to read
  //

  gte_SetColorMatrix( &color_mtx );
  gte_SetLightMatrix(&local_light_matrix);
  gte_SetRotMatrix(&xform_matrix);
  gte_SetTransMatrix(&xform_matrix);

  for (i = 0; i < mesh->vertex_count; i+=3) {
    gte_ldv3(&mesh->vertices[i], &mesh->vertices[i+1], &mesh->vertices[i+2]);
    gte_rtpt_b();
    gte_stflg(&flg);

    if ((flg & (1u << 17)) ||
	(flg & (1u << 31))) {
      continue;
    }

    gte_nclip_b();

    gte_stopz(&z);
    if (z <= 0) {
      continue;
    }

    gte_avsz3_b();
    gte_stotz(&z);

    z >>= 2;

    triangle = _alloc_primitive(sizeof(*triangle));
    setPolyG3(triangle);
    gte_stsxy3(&triangle->x0, &triangle->x1, &triangle->x2);

    if (R_triangle_clip(triangle, z)) {
      _dealloc(sizeof(*triangle));
      continue;
    }


    setRGB0(triangle,   mesh->colors[i].vx, mesh->colors[i].vy, mesh->colors[i].vz);
    setRGB1(triangle, mesh->colors[i+1].vx, mesh->colors[i+1].vy, mesh->colors[i+1].vz);
    setRGB2(triangle, mesh->colors[i+2].vx, mesh->colors[i+2].vy, mesh->colors[i+2].vz);

    /* gte_ldrgb(&triangle->r0); */
    gte_ldrgb3(&triangle->r2, &triangle->r1, &triangle->r0);
    gte_ldv3(&mesh->normals[i], &mesh->normals[i+1], &mesh->normals[i+2]);
    gte_nct_b();
    gte_strgb3_g3(triangle);

    _sort_primitive(z, triangle);
  }
}

void R_render_end_frame(void)
{
  int i;

  for (i = 0; i < g_render_command_count; ++i) {
    Render_Command* command = g_render_commands + i;

    switch (command->type) {
    case RENDER_COMMAND_MESH: {
      R_render_command_present_mesh(&command->mesh);
    } break;
    default: {
      assert(0 && "[RENDER] Indeterminate render command encountered, memory stomping?");
    } break;
    }
  }
}
