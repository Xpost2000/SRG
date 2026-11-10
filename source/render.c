#include "render.h"
#include "memory_arena.h"

#include <psxgpu.h>
#include <psxgte.h>

//
// GTE inline ASM macros
//
#include <inline_c.h>

#define RENDER_OT_LENGTH          (512)

//
// A screen packed with 9x9 glyphs is about 900 SPRTs at 20 bytes each, so this
// holds a full screen of text with room for panels. Going over is not fatal,
// see _render_flush.
//
#define RENDER_2D_PACKET_BUFFER_SIZE (1024 * 24)
#define TRI_RENDER_PACKET_BUFFER_SIZE (5000)
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
// 2D gets its own table, chained on behind the 3D one (see _render_reset_tables).
// UI is therefore always in front of the world no matter what depth a triangle
// lands on, and 2D layers do not compete with 3D for buckets.
//
// The 2D packets are aligned because the GPU's DMA linked-list mode can only
// follow word-aligned addresses.
//
typedef struct Render_Buffer {
  DISPENV       display_environment;
  DRAWENV       draw_environment;
  uint32_t      ordering_table[RENDER_OT_LENGTH];
  uint32_t      ordering_table_2d[RENDER_2D_OT_LENGTH];
  Memory_Arena* packets_2d;
  uint8_t       packets_2d_memory[RENDER_2D_PACKET_BUFFER_SIZE] __attribute__((aligned(4)));
  uint8_t       tri_packets[TRI_RENDER_PACKET_BUFFER_SIZE * sizeof(POLY_G3)];
} Render_Buffer;

static Render_Buffer g_buffers[2];
static int           g_active_buffer;
static uint8_t*      g_next_packet2;
static int           g_frame_flush_count;
static Render_Stats  g_stats;

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
  ONE	  , ONE	  , 0
};

VECTOR cam_pos;

//
// Empties both ordering tables and joins them into one chain, so the GPU can be
// handed a single pointer and walk 3D then 2D.
//
// A reversed table ends at entry 0, so the 2D table has to hang off of 3D entry
// 0. This must happen right after clearing and not at the end of the frame:
// addPrim pushes onto the FRONT of a bucket, so any triangle sorted into entry 0
// later still ends up ahead of the 2D chain. Linking at the end of the frame
// would draw the UI first, and those triangles over the top of it.
//
static void _render_reset_tables(Render_Buffer* buffer)
{
  ClearOTagR(buffer->ordering_table,    RENDER_OT_LENGTH);
  ClearOTagR(buffer->ordering_table_2d, RENDER_2D_OT_LENGTH);

  addPrims(&buffer->ordering_table[0],
           &buffer->ordering_table_2d[RENDER_2D_OT_LENGTH - 1],
           &buffer->ordering_table_2d[0]);
}

static void _render_record_packet_peak(Render_Buffer* buffer)
{
  g_stats.packets_2d_peak = max((int) memory_arena_get_used(buffer->packets_2d), g_stats.packets_2d_peak);
}

//
// Setting it up is pretty easy, we just pick a section
// of VRAM to mark as our draw (where rendering commands output)
// and display (where the display hardware reads from).
//
// use the opposite rectangles for opposing ones
//
//               < VRAM MAP (1024 x 512, 16bpp) >
//
//  0            320                               960   1023
//  +-------------+----------------------------------+-----+ 0
//  |             |                                  | SDK |
//  | DRAW0/DISP1 |                                  | FONT|
//  |  320 x 240  |                                  |     |
//  |             |         TEXTURE SHELVES          |-----|
//  +-------------+         (owned by vram.c)        |     | 240
//  |             |                                  |     |
//  | DRAW1/DISP0 |                                  |     |
//  |  320 x 240  |                                  |     |
//  |             |                                  |     |
//  +-------------+                                  |     | 480
//  |    CLUTS    |                                  |     |
//  +-------------+----------------------------------+-----+ 512
//
// we'll make the tradeoff for visual smoothness for
// (at most)one potential frame of latency, which is fine.
//
// if you check the emulator's VRAM view, you should see that
// we've setup the framebuffers to essentially follow the order
// in the diagram. The SDK's debug font texture lives at (960, 0),
// far right, and vram.c never allocates in that column.
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
  setRGB0(&g_buffers[0].draw_environment, 0, 0, 0);
  setRGB0(&g_buffers[1].draw_environment, 0, 0, 0);
  g_buffers[0].draw_environment.isbg = 1;
  g_buffers[1].draw_environment.isbg = 1;

  //
  // Upload the SDK's built-in debug font to VRAM. FntSort() needs it there.
  //
  FntLoad(960, 0);

  g_buffers[0].packets_2d = memory_arena_create("render-2d-0", g_buffers[0].packets_2d_memory, RENDER_2D_PACKET_BUFFER_SIZE);
  g_buffers[1].packets_2d = memory_arena_create("render-2d-1", g_buffers[1].packets_2d_memory, RENDER_2D_PACKET_BUFFER_SIZE);

  g_active_buffer = 0;
  g_next_packet2   = g_buffers[0].tri_packets;
  _render_reset_tables(&g_buffers[0]);

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
  gte_SetBackColor(32, 32, 32);

  // temp camera setup
  cam_pos.vx = 0;
  cam_pos.vy = 0;
  cam_pos.vz = -ONE * 550;

  for (int i = 0; i < TRI_RENDER_PACKET_BUFFER_SIZE; ++i) {
    POLY_G3* g = &g_buffers[0].tri_packets[i * sizeof(POLY_G3)];
    POLY_G3* g1 = &g_buffers[1].tri_packets[i * sizeof(POLY_G3)];
    setPolyG3(g);
    setPolyG3(g1);
  }
}

//
// I hope these are inlined!
//
static void _sort_primitive(int z, void* prim)
{
  Render_Buffer* buffer    = &g_buffers[g_active_buffer];
  assert(z >= 0 && z < RENDER_OT_LENGTH && "[RENDER] z outside ordering table.");
  addPrim(&buffer->ordering_table[z], prim);
}

void* _alloc_primitive2(void)
{
  uint8_t*       primitive = g_next_packet2;
  g_next_packet2 += sizeof(POLY_G3);
  return (void*) primitive;
}

//
// Submits everything built so far and empties the tables and packets, so that
// running out of 2D packets costs a hitch instead of a crash.
//
// The frame timeline is what makes this awkward:
//
//   end of frame N-1:  screen -> region of N-2      GPU starts drawing N-1
//   building frame N:  N's region IS what is on screen
//
// Two buffers alternate, so the region frame N draws into holds frame N-2,
// which is the picture being scanned out while we build. Drawing there now
// would be visible. So wait for N-1 to finish and show it, a little earlier than
// render_end_frame would have, which leaves N's region off-screen.
//
// DrawOTagEnv with isbg set clears the framebuffer, so only the first submission
// of a frame may use it. Everything after continues on top with plain DrawOTag.
//
// NOTE:
// A flush is a hard z boundary. Things drawn after one land on top of everything
// before it, whatever their z. Only for emergencies, RENDER_2D_PACKET_BUFFER_SIZE
// should be raised if the on-screen flush count is ever non-zero in normal play.
//
static void _render_flush(void)
{
  Render_Buffer* buffer = &g_buffers[g_active_buffer];
  Render_Buffer* shown  = &g_buffers[g_active_buffer ^ 1];

  DrawSync(0);

  if (g_frame_flush_count == 0) {
    VSync(0);
    PutDispEnv(&shown->display_environment);
    DrawOTagEnv(&buffer->ordering_table[RENDER_OT_LENGTH - 1], &buffer->draw_environment);
  } else {
    DrawOTag(&buffer->ordering_table[RENDER_OT_LENGTH - 1]);
  }

  //
  // The GPU reads the packets as it goes, so they cannot be reused until it
  // has finished.
  //
  DrawSync(0);

  _render_record_packet_peak(buffer);
  _render_reset_tables(buffer);
  memory_arena_reset(buffer->packets_2d);
  g_frame_flush_count++;
}

//
// Every 2D draw call reserves ALL of its bytes up front. A sprite and the
// DR_TPAGE that selects its texture have to be submitted together, so a flush
// is only allowed to happen in here, between draw calls.
//
static void _render_2d_reserve(int bytes)
{
  Memory_Arena* arena = g_buffers[g_active_buffer].packets_2d;

  assert(bytes < arena->capacity && "[RENDER] One 2D draw call is bigger than the whole packet buffer.");

  if ((int) memory_arena_get_used(arena) + bytes >= arena->capacity) {
    _debugprintf("[RENDER] out of 2D packets, flushing.");
    _render_flush();
  }
}

static void* _render_2d_alloc(size_t size)
{
  return memory_arena_push_no_clear(g_buffers[g_active_buffer].packets_2d, size);
}

//
// ClearOTagR builds a REVERSED table: the GPU is handed the LAST entry and
// walks backwards, so higher z is processed (drawn) first and ends up behind
// lower z.
//
// Within one z it is last-in-first-drawn, addPrim pushes onto the front of the
// bucket. So a DR_TPAGE must be sorted AFTER the sprites that depend on it.
//
static void _render_2d_sort(int z, void* primitive)
{
  Render_Buffer* buffer = &g_buffers[g_active_buffer];

  assert(z >= 0 && z < RENDER_2D_OT_LENGTH && "[RENDER] z outside 2D ordering table.");
  addPrim(&buffer->ordering_table_2d[z], primitive);
}

void render_tile(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, int z)
{
  TILE* tile;

  _render_2d_reserve(sizeof(TILE));

  //
  // TILE = untextured, flat-coloured, axis-aligned rectangle. The cheapest
  // primitive the GPU has.
  //
  tile = (TILE*) _render_2d_alloc(sizeof(TILE));
  setTile(tile);
  setXY0 (tile, x, y);
  setWH  (tile, w, h);
  setRGB0(tile, r, g, b);

  _render_2d_sort(z, tile);
}

//
// SPRT has no texture page field, it samples from whichever page the GPU was
// last told about, hence the DR_TPAGE. A POLY_FT4 carries its own page but costs
// twice the bytes, and we do not need it to scale or rotate here.
//
void render_sprite(const Vram_Texture* texture, Rectangle32 source, Vector2 position, Render_Color color, int z)
{
  SPRT*     sprite;
  DR_TPAGE* tpage;

  assert(source.x >= 0 && source.x + source.w <= texture->width  && "[RENDER] Sprite source outside of its texture.");
  assert(source.y >= 0 && source.y + source.h <= texture->height && "[RENDER] Sprite source outside of its texture.");

  _render_2d_reserve(sizeof(SPRT) + sizeof(DR_TPAGE));

  sprite = (SPRT*)     _render_2d_alloc(sizeof(SPRT));
  tpage  = (DR_TPAGE*) _render_2d_alloc(sizeof(DR_TPAGE));

  setSprt(sprite);
  setXY0 (sprite, position.x, position.y);
  setWH  (sprite, source.w, source.h);
  setUV0 (sprite, texture->u + source.x, texture->v + source.y);
  setRGB0(sprite, color.r, color.g, color.b);
  sprite->clut = texture->clut;

  setDrawTPage(tpage, 0, 0, texture->tpage);

  _render_2d_sort(z, sprite);
  _render_2d_sort(z, tpage);
}

void render_font_text(Font* font, Vector2 position, const char* text, Render_Color color, int z)
{
  DR_TPAGE* tpage;
  Vector2   cursor;
  uint16_t  clut;
  int       mode;
  int       base_u;
  int       base_v;
  int       string_length;
  int       i;

  assert(font->columns != 0 && "[RENDER] This font was never loaded.");

  string_length = strlen(text);

  if (string_length == 0) {
    return;
  }

  //
  // The whole string goes in as one run: the glyphs all share one page, so one
  // DR_TPAGE sorted after them covers every one of them. Per glyph it would be
  // an extra 8 bytes on each 20 byte SPRT.
  //
  // That only works if the run cannot be split, which is why it reserves all of
  // it at once.
  //
  _render_2d_reserve(sizeof(SPRT) * string_length + sizeof(DR_TPAGE));

  mode   = font->tim.flags & VRAM_TIM_MODE_MASK;
  base_u = vram_tpage_u(font->tim.image.x, mode);
  base_v = vram_tpage_v(font->tim.image.y);
  clut   = getClut(font->tim.clut.x, font->tim.clut.y);
  cursor = position;

  for (i = 0; i < string_length; ++i) {
    uint8_t character = (uint8_t) text[i];

    if (character == '\n') {
      cursor.x  = position.x;
      cursor.y += font->glyph_height;
      continue;
    }

    if (font->glyphmap[character] != FONT_GLYPH_NONE) {
      Rectangle32 glyph_rect = font_get_glyph_rect(font, character);
      SPRT*       glyph      = (SPRT*) _render_2d_alloc(sizeof(SPRT));

      setSprt(glyph);
      setXY0 (glyph, cursor.x, cursor.y);
      setWH  (glyph, glyph_rect.w, glyph_rect.h);
      setUV0 (glyph, base_u + glyph_rect.x, base_v + glyph_rect.y);
      setRGB0(glyph, color.r, color.g, color.b);
      glyph->clut = clut;

      _render_2d_sort(z, glyph);
    }

    cursor.x += font->glyph_width;
  }

  tpage = (DR_TPAGE*) _render_2d_alloc(sizeof(DR_TPAGE));
  setDrawTPage(tpage, 0, 0, getTPage(mode, 0, font->tim.image.x, font->tim.image.y));
  _render_2d_sort(z, tpage);
}

void render_text(int x, int y, int z, const char* text)
{
  Render_Buffer* buffer;
  Memory_Arena*  arena;
  uint8_t*       packets_start;
  uint8_t*       packets_end;

  //
  // FntSort writes one SPRT_8 per glyph then one DR_TPAGE, with no idea how
  // much room there is, so the worst case gets reserved before calling it.
  //
  _render_2d_reserve(sizeof(SPRT_8) * strlen(text) + sizeof(DR_TPAGE));

  buffer        = &g_buffers[g_active_buffer];
  arena         = buffer->packets_2d;
  packets_start = arena->memory + memory_arena_get_cursor(arena);

  assert(z >= 0 && z < RENDER_2D_OT_LENGTH && "[RENDER] z outside 2D ordering table.");
  packets_end = (uint8_t*) FntSort(&buffer->ordering_table_2d[z], packets_start, x, y, text);

  memory_arena_reset_cursor_to(arena, packets_end - arena->memory);
}

void render_end_frame(void)
{
  Render_Buffer* draw_buffer;
  Render_Buffer* display_buffer;
  uint32_t*      table_head;

  //
  // Complete GPU drawing commands and
  // wait on vblank / vsync
  //
  DrawSync(0);
  VSync(0);

  draw_buffer    = &g_buffers[g_active_buffer];
  display_buffer = &g_buffers[g_active_buffer ^ 1];
  table_head     = &draw_buffer->ordering_table[RENDER_OT_LENGTH - 1];

  //
  // Show the frame the GPU just finished, then kick off drawing of the OT
  // we built this frame. DrawOTagEnv applies the DRAWENV (clear colour,
  // clip rect) and then walks the table from its last entry.
  //
  // If a flush happened the DRAWENV is already applied and has already cleared
  // the screen, applying it again would erase the flushed work.
  //
  PutDispEnv(&display_buffer->display_environment);
  if (g_frame_flush_count == 0) {
    DrawOTagEnv(table_head, &draw_buffer->draw_environment);
  } else {
    DrawOTag(table_head);
  }

  _render_record_packet_peak(draw_buffer);
  g_stats.flushes_last_frame = g_frame_flush_count;
  g_frame_flush_count        = 0;

  //
  // Flip. The buffer we were displaying is now free to build into.
  //
  g_active_buffer ^= 1;
  g_next_packet2    = display_buffer->tri_packets;
  memory_arena_reset(display_buffer->packets_2d);
  _render_reset_tables(display_buffer);
}

Render_Stats render_get_stats(void)
{
  return g_stats;
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

  MATRIX  xform_matrix;
  MATRIX  local_light_matrix;
  SVECTOR rotation_vector;
  VECTOR scale_vector;
  VECTOR  translation_vector;

  mesh = mesh_command->mesh;
  assert(((mesh->vertex_count % 3) == 0) && "[RENDER] The engine only supports triangles (non-indexed for now :( )).");

  scale_vector          = mesh_command->scale;
  rotation_vector       = mesh_command->rotation;
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
  ScaleMatrix(&xform_matrix, &scale_vector);

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

  Render_Buffer* buffer    = &g_buffers[g_active_buffer];
  for (i = 0; i < mesh->vertex_count; i+=3) {
    gte_ldv3(&mesh->vertices[i], &mesh->vertices[i+1], &mesh->vertices[i+2]);
    gte_rtpt_b();
    gte_nclip_b();
    gte_stopz(&z);

    if ((z <= 0)) {
      continue;
    }

    gte_avsz3_b();
    gte_stotz(&z);

    triangle = _alloc_primitive2();
    gte_stsxy3(&triangle->x0, &triangle->x1, &triangle->x2);

    if (R_triangle_clip(triangle, z)) {
      continue;
    }

    setRGB0(triangle,   mesh->colors[i].vx, mesh->colors[i].vy, mesh->colors[i].vz);
    setRGB1(triangle, mesh->colors[i+1].vx, mesh->colors[i+1].vy, mesh->colors[i+1].vz);
    setRGB2(triangle, mesh->colors[i+2].vx, mesh->colors[i+2].vy, mesh->colors[i+2].vz);

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
