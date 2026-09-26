#include "render.h"

#include <psxgpu.h>

//
// Size of the buffer GPU commands and primitives are written to each frame.
// If the program asserts in _new_primitive, increase this.
//
#define RENDER_PACKET_BUFFER_SIZE (32768)

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
}

//
// Carve `size` bytes out of the active packet buffer and link the new
// primitive into the OT at depth z.
//
// ClearOTagR builds a REVERSED table: DrawOTagEnv is handed the LAST entry
// and walks backwards, so higher z is processed (drawn) first and ends up
// behind lower z.
//
static void* _new_primitive(int z, size_t size)
{
  Render_Buffer* buffer    = &g_buffers[g_active_buffer];
  uint8_t*       primitive = g_next_packet;

  assert(z >= 0 && z < RENDER_OT_LENGTH && "[RENDER] z outside ordering table.");

  addPrim(&buffer->ordering_table[z], primitive);
  g_next_packet += size;

  assert(g_next_packet <= &buffer->packets[RENDER_PACKET_BUFFER_SIZE] && "[RENDER] Out of packet buffer, raise RENDER_PACKET_BUFFER_SIZE.");
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
  ClearOTagR(display_buffer->ordering_table, RENDER_OT_LENGTH);
}
