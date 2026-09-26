#ifndef INPUT_ACTION_H
#define INPUT_ACTION_H

#include "common.h"
#include "input_pad.h"

//
// The ACTION layer. Game code asks "is CONFIRM pressed?" or "which way is
// the player moving?" and never mentions a physical button.
//
// A SCHEMA is a table with one 16-bit button mask per action. There is one
// table for menus and one per gameplay layout ("Type A", "Type B" in PS1
// option-screen speak). Each pad has exactly one schema live at a time:
//
//   input_action_set_schema(P1_PAD, INPUT_SCHEMA_UI);          // menu opened
//   input_action_set_schema(P1_PAD, INPUT_SCHEMA_GAMEPLAY_B);  // back to game
//
// A mask of 0 means "this action does not exist in this schema", which is
// how the UI table makes GROW impossible while a menu is open. Masks may OR
// several buttons so that either one works:
//
//   [INPUT_ACTION_UI_CANCEL] = PAD_TRIANGLE | PAD_CIRCLE,
//
// Everything is polled. There are no callbacks: game code checks
// input_action_pressed() and friends wherever it needs to, in plain
// sequential code, and a schema switch simply applies to the next poll.
//

// ============================================================================
//  GAME-DEFINED PART: edit this section for your game.
//  Add actions here, then bind them in the tables in input_action.c.
// ============================================================================

typedef enum Input_Action Input_Action;
typedef enum Input_Schema Input_Schema;

enum Input_Action {
  // Menus
  INPUT_ACTION_UI_UP,
  INPUT_ACTION_UI_DOWN,
  INPUT_ACTION_UI_LEFT,
  INPUT_ACTION_UI_RIGHT,
  INPUT_ACTION_UI_CONFIRM,
  INPUT_ACTION_UI_CANCEL,
  INPUT_ACTION_UI_MENU,

  // Gameplay. These are the test scene's verbs; a real game will replace
  // them with its own (JUMP, ATTACK, ...). Every physical button has a job
  // so the scene can demonstrate the whole pad.
  INPUT_ACTION_MOVE_UP,
  INPUT_ACTION_MOVE_DOWN,
  INPUT_ACTION_MOVE_LEFT,
  INPUT_ACTION_MOVE_RIGHT,
  INPUT_ACTION_COLOR_NEXT,     // tap
  INPUT_ACTION_COLOR_PREV,     // tap
  INPUT_ACTION_GROW,           // hold
  INPUT_ACTION_SHRINK,         // hold
  INPUT_ACTION_BOOST,          // hold: move faster
  INPUT_ACTION_SLOW,           // hold: move slower
  INPUT_ACTION_DROP_MARKER,    // tap: leave a dot where the box is
  INPUT_ACTION_CLEAR_MARKERS,  // tap
  INPUT_ACTION_TOGGLE_OUTLINE, // tap: filled <-> hollow box
  INPUT_ACTION_RESET_POSITION, // tap: back to centre
  INPUT_ACTION_HELP,           // hold: show the live bindings on screen
  INPUT_ACTION_PAUSE,          // tap: open the menu

  INPUT_ACTION_COUNT
};

//
// Which table is live. The gameplay layouts are contiguous so a menu can
// cycle through them with + 1. This enum's value is what gets saved to the
// memory card, so only ever APPEND to it; renumbering would silently change
// what an old save means.
//
enum Input_Schema {
  INPUT_SCHEMA_UI,
  INPUT_SCHEMA_GAMEPLAY_A,
  INPUT_SCHEMA_GAMEPLAY_B,

  INPUT_SCHEMA_COUNT
};

#define INPUT_SCHEMA_GAMEPLAY_FIRST (INPUT_SCHEMA_GAMEPLAY_A)
#define INPUT_SCHEMA_GAMEPLAY_COUNT (INPUT_SCHEMA_COUNT - INPUT_SCHEMA_GAMEPLAY_FIRST)

// ============================================================================
//  ENGINE PART: generic, should not need touching per game.
// ============================================================================

void         input_action_set_schema(int pad_index, Input_Schema schema);
Input_Schema input_action_get_schema(int pad_index);

//
// All of these read through the pad's current schema; an unbound action is
// never active. Call them after VSync(0), which is when the BIOS has
// finished refreshing the pad buffers for the frame.
//
int input_action_held(int pad_index, Input_Action action);
int input_action_pressed(int pad_index, Input_Action action);
int input_action_released(int pad_index, Input_Action action);

//
// Menu auto-repeat: true on the first frame of a press, then again every
// INPUT_REPEAT_RATE_FRAMES once the action has been held for
// INPUT_REPEAT_DELAY_FRAMES. One cadence for the whole game, on purpose, so
// every menu feels the same. Tuned for 60 Hz; a 50 Hz PAL build would want
// slightly smaller numbers.
//
#define INPUT_REPEAT_DELAY_FRAMES (20)
#define INPUT_REPEAT_RATE_FRAMES  (6)

int input_action_repeat(int pad_index, Input_Action action);

//
// -1, 0 or +1 from two opposing actions (both held = 0). Digital only, by
// design: this game does not read analog sticks. Multiply by a speed to get
// pixels per frame.
//
int input_action_axis(int pad_index, Input_Action negative, Input_Action positive);

//
// Human-readable binding for options screens and debug overlays, e.g.
// "CROSS", "TRIANGLE+CIRCLE" for an OR'd mask, "-" if unbound.
//
// NOTE: returned as a static buffer, same as get_game_save_name(). Copy it
// if you need to keep it past the next call.
//
char* input_action_button_name(Input_Schema schema, Input_Action action);

#endif
