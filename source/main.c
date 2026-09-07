//
// this just clears a screen red, super simple testing compilation sample.
//

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <psxgpu.h>

#include <psxpad.h>
#include <psxapi.h>

typedef struct FrameBuffer FrameBuffer;
struct FrameBuffer {
  DISPENV display_environment[2];
  DRAWENV draw_environment[2];
  
  unsigned char cmd_buffer[2][1024];
  int           cmd_buffer_used[2];
  
  uint32_t     ordering_table[2][1];

  int frame_index;
};

//
// this will allocate memory from the specific frame that is not being used
// (the one not being displayed on screen, because the displayed framebuffer is supposed
// to be "frozen" and if we touch anything while that framebuffer is being displayed, we may get unexpected behavior)
//
unsigned char* framebuffer_alloc_mem(FrameBuffer* framebuffer, int amount)
{
  unsigned char* returnaddress = framebuffer->cmd_buffer[framebuffer->frame_index] + framebuffer->cmd_buffer_used[framebuffer->frame_index];
  framebuffer->cmd_buffer_used[framebuffer->frame_index] += amount;
  return returnaddress;
}

void framebuffer_reset_allocator(FrameBuffer* framebuffer)
{
  framebuffer->cmd_buffer_used[framebuffer->frame_index] = 0;
}

int main(int argc, const char **argv) {
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
  FrameBuffer framebuffer;
  //
  // Initialize the GPU
  //
  ResetGraph(0);

  //
  // Controller buffers for receiving data from the BIOS
  //
  PADTYPE gamepads[2];

  //
  // Setting it up is pretty easy, we just pick a section
  // of VRAM to mark as our draw (where rendering commands output)
  // and display (where the display hardware reads from).
  //
  // use the opposite rectangles for opposing ones
  //
  //               < VRAM MAP >
  // ----------------------------------------------
  // --               -                 -----------
  // --               -                 -----------
  // --DRAW0 / DISP1  -   DRAW1 / DISP0 -----------
  // --               -                 -----------
  // --               -                 -----------
  // ----------------------------------------------
  // ----------------------------------------------
  // ----------------------------------------------
  // ----------------------------------------------
  // ------------------ETC-------------------------
  // ----------------------------------------------
  // ----------------------------------------------
  //
  // we'll make the tradeoff for visual smoothness for
  // (at most)one potential frame of latency, which is fine.
  //
  // if you check the emulator's VRAM view, you should see that
  // we've setup the framebuffers to essentially follow the order
  // in the diagram.
  //

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

  SetDefDispEnv(&framebuffer.display_environment[0], 0,   0, 320, 240);
  SetDefDrawEnv(&framebuffer.draw_environment[0],    0,   0, 320, 240);

  SetDefDispEnv(&framebuffer.display_environment[1], 320, 0, 320, 240);
  SetDefDrawEnv(&framebuffer.draw_environment[1],    320, 0, 320, 240);

  //
  // While we're setting them up to have the display/draw overlap
  // the same region, we will display / draw from the opposite framebuffer
  //
  // because we will by induction assume that the opposite framebuffer is "completed".
  // of course, we can wait for the GPU to finish if we're too fast, but the other useful
  // point of double buffering, is that it allows us to get increased parallelism by preparing
  // N frames in advance (the GPU can display a frame at the same time to draws into another.)
  //
  setRGB0(&framebuffer.draw_environment[0], 127, 0, 0);
  setRGB0(&framebuffer.draw_environment[1], 127, 0, 0);
  framebuffer.draw_environment[0].isbg = 1;
  framebuffer.draw_environment[1].isbg = 1;

  //
  // Initialize gamepad
  //
  InitPAD(&gamepads[0], sizeof(gamepads[0]), &gamepads[1], sizeof(gamepads[1]));

  //
  // Turn on display
  //
  SetDispMask(1);

  printf("Hello PSX\n");
  
  int tile_x = 320 / 2 - 20;
  int tile_y = 240 / 2 - 20;

  int vx = 5;
  int vy = 2;
  
  StartPAD();

  for (;;) {
    DISPENV* presenting_display_environment;
    DRAWENV* available_draw_environment;
    //
    // Complete GPU drawing commands and
    // wait on vblank / vsync
    //
    DrawSync(0);
    VSync(0);

    framebuffer_reset_allocator(&framebuffer);

    //
    // Initialize both ordering tables in the double buffer
    //
    {
      int i;
      for (i = 0; i < 2; ++i) {
        ClearOTag(framebuffer.ordering_table[i], 1);
      }
    }

    if (!(gamepads[0].btn & PAD_TRIANGLE)) {
      setRGB0(&framebuffer.draw_environment[0], 0, 127, 0);
      setRGB0(&framebuffer.draw_environment[1], 0, 127, 0);
    } else if (!(gamepads[0].btn & PAD_SQUARE)) {
      setRGB0(&framebuffer.draw_environment[0], 0, 0, 127);
      setRGB0(&framebuffer.draw_environment[1], 0, 0, 127);
    } else if (!(gamepads[0].btn & PAD_CROSS)) {
      setRGB0(&framebuffer.draw_environment[0], 127, 127, 127);
      setRGB0(&framebuffer.draw_environment[1], 127, 127, 127);
    }

    {
      TILE* t;
      TILE* t2;
    
      /*

      x(t) = vt + x0

      x += vx;
      y += vy;

      v(t) = a*t + v0;
      */

      tile_x += vx;
      tile_y += vy;

      //
      // if the right edge of the tile is past the right edge of the screen
      //
      if (tile_x > 320 - 40 ||
          tile_x < 0) {
        vx *= -1;
      }
      
      if (tile_y > 240 - 40 ||
          tile_y < 0) {
        vy *= -1;
      }

      //
      //
      //           SMILE!
      //     TODAY IS A GREAT DAY!
      //
      //
      //          ---------
      //        -------------
      //       --            --
      //       --            --
      //       --            --
      //       --            --
      //      |O |          |O |
      //       --            --
      //        |           |
      //    |   |           |     |
      //     \_ |           |   _/
      ///      \_______________/

      t = (TILE*) framebuffer_alloc_mem(&framebuffer, sizeof(*t));
      setTile(t);
      setRGB0(t, 0, 0, 128);
      setXY0(t, tile_x, tile_y);
      setWH(t, 40, 40);

      t2 = (TILE*) framebuffer_alloc_mem(&framebuffer, sizeof(*t));
      setTile(t2);
      setRGB0(t2, 128, 128, 128);
      setXY0(t2, tile_x/2, tile_y/2);
      setWH(t2, 40, 40);

      addPrim(framebuffer.ordering_table[framebuffer.frame_index], t);
      addPrim(framebuffer.ordering_table[framebuffer.frame_index], t2);
    }

    available_draw_environment = &framebuffer.draw_environment[framebuffer.frame_index];
    presenting_display_environment = &framebuffer.display_environment[framebuffer.frame_index ^ 1];

    PutDispEnv(presenting_display_environment);

    DrawOTag(framebuffer.ordering_table[framebuffer.frame_index]);
    PutDrawEnv(available_draw_environment);

    //
    // ... nothing to draw in draw environment really
    // as I'm not trying to render any primitives.
    //
    // but I'll just advance the frame.
    //
    framebuffer.frame_index ^= 1;
  }

  return 0;
}
