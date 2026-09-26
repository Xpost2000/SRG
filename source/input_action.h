#ifndef INPUT_ACTION_H
#define INPUT_ACTION_H

#include "common.h"
#include "input_pad.h"

//
// The ACTION layer. Game code asks "is CONFIRM pressed?" or "which way is
// the player moving?" and never mentions a physical button.
//
// Two ideas are kept deliberately separate:
//
//   CONTEXT  = which set of actions is live right now (menus vs gameplay).
//              Decided by game state: opening the pause menu binds the UI
//              schema, closing it binds the gameplay schema back.
//
//   PRESET   = which button layout the gameplay context uses ("Type A",
//              "Type B" in PS1 option-screen speak). Decided by the player
//              and persisted to the memory card as a plain enum value.
//
// A SCHEMA is just a table: one 16-bit button mask per action. The UI
// context is one schema; each gameplay preset is another. A mask of 0
// means "this action does not exist in this context", which is how the UI
// schema makes GROW impossible while a menu is open.
//
//   schema.buttons[INPUT_ACTION_UI_CONFIRM] = PAD_CROSS | PAD_START;
//                                             ^^^^^^^^^^^^^^^^^^^^^^
//                                             OR = either button works
//
// Both polling (input_action_held/pressed/released) and callbacks
// (input_action_bind + input_action_frame) are offered. Poll for
// continuous things like movement; use callbacks for discrete things
// like pause and confirm so they do not litter the game loop.
//

// ============================================================================
//  GAME-DEFINED PART: edit this section for your game.
//  Add actions here, then bind them in the tables in input_action.c.
// ============================================================================

typedef enum Input_Action {
  // UI context
  INPUT_ACTION_UI_UP,
  INPUT_ACTION_UI_DOWN,
  INPUT_ACTION_UI_LEFT,
  INPUT_ACTION_UI_RIGHT,
  INPUT_ACTION_UI_CONFIRM,
  INPUT_ACTION_UI_CANCEL,
  INPUT_ACTION_UI_MENU,

  // Gameplay context. These are the test scene's verbs; a real game will
  // replace them with its own (JUMP, ATTACK, ...). Every physical button
  // has a job so the scene can demonstrate the whole pad.
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
  INPUT_ACTION_PAUSE,          // tap: open the menu (UI context)

  INPUT_ACTION_COUNT
} Input_Action;

//
// This is the value that goes on the memory card. Only ever append to it;
// renumbering would silently change what an old save means.
//
typedef enum Input_Gameplay_Preset {
  INPUT_GAMEPLAY_PRESET_A = 0,
  INPUT_GAMEPLAY_PRESET_B,

  INPUT_GAMEPLAY_PRESET_COUNT
} Input_Gameplay_Preset;

// ============================================================================
//  ENGINE PART: generic, should not need touching per game.
// ============================================================================

typedef struct Input_Schema {
  uint16_t buttons[INPUT_ACTION_COUNT]; // PadButton mask per action, 0 = unbound
} Input_Schema;

typedef enum Input_Event {
  INPUT_EVENT_PRESSED,
  INPUT_EVENT_RELEASED,
} Input_Event;

typedef void (*Input_Action_Callback)(int pad_index, Input_Action action, Input_Event event, void* user_data);

const Input_Schema* input_schema_ui(void);
const Input_Schema* input_schema_gameplay(Input_Gameplay_Preset preset);

//
// Human-readable name of an action's binding in a schema, e.g. "CROSS" or
// "TRIANGLE+CIRCLE" for an OR'd mask, "-" if unbound. For options screens
// and debug overlays. Writes at most capacity bytes including the NUL.
//
void input_schema_describe(const Input_Schema* schema, Input_Action action, char* out, int capacity);

//
// One active context per pad: which schema is live and who gets told about
// edges. on_action may be NULL for polling-only use. Switching between
// menus and gameplay is one call.
//
// ponytail: no context stack. If pause-over-gameplay ever needs to restore
// the previous context automatically, add push/pop on top of this.
//
void                input_action_bind(int pad_index, const Input_Schema* schema, Input_Action_Callback on_action, void* user_data);
const Input_Schema* input_action_current_schema(int pad_index);

//
// Call once per frame, AFTER VSync(0) (the pad buffers refresh in the
// vblank IRQ). Fires PRESSED/RELEASED callbacks for every bound action and
// advances the held-frame counters used by input_action_repeat.
//
void input_action_frame(void);

//
// Polling. All read through the pad's current schema; an unbound action is
// never active.
//
int input_action_held(int pad_index, Input_Action action);
int input_action_pressed(int pad_index, Input_Action action);
int input_action_released(int pad_index, Input_Action action);

//
// Menu auto-repeat: true on the first press, then again every rate_frames
// once the action has been held for delay_frames. At 60 Hz, (20, 6) feels
// like a typical console menu.
//
int input_action_repeat(int pad_index, Input_Action action, int delay_frames, int rate_frames);

//
// -1, 0 or +1 from two opposing actions (both held = 0). Digital only, by
// design: this game does not read analog sticks. This is how we get angle movement
//
int input_action_axis(int pad_index, Input_Action negative, Input_Action positive);

#endif
