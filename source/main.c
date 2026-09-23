//
// this just clears a screen red, super simple testing compilation sample.
//
#include "common.h"

#include "cdfs.h"
#include "memory_card.h"
#include "input_pad.h"

#include <psxpad.h>
#include <psxgpu.h>

typedef struct SaveDummyPayload SaveDummyPayload;

struct SaveDummyPayload {
  int x;
  int y;
  int z;
  PAD_TO_MEMCARD_BLOCKS(1);
};

static uint8_t iconfile[192]; // fixed 1024 bytes for the file

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

  memory_card_initialize();
  savename = get_game_save_name(1, 0);

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

    memory_card_end();
  }

  //
  // read the file and hopefully it doesn't look wrong...
  //
  filehandle = cd_file_open(".\\RES\\SAVICO.TIM");
  if (filehandle.valid) {
    readcount = cd_file_read_sync_uncached(&filehandle, iconfile, sizeof(iconfile));
    printf("read %d bytes from icon file\n", readcount);
  } else {
    printf("filehandle not valid?\n");
  }

  //
  // Initialize the low level input module
  //
  input_pad_initialize();


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

      memory_card_start();
      payload.x = 4;
      payload.y = 9;
      payload.z = 12;
      memory_card_write(savename, iconfile, &payload, sizeof(payload));

      memory_card_end();
      input_pad_start();
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
