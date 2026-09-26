#include "input_action.h"

#include <psxpad.h>

// ============================================================================
//  SCHEMA TABLES: this is the "config file". Designated initialisers mean
//  anything not listed is 0 = unbound in that context.
// ============================================================================

static const Input_Schema g_schema_ui = { .buttons = {
  [INPUT_ACTION_UI_UP]      = PAD_UP,
  [INPUT_ACTION_UI_DOWN]    = PAD_DOWN,
  [INPUT_ACTION_UI_LEFT]    = PAD_LEFT,
  [INPUT_ACTION_UI_RIGHT]   = PAD_RIGHT,
  [INPUT_ACTION_UI_CONFIRM] = PAD_CROSS,
  [INPUT_ACTION_UI_CANCEL]  = PAD_TRIANGLE | PAD_CIRCLE,
  [INPUT_ACTION_UI_MENU]    = PAD_START,
}};

//
// "Type A" is the default. "Type B" mirrors it: face buttons swap
// vertically (Cross<->Square, Circle<->Triangle) and every left/right
// shoulder and stick-click pair swaps sides. D-pad, Start and Select stay
// put in both so a player can never lose the menu. Every one of the 16
// buttons is bound in each preset.
//
static const Input_Schema g_schema_gameplay[INPUT_GAMEPLAY_PRESET_COUNT] = {
  [INPUT_GAMEPLAY_PRESET_A] = { .buttons = {
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
  }},
  [INPUT_GAMEPLAY_PRESET_B] = { .buttons = {
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
  }},
};

const Input_Schema* input_schema_ui(void)
{
  return &g_schema_ui;
}

const Input_Schema* input_schema_gameplay(Input_Gameplay_Preset preset)
{
  assert(preset >= 0 && preset < INPUT_GAMEPLAY_PRESET_COUNT && "[INPUT] Bad gameplay preset.");
  return &g_schema_gameplay[preset];
}

//
// Indexed by bit position in PADTYPE.btn (see the PadButton enum).
//
static const char* const BUTTON_NAMES[16] = {
  "SELECT", "L3", "R3", "START", "UP", "RIGHT", "DOWN", "LEFT",
  "L2", "R2", "L1", "R1", "TRIANGLE", "CIRCLE", "CROSS", "SQUARE",
};

void input_schema_describe(const Input_Schema* schema, Input_Action action, char* out, int capacity)
{
  uint16_t mask    = schema ? schema->buttons[action] : 0;
  int      written = 0;

  assert(capacity > 0);
  out[0] = 0;

  if (!mask) {
    strncpy(out, "-", capacity - 1);
    out[capacity - 1] = 0;
    return;
  }

  for (int bit = 0; bit < 16; ++bit) {
    if (!(mask & (1 << bit))) {
      continue;
    }
    written += snprintf(out + written, capacity - written, "%s%s", written ? "+" : "", BUTTON_NAMES[bit]);
    if (written >= capacity - 1) {
      return; // truncated, snprintf kept it NUL-terminated
    }
  }
}

// ============================================================================
//  CONTEXTS
// ============================================================================

typedef struct Input_Context {
  const Input_Schema*   schema;
  Input_Action_Callback on_action;
  void*                 user_data;
  uint16_t              held_frames[INPUT_ACTION_COUNT]; // 0 = not held, saturates
} Input_Context;

static Input_Context g_contexts[2];

static Input_Context* _context(int pad_index)
{
  assert(pad_index >= 0 && pad_index < array_count(g_contexts) && "[INPUT] Bad pad index.");
  return &g_contexts[pad_index];
}

static uint16_t _mask(int pad_index, Input_Action action)
{
  const Input_Schema* schema = _context(pad_index)->schema;
  assert(action >= 0 && action < INPUT_ACTION_COUNT && "[INPUT] Bad action.");
  if (!schema) {
    return 0;
  }
  return schema->buttons[action];
}

void input_action_bind(int pad_index, const Input_Schema* schema, Input_Action_Callback on_action, void* user_data)
{
  Input_Context* context = _context(pad_index);
  context->schema    = schema;
  context->on_action = on_action;
  context->user_data = user_data;
  //
  // A fresh context starts with nothing held, so a button that was down
  // when the menu opened does not immediately auto-repeat in the menu.
  //
  memory_zero_fixed(context->held_frames);
}

const Input_Schema* input_action_current_schema(int pad_index)
{
  return _context(pad_index)->schema;
}

// ============================================================================
//  POLLING
// ============================================================================

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

int input_action_repeat(int pad_index, Input_Action action, int delay_frames, int rate_frames)
{
  int held = _context(pad_index)->held_frames[action];
  if (!input_action_held(pad_index, action)) {
    return 0;
  }
  //
  // held_frames is advanced in input_action_frame(), which runs before the
  // game reads input, so the first frame a button is down reads as 1.
  //
  if (held <= 1) {
    return 1;
  }
  if (held > delay_frames && rate_frames > 0) {
    return ((held - delay_frames) % rate_frames) == 0;
  }
  return 0;
}

int input_action_axis(int pad_index, Input_Action negative, Input_Action positive)
{
  return input_action_held(pad_index, positive) - input_action_held(pad_index, negative);
}

// ============================================================================
//  PER-FRAME DISPATCH
// ============================================================================

void input_action_frame(void)
{
  for (int pad_index = 0; pad_index < array_count(g_contexts); ++pad_index) {
    Input_Context*      context = &g_contexts[pad_index];
    const Input_Schema* schema  = context->schema;
    if (!schema) {
      continue;
    }

    //
    // 2 pads x ~14 actions x a couple of mask tests: nothing on an R3000.
    //
    // We walk the schema that was live at the START of the frame. A callback
    // may rebind the pad mid-loop (PAUSE switching to the UI schema); once
    // that happens the remaining actions in the old table are skipped, and
    // the new context starts fresh next frame.
    //
    for (int action = 0; action < INPUT_ACTION_COUNT && context->schema == schema; ++action) {
      uint16_t mask = schema->buttons[action];
      if (!mask) {
        continue;
      }

      if (input_pad_mask_button(pad_index, mask)) {
        if (context->held_frames[action] < UINT16_MAX) {
          context->held_frames[action]++;
        }
      } else {
        context->held_frames[action] = 0;
      }

      if (!context->on_action) {
        continue;
      }

      if (input_pad_mask_button_pressed(pad_index, mask)) {
        context->on_action(pad_index, action, INPUT_EVENT_PRESSED, context->user_data);
      } else if (input_pad_mask_button_released(pad_index, mask)) {
        context->on_action(pad_index, action, INPUT_EVENT_RELEASED, context->user_data);
      }
    }
  }
}
