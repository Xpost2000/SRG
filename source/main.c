//
// this just clears a screen red, super simple testing compilation sample.
//
#include "common.h"

#include "cdfs.h"
#include "memory_card.h"
#include "input_pad.h"
#include "ps1_mem_limit.h"

#include <psxpad.h>
#include <psxgpu.h>

typedef struct SaveDummyPayload SaveDummyPayload;

struct SaveDummyPayload {
  int x;
  int y;
  int z;
};

static uint8_t iconfile[CD_SECTOR_SIZE]; // one CD sector

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
  DISPENV display_environment[2];
  DRAWENV draw_environment[2];
  char* savename;
                           //
  int     frame_index = 0; // for the double buffer counter
                           //

  //
  // testing out cdfs and memory card
  //
  CD_File filehandle;
  size_t readcount;

  //
  // Initialize the GPU
  //
  ResetGraph(0);

  //
  // Initialize CD I/O
  //
  cd_start();

  memory_card_initialize();
  savename = get_game_save_name(1, 0);

  {
    uintptr_t remaining_memory = system_get_remaining_allocatable_memory();
    _debugprintf("[MEMORY]: %d bytes, %d kb, %d mb left\n", remaining_memory, remaining_memory / 1024, remaining_memory / (1024*1024));
  }

  {
    memory_card_start();

    printf("looking for memcard file: %s\n", savename);
    if (memory_card_file_exists(savename)) {
      SaveDummyPayload payload;

      memory_card_read(savename, &payload, sizeof(payload));

      printf("save file found\n");
      printf("x: %d, y: %d, z: %d\n", payload.x, payload.y, payload.z);
    } else {
      printf("save file not found\n");
    }

    memory_card_stop();
  }

  //
  // read the file and hopefully it doesn't look wrong...
  //
  filehandle = cd_file_open("\\RES\\SAVICO.TIM");
  if (filehandle.valid) {
    size_t filesize = cd_file_get_size(&filehandle);
    readcount = cd_file_read_sync_uncached(&filehandle, iconfile, CD_SECTOR_SIZE);
    printf("read %d bytes from icon file (%d sz)\n", readcount, CD_SECTOR_SIZE);
  } else {
    printf("filehandle not valid?\n");
  }

  //
  // Initialize the low level input module
  //
  input_pad_initialize();
  input_pad_start();


  //
  // Setting it up is pretty easy, we just pick a section
  // of VRAM to mark as our draw (where rendering commands output)
  // and display (where the display hardware reads from).
  //
  // use the opposite rectangles for opposing ones
  //
  //               < VRAM MAP >
  // ----------------------------------------------
  // --               -----------------------------
  // --               -----------------------------
  // --DRAW0 / DISP1  -----------------------------
  // --               -----------------------------
  // --               -----------------------------
  // ----------------------------------------------
  // --               -----------------------------
  // --               -----------------------------
  // --DRAW1/DISP0    -----------------------------
  // --               -ETC-------------------------
  // --               -----------------------------
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

  SetDefDispEnv(&display_environment[0], 0,   0,   320, 240);
  SetDefDrawEnv(&draw_environment[0],    0,   0,   320, 240);

  SetDefDispEnv(&display_environment[1], 0,   240, 320, 240);
  SetDefDrawEnv(&draw_environment[1],    0,   240, 320, 240);

  //
  // While we're setting them up to have the display/draw overlap
  // the same region, we will display / draw from the opposite framebuffer
  //
  // because we will by induction assume that the opposite framebuffer is "completed".
  // of course, we can wait for the GPU to finish if we're too fast, but the other useful
  // point of double buffering, is that it allows us to get increased parallelism by preparing
  // N frames in advance (the GPU can display a frame at the same time to draws into another.)
  //
  setRGB0(&draw_environment[0], 127, 0, 0);
  setRGB0(&draw_environment[1], 127, 0, 0);
  draw_environment[0].isbg = 1;
  draw_environment[1].isbg = 1;

  //
  // Turn on display
  //
  SetDispMask(1);

  printf("Hello PSX\n");

  for (;;) {
    DISPENV* presenting_display_environment;
    DRAWENV* available_draw_environment;

    input_pad_frame();

    //
    // Complete GPU drawing commands and
    // wait on vblank / vsync
    //
    DrawSync(0);
    VSync(0);

    available_draw_environment = &draw_environment[frame_index];
    presenting_display_environment = &display_environment[frame_index ^ 1];

    if (input_pad_mask_button_pressed(0, PAD_UP)) {
      SaveDummyPayload payload;
      payload.x = 4;
      payload.y = 255;
      payload.z = 12;

      printf("writing save\n");

      memory_card_start();
      memory_card_write(savename, iconfile, &payload, sizeof(payload));

      memory_card_stop();

      printf("wrote save\n");

      input_pad_start();
      printf("I pressed up\n");
    }

    PutDispEnv(presenting_display_environment);
    PutDrawEnv(available_draw_environment);

    //
    // ... nothing to draw in draw environment really
    // as I'm not trying to render any primitives.
    //
    // but I'll just advance the frame.
    //
    frame_index ^= 1;
  }

  return 0;
}
