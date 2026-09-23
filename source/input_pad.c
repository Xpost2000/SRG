#include "input_pad.h"

#include <psxapi.h>
#include <psxpad.h>

static PADTYPE g_padpacket[2];
static PADTYPE g_last_padpacket[2]; // last _frame

void input_pad_initialize(void)
{
  InitPAD(
    &g_padpacket[0], sizeof(g_padpacket[0]),
    &g_padpacket[1], sizeof(g_padpacket[1])
  );

              //
  StartPAD(); // Vsync-dependent interrupt handling.
              // NOTE(jerry):
              // I know there is a higher frequency driver
              // within the libPSN00BSDK but realistically I don't
              // think we need it...
              //
}

int input_pad_is_valid(int pad_index)
{
  assert(pad_index >= 0 && pad_index < array_count(g_padpacket) && "[INPUT] Bad pad index.");
  return (g_padpacket[pad_index].stat) == 0;
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

int input_pad_mask_button(int pad_index, uint16_t buttonmask)
{
  return _input_pad_mask_button(pad_index, buttonmask, 1);
}

void input_pad_frame(void)
{
  g_last_padpacket[0] = g_padpacket[0];
  g_last_padpacket[1] = g_padpacket[1];
}
