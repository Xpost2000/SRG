#include "input_action.h"

#include <psxpad.h>

// ============================================================================
//  SCHEMA TABLES: this is the "config file". Designated initialisers mean
//  anything not listed is 0 = unbound in that schema.
//
//  "Type A" is the default. "Type B" mirrors it: face buttons swap
//  vertically (Cross<->Square, Circle<->Triangle) and every left/right
//  shoulder and stick-click pair swaps sides. D-pad, Start and Select stay
//  put in both so a player can never lose the menu.
// ============================================================================

static const uint16_t g_schemas[INPUT_SCHEMA_COUNT][INPUT_ACTION_COUNT] = {
  [INPUT_SCHEMA_UI] = {
    [INPUT_ACTION_UI_UP]          = PAD_UP,
    [INPUT_ACTION_UI_DOWN]        = PAD_DOWN,
    [INPUT_ACTION_UI_LEFT]        = PAD_LEFT,
    [INPUT_ACTION_UI_RIGHT]       = PAD_RIGHT,
    [INPUT_ACTION_UI_CONFIRM]     = PAD_CROSS,
    [INPUT_ACTION_UI_CANCEL]      = PAD_TRIANGLE | PAD_CIRCLE,
    [INPUT_ACTION_UI_MENU]        = PAD_START,
  },
  [INPUT_SCHEMA_GAMEPLAY_A] = {
    [INPUT_ACTION_MOVE_UP]        = PAD_UP,
    [INPUT_ACTION_MOVE_DOWN]      = PAD_DOWN,
    [INPUT_ACTION_MOVE_LEFT]      = PAD_LEFT,
    [INPUT_ACTION_MOVE_RIGHT]     = PAD_RIGHT,
    [INPUT_ACTION_COLOR_NEXT]     = PAD_CROSS,
    [INPUT_ACTION_COLOR_PREV]     = PAD_CIRCLE,
    [INPUT_ACTION_GROW]           = PAD_SQUARE,
    [INPUT_ACTION_SHRINK]         = PAD_TRIANGLE,
    [INPUT_ACTION_BOOST]          = PAD_R1,
    [INPUT_ACTION_SLOW]           = PAD_L1,
    [INPUT_ACTION_DROP_MARKER]    = PAD_R2,
    [INPUT_ACTION_CLEAR_MARKERS]  = PAD_L2,
    [INPUT_ACTION_TOGGLE_OUTLINE] = PAD_R3,
    [INPUT_ACTION_RESET_POSITION] = PAD_L3,
    [INPUT_ACTION_HELP]           = PAD_SELECT,
    [INPUT_ACTION_PAUSE]          = PAD_START,
  },
  [INPUT_SCHEMA_GAMEPLAY_B] = {
    [INPUT_ACTION_MOVE_UP]        = PAD_UP,
    [INPUT_ACTION_MOVE_DOWN]      = PAD_DOWN,
    [INPUT_ACTION_MOVE_LEFT]      = PAD_LEFT,
    [INPUT_ACTION_MOVE_RIGHT]     = PAD_RIGHT,
    [INPUT_ACTION_COLOR_NEXT]     = PAD_SQUARE,
    [INPUT_ACTION_COLOR_PREV]     = PAD_TRIANGLE,
    [INPUT_ACTION_GROW]           = PAD_CROSS,
    [INPUT_ACTION_SHRINK]         = PAD_CIRCLE,
    [INPUT_ACTION_BOOST]          = PAD_L1,
    [INPUT_ACTION_SLOW]           = PAD_R1,
    [INPUT_ACTION_DROP_MARKER]    = PAD_L2,
    [INPUT_ACTION_CLEAR_MARKERS]  = PAD_R2,
    [INPUT_ACTION_TOGGLE_OUTLINE] = PAD_L3,
    [INPUT_ACTION_RESET_POSITION] = PAD_R3,
    [INPUT_ACTION_HELP]           = PAD_SELECT,
    [INPUT_ACTION_PAUSE]          = PAD_START,
  },
};

//
// Indexed by bit position in PADTYPE.btn (see the PadButton enum).
//
static const char* const g_button_names[16] = {
  "SELECT", "L3", "R3", "START", "UP", "RIGHT", "DOWN", "LEFT",
  "L2", "R2", "L1", "R1", "TRIANGLE", "CIRCLE", "CROSS", "SQUARE",
};

//
// The only state this module has: which table each pad is reading from.
// Zero-initialised means INPUT_SCHEMA_UI, which is a harmless default.
//
static Input_Schema g_pad_schema[2];

static uint16_t _mask(int pad_index, Input_Action action)
{
  assert(pad_index >= 0 && pad_index < array_count(g_pad_schema) && "[INPUT] Bad pad index.");
  assert(action >= 0 && action < INPUT_ACTION_COUNT && "[INPUT] Bad action.");
  return g_schemas[g_pad_schema[pad_index]][action];
}

void input_action_set_schema(int pad_index, Input_Schema schema)
{
  assert(pad_index >= 0 && pad_index < array_count(g_pad_schema) && "[INPUT] Bad pad index.");
  assert(schema >= 0 && schema < INPUT_SCHEMA_COUNT && "[INPUT] Bad schema.");
  g_pad_schema[pad_index] = schema;
}

Input_Schema input_action_get_schema(int pad_index)
{
  assert(pad_index >= 0 && pad_index < array_count(g_pad_schema) && "[INPUT] Bad pad index.");
  return g_pad_schema[pad_index];
}

//
// A mask of 0 is "unbound". input_pad_mask_button(pad, 0) would return 0
// anyway (~btn & 0 == 0) but being explicit costs nothing and reads better.
//

int input_action_held(int pad_index, Input_Action action)
{
  uint16_t mask = _mask(pad_index, action);
  return mask && input_pad_mask_button(pad_index, mask);
}

int input_action_pressed(int pad_index, Input_Action action)
{
  uint16_t mask = _mask(pad_index, action);
  return mask && input_pad_mask_button_pressed(pad_index, mask);
}

int input_action_released(int pad_index, Input_Action action)
{
  uint16_t mask = _mask(pad_index, action);
  return mask && input_pad_mask_button_released(pad_index, mask);
}

int input_action_repeat(int pad_index, Input_Action action)
{
  uint16_t mask = _mask(pad_index, action);
  int      previous_frames;

  if (!mask || !input_pad_mask_button(pad_index, mask)) {
    return 0;
  }

  //
  // held_frames counts frames BEFORE this one, so 0 means "pressed just now".
  //
  previous_frames = input_pad_mask_button_held_frames(pad_index, mask);
  if (previous_frames == 0) {
    return 1;
  }
  if (previous_frames >= INPUT_REPEAT_DELAY_FRAMES) {
    return ((previous_frames - INPUT_REPEAT_DELAY_FRAMES) % INPUT_REPEAT_RATE_FRAMES) == 0;
  }
  return 0;
}

int input_action_axis(int pad_index, Input_Action negative, Input_Action positive)
{
  return input_action_held(pad_index, positive) - input_action_held(pad_index, negative);
}

char* input_action_button_name(Input_Schema schema, Input_Action action)
{
  static char result[64];
  uint16_t    mask    = g_schemas[schema][action];
  int         written = 0;

  assert(schema >= 0 && schema < INPUT_SCHEMA_COUNT && "[INPUT] Bad schema.");
  assert(action >= 0 && action < INPUT_ACTION_COUNT && "[INPUT] Bad action.");

  if (!mask) {
    strcpy(result, "-");
    return result;
  }

  result[0] = 0;
  for (int bit = 0; bit < 16; ++bit) {
    if (mask & (1 << bit)) {
      written += snprintf(result + written, sizeof(result) - written, "%s%s", written ? "+" : "", g_button_names[bit]);
    }
  }

  return result;
}
