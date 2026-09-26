#include "input_pad.h"

#include <psxapi.h>
#include <psxpad.h>

//
// The BIOS driver writes straight into these from its vblank IRQ handler.
//
// GOTCHA: InitPAD zero-fills them, and a zeroed packet reads as
// stat == 0 ("ok"), type == 0 (not a real PadTypeID) and btn == 0, which
// because buttons are active-low means "every button held". Nothing is
// really connected until the first vblank fills them in. The type switch
// in _input_pad_mask_button is what protects us: type 0 falls to the
// default case and reports nothing pressed.
//
static PADTYPE g_padpacket[2];
static PADTYPE g_last_padpacket[2]; // last _frame

void input_pad_initialize(void)
{
  InitPAD(
    (uint8_t*)&g_padpacket[0], sizeof(g_padpacket[0]),
    (uint8_t*)&g_padpacket[1], sizeof(g_padpacket[1])
  );
}

void input_pad_start(void)
{
              //
  StartPAD(); // Vsync-dependent interrupt handling.
              // NOTE(jerry):
              // I know there is a higher frequency driver
              // within the libPSN00BSDK but realistically I don't
              // think we need it...
              //
              // NOTE: the memory card shares this IRQ handler, so card
              // I/O stops the pad driver. Whoever touches the card must
              // call this again afterwards. Never call StopPAD() yourself.
              //

  //
  // StartPAD() (and StartCARD()) internally do ChangeClearPAD(1), which
  // makes the BIOS pad handler mark the vblank interrupt as fully handled.
  // Nothing lower in the chain then ever sees it, and PSn00bSDK's VSync()
  // sits waiting until its timeout fires ("psxgpu: VSync() timeout" on the
  // TTY, a multi-second stall) before it repairs things itself. Passing 0
  // tells the pad handler to hand the interrupt on instead. This is the
  // standard PsyQ idiom right after StartPAD().
  //
  ChangeClearPAD(0);
}

int input_pad_is_valid(int pad_index)
{
  assert(pad_index >= 0 && pad_index < array_count(g_padpacket) && "[INPUT] Bad pad index.");
  return (g_padpacket[pad_index].stat) == 0;
}

int input_pad_type(int pad_index)
{
  assert(pad_index >= 0 && pad_index < array_count(g_padpacket) && "[INPUT] Bad pad index.");
  if (g_padpacket[pad_index].stat != 0) {
    return PAD_ID_NONE;
  }
  return g_padpacket[pad_index].type;
}

static int _input_pad_mask_button(int pad_index, uint16_t buttonmask, int frame)
{
  assert(pad_index >= 0 && pad_index < array_count(g_padpacket) && "[INPUT] Bad pad index.");
  PADTYPE* packet;
  if (frame == 1) {
    packet = &g_padpacket[pad_index];
  } else {
    packet = &g_last_padpacket[pad_index];
  }

  // NOTE(jerry):
  // Buttons are inverted state (floating signal on hardware?)
  //
  // A DualShock in analog (red LED) mode identifies as PAD_ID_ANALOG but
  // carries the same 16 button bits in the same place, so both types are
  // read identically. We only use buttons in this game, so the stick
  // bytes that follow are ignored.
  switch (packet->type) {
    case PAD_ID_ANALOG:
    case PAD_ID_DIGITAL: {
      return !!(~packet->btn & buttonmask);
    } break;
    default: {
      return 0;
    } break;
  }

  return 0;
}

int input_pad_mask_button_pressed(int pad_index, uint16_t buttonmask)
{
  int last_state = _input_pad_mask_button(pad_index, buttonmask, 0);
  int current_state = _input_pad_mask_button(pad_index, buttonmask, 1);
  return !last_state && current_state;
}

int input_pad_mask_button_released(int pad_index, uint16_t buttonmask)
{
  int last_state = _input_pad_mask_button(pad_index, buttonmask, 0);
  int current_state = _input_pad_mask_button(pad_index, buttonmask, 1);
  return last_state && !current_state;
}

int input_pad_mask_button(int pad_index, uint16_t buttonmask)
{
  return _input_pad_mask_button(pad_index, buttonmask, 1);
}

void input_pad_frame(void)
{
  g_last_padpacket[0] = g_padpacket[0];
  g_last_padpacket[1] = g_padpacket[1];
}
