//
// INPUT TEST SCENE
//
// A coloured box you push around with the D-pad, plus a pause menu. It
// exists to exercise the input action layer end to end so anyone can plug in
// a pad (real or emulated) and see:
//
//   - gameplay context: every one of the 16 buttons does something visible
//   - UI context:       menu navigation with auto-repeat, confirm, cancel
//   - presets:          switching Type A <-> Type B live, saving the choice
//                       to the memory card, and reloading it
//   - help overlay:     hold HELP to print the live bindings, read straight
//                       out of the schema table
//
// Nothing in here names a physical button. That is the point.
//
#include "common.h"

#include "cdfs.h"
#include "memory_card.h"
#include "input_pad.h"
#include "input_action.h"
#include "render.h"

// ----------------------------------------------------------------------------
//  Save data
// ----------------------------------------------------------------------------

//
// What goes on the memory card. PS1 games kept controller config inside the
// one game save block (a separate file costs one of the card's 15 blocks),
// so this is the shape our real save will grow from.
//
// The magic + version guard against reading a stale layout from an old card.
// Bump the version whenever the struct changes.
//
#define GAME_SETTINGS_MAGIC   "SRGS"
#define GAME_SETTINGS_VERSION (1)

typedef struct Game_Settings {
  char    magic[4];
  uint8_t version;
  uint8_t gameplay_preset[2]; // Input_Gameplay_Preset, per pad
} Game_Settings;

// ----------------------------------------------------------------------------
//  Scene state
// ----------------------------------------------------------------------------

#define BOX_SPEED_NORMAL (2)  // pixels per frame
#define BOX_SPEED_BOOST  (5)
#define BOX_SPEED_SLOW   (1)
#define BOX_SIZE_SMALL   (12)
#define BOX_SIZE_NORMAL  (32)
#define BOX_SIZE_LARGE   (64)

#define MARKER_CAPACITY (24)
#define MARKER_SIZE     (4)

#define MENU_REPEAT_DELAY_FRAMES (20)
#define MENU_REPEAT_RATE_FRAMES  (6)

typedef enum Scene_Mode {
  SCENE_MODE_GAMEPLAY,
  SCENE_MODE_MENU,
} Scene_Mode;

typedef enum Menu_Item {
  MENU_ITEM_P1_PRESET,
  MENU_ITEM_P2_PRESET,
  MENU_ITEM_SAVE,
  MENU_ITEM_LOAD,
  MENU_ITEM_COUNT
} Menu_Item;

typedef struct Test_Scene {
  Scene_Mode    mode;
  Game_Settings settings;

  Rectangle32   box;
  int           box_color_index;
  int           box_outline;

  Vector2       markers[MARKER_CAPACITY];
  int           marker_count;

  int           menu_cursor;
  char          status[48]; // last save / load result, shown on screen
} Test_Scene;

static const uint8_t BOX_COLORS[][3] = {
  { 255, 200,  40 },
  {  60, 220, 120 },
  {  80, 140, 255 },
  { 255,  90,  90 },
  { 230, 230, 230 },
};

static uint8_t g_iconfile[CD_SECTOR_SIZE]; // memory card icon TIM, one CD sector

// ----------------------------------------------------------------------------
//  Memory card <-> settings
// ----------------------------------------------------------------------------

static void settings_reset(Game_Settings* settings)
{
  memory_zero(settings, sizeof(*settings));
  memcpy(settings->magic, GAME_SETTINGS_MAGIC, sizeof(settings->magic));
  settings->version            = GAME_SETTINGS_VERSION;
  settings->gameplay_preset[0] = INPUT_GAMEPLAY_PRESET_A;
  settings->gameplay_preset[1] = INPUT_GAMEPLAY_PRESET_A;
}

static int settings_valid(const Game_Settings* settings)
{
  if (memcmp(settings->magic, GAME_SETTINGS_MAGIC, sizeof(settings->magic)) != 0) return 0;
  if (settings->version != GAME_SETTINGS_VERSION)                                  return 0;
  if (settings->gameplay_preset[0] >= INPUT_GAMEPLAY_PRESET_COUNT)                 return 0;
  if (settings->gameplay_preset[1] >= INPUT_GAMEPLAY_PRESET_COUNT)                 return 0;
  return 1;
}

//
// Both of these bracket the card access with memory_card_start/stop and then
// restart the pad driver: card I/O and the pads share one BIOS IRQ handler,
// and the card side stops it (see memory_card.h).
//
static void settings_load(Test_Scene* scene)
{
  char*         savename = get_game_save_name(1, 0);
  Game_Settings loaded;

  settings_reset(&scene->settings);

  memory_card_start();
  if (memory_card_file_exists(savename) && memory_card_read(savename, &loaded, sizeof(loaded))) {
    if (settings_valid(&loaded)) {
      scene->settings = loaded;
      strcpy(scene->status, "LOADED SETTINGS FROM CARD");
    } else {
      strcpy(scene->status, "CARD SAVE INVALID, USING DEFAULTS");
    }
  } else {
    strcpy(scene->status, "NO SAVE ON CARD, USING DEFAULTS");
  }
  memory_card_stop();

  input_pad_start();
}

static void settings_save(Test_Scene* scene)
{
  char* savename = get_game_save_name(1, 0);
  int   ok;

  memory_card_start();
  ok = memory_card_write(savename, g_iconfile, &scene->settings, sizeof(scene->settings));
  memory_card_stop();

  input_pad_start();

  strcpy(scene->status, ok ? "SAVED SETTINGS TO CARD" : "SAVE FAILED (NO CARD?)");
}

// ----------------------------------------------------------------------------
//  Context switching
// ----------------------------------------------------------------------------

static void on_gameplay_action(int pad_index, Input_Action action, Input_Event event, void* user_data);
static void on_menu_action(int pad_index, Input_Action action, Input_Event event, void* user_data);

//
// Opening or closing the menu is nothing more than rebinding both pads to a
// different schema. From the next frame the same physical button means
// UI_CONFIRM instead of COLOR_NEXT, and COLOR_NEXT can no longer fire at all.
//
static void scene_enter_gameplay(Test_Scene* scene)
{
  scene->mode = SCENE_MODE_GAMEPLAY;
  for (int pad_index = 0; pad_index < 2; ++pad_index) {
    Input_Gameplay_Preset preset = scene->settings.gameplay_preset[pad_index];
    input_action_bind(pad_index, input_schema_gameplay(preset), on_gameplay_action, scene);
  }
}

static void scene_enter_menu(Test_Scene* scene)
{
  scene->mode        = SCENE_MODE_MENU;
  scene->menu_cursor = 0;
  for (int pad_index = 0; pad_index < 2; ++pad_index) {
    input_action_bind(pad_index, input_schema_ui(), on_menu_action, scene);
  }
}

static void scene_reset_box(Test_Scene* scene)
{
  scene->box.x = (RENDER_SCREEN_WIDTH  - BOX_SIZE_NORMAL) / 2;
  scene->box.y = (RENDER_SCREEN_HEIGHT - BOX_SIZE_NORMAL) / 2;
  scene->box.w = BOX_SIZE_NORMAL;
  scene->box.h = BOX_SIZE_NORMAL;
}

// ----------------------------------------------------------------------------
//  Callbacks: discrete, edge-triggered things
// ----------------------------------------------------------------------------

static void on_gameplay_action(int pad_index, Input_Action action, Input_Event event, void* user_data)
{
  Test_Scene* scene = user_data;
  int         color_count = array_count(BOX_COLORS);

  if (event != INPUT_EVENT_PRESSED) {
    return;
  }

  switch (action) {
    case INPUT_ACTION_COLOR_NEXT: {
      scene->box_color_index = (scene->box_color_index + 1) % color_count;
    } break;
    case INPUT_ACTION_COLOR_PREV: {
      scene->box_color_index = (scene->box_color_index + color_count - 1) % color_count;
    } break;
    case INPUT_ACTION_DROP_MARKER: {
      if (scene->marker_count < MARKER_CAPACITY) {
        Vector2* marker = &scene->markers[scene->marker_count++];
        marker->x = scene->box.x + scene->box.w / 2 - MARKER_SIZE / 2;
        marker->y = scene->box.y + scene->box.h / 2 - MARKER_SIZE / 2;
      }
    } break;
    case INPUT_ACTION_CLEAR_MARKERS: {
      scene->marker_count = 0;
    } break;
    case INPUT_ACTION_TOGGLE_OUTLINE: {
      scene->box_outline ^= 1;
    } break;
    case INPUT_ACTION_RESET_POSITION: {
      scene_reset_box(scene);
    } break;
    case INPUT_ACTION_PAUSE: {
      scene_enter_menu(scene);
    } break;
    default: break;
  }
}

static void menu_cycle_preset(Test_Scene* scene, int which, int direction)
{
  uint8_t* preset = &scene->settings.gameplay_preset[which];
  *preset = (*preset + INPUT_GAMEPLAY_PRESET_COUNT + direction) % INPUT_GAMEPLAY_PRESET_COUNT;
  strcpy(scene->status, "PRESET CHANGED (NOT SAVED)");
}

static void on_menu_action(int pad_index, Input_Action action, Input_Event event, void* user_data)
{
  Test_Scene* scene    = user_data;
  int         on_preset_item = scene->menu_cursor == MENU_ITEM_P1_PRESET || scene->menu_cursor == MENU_ITEM_P2_PRESET;

  if (event != INPUT_EVENT_PRESSED) {
    return;
  }

  switch (action) {
    case INPUT_ACTION_UI_LEFT: {
      if (on_preset_item) menu_cycle_preset(scene, scene->menu_cursor, -1);
    } break;
    case INPUT_ACTION_UI_RIGHT: {
      if (on_preset_item) menu_cycle_preset(scene, scene->menu_cursor, +1);
    } break;
    case INPUT_ACTION_UI_CONFIRM: {
      switch (scene->menu_cursor) {
        case MENU_ITEM_P1_PRESET:
        case MENU_ITEM_P2_PRESET: menu_cycle_preset(scene, scene->menu_cursor, +1); break;
        case MENU_ITEM_SAVE:      settings_save(scene);                             break;
        case MENU_ITEM_LOAD:      settings_load(scene);                             break;
      }
    } break;
    case INPUT_ACTION_UI_CANCEL:
    case INPUT_ACTION_UI_MENU: {
      scene_enter_gameplay(scene);
    } break;
    default: break;
  }
}

// ----------------------------------------------------------------------------
//  Per-frame update: continuous, polled things
// ----------------------------------------------------------------------------

static void scene_update(Test_Scene* scene)
{
  switch (scene->mode) {
    case SCENE_MODE_GAMEPLAY: {
      //
      // Digital axis: -1 / 0 / +1 per direction, both held cancels out.
      //
      int dx    = input_action_axis(P1_PAD, INPUT_ACTION_MOVE_LEFT, INPUT_ACTION_MOVE_RIGHT);
      int dy    = input_action_axis(P1_PAD, INPUT_ACTION_MOVE_UP,   INPUT_ACTION_MOVE_DOWN);
      int speed = BOX_SPEED_NORMAL;
      int target_size = BOX_SIZE_NORMAL;

      if (input_action_held(P1_PAD, INPUT_ACTION_BOOST))  speed = BOX_SPEED_BOOST;
      if (input_action_held(P1_PAD, INPUT_ACTION_SLOW))   speed = BOX_SPEED_SLOW;
      if (input_action_held(P1_PAD, INPUT_ACTION_GROW))   target_size = BOX_SIZE_LARGE;
      if (input_action_held(P1_PAD, INPUT_ACTION_SHRINK)) target_size = BOX_SIZE_SMALL;

      scene->box.x += dx * speed;
      scene->box.y += dy * speed;

      // ease the size towards the target, 2 px per frame, around the centre
      if (scene->box.w < target_size) { scene->box.w += 2; scene->box.x -= 1; scene->box.y -= 1; }
      if (scene->box.w > target_size) { scene->box.w -= 2; scene->box.x += 1; scene->box.y += 1; }
      scene->box.h = scene->box.w;

      scene->box.x = clamp(scene->box.x, RENDER_SCREEN_WIDTH  - scene->box.w, 0);
      scene->box.y = clamp(scene->box.y, RENDER_SCREEN_HEIGHT - scene->box.h, 0);
    } break;

    case SCENE_MODE_MENU: {
      //
      // Held-to-scroll menu navigation through the repeat helper.
      //
      if (input_action_repeat(P1_PAD, INPUT_ACTION_UI_DOWN, MENU_REPEAT_DELAY_FRAMES, MENU_REPEAT_RATE_FRAMES)) {
        scene->menu_cursor = (scene->menu_cursor + 1) % MENU_ITEM_COUNT;
      }
      if (input_action_repeat(P1_PAD, INPUT_ACTION_UI_UP, MENU_REPEAT_DELAY_FRAMES, MENU_REPEAT_RATE_FRAMES)) {
        scene->menu_cursor = (scene->menu_cursor + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
      }
    } break;
  }
}

// ----------------------------------------------------------------------------
//  Drawing
// ----------------------------------------------------------------------------

//
// One character per action, in enum order, shown when held. Lets a tester
// see exactly which actions the current schema is producing from their pad.
//
static const char ACTION_LETTERS[INPUT_ACTION_COUNT] = {
  [INPUT_ACTION_UI_UP]          = 'u',
  [INPUT_ACTION_UI_DOWN]        = 'd',
  [INPUT_ACTION_UI_LEFT]        = 'l',
  [INPUT_ACTION_UI_RIGHT]       = 'r',
  [INPUT_ACTION_UI_CONFIRM]     = 'o',
  [INPUT_ACTION_UI_CANCEL]      = 'x',
  [INPUT_ACTION_UI_MENU]        = 'm',
  [INPUT_ACTION_MOVE_UP]        = 'U',
  [INPUT_ACTION_MOVE_DOWN]      = 'D',
  [INPUT_ACTION_MOVE_LEFT]      = 'L',
  [INPUT_ACTION_MOVE_RIGHT]     = 'R',
  [INPUT_ACTION_COLOR_NEXT]     = 'C',
  [INPUT_ACTION_COLOR_PREV]     = 'c',
  [INPUT_ACTION_GROW]           = 'G',
  [INPUT_ACTION_SHRINK]         = 'g',
  [INPUT_ACTION_BOOST]          = 'B',
  [INPUT_ACTION_SLOW]           = 'b',
  [INPUT_ACTION_DROP_MARKER]    = 'M',
  [INPUT_ACTION_CLEAR_MARKERS]  = 'm',
  [INPUT_ACTION_TOGGLE_OUTLINE] = 'O',
  [INPUT_ACTION_RESET_POSITION] = 'Z',
  [INPUT_ACTION_HELP]           = '?',
  [INPUT_ACTION_PAUSE]          = '!',
};

//
// Short labels for the help overlay, in enum order.
//
static const char* const ACTION_LABELS[INPUT_ACTION_COUNT] = {
  [INPUT_ACTION_UI_UP]          = "UI UP",
  [INPUT_ACTION_UI_DOWN]        = "UI DOWN",
  [INPUT_ACTION_UI_LEFT]        = "UI LEFT",
  [INPUT_ACTION_UI_RIGHT]       = "UI RIGHT",
  [INPUT_ACTION_UI_CONFIRM]     = "CONFIRM",
  [INPUT_ACTION_UI_CANCEL]      = "CANCEL",
  [INPUT_ACTION_UI_MENU]        = "MENU",
  [INPUT_ACTION_MOVE_UP]        = "MOVE UP",
  [INPUT_ACTION_MOVE_DOWN]      = "MOVE DOWN",
  [INPUT_ACTION_MOVE_LEFT]      = "MOVE LEFT",
  [INPUT_ACTION_MOVE_RIGHT]     = "MOVE RIGHT",
  [INPUT_ACTION_COLOR_NEXT]     = "COLOR NEXT",
  [INPUT_ACTION_COLOR_PREV]     = "COLOR PREV",
  [INPUT_ACTION_GROW]           = "GROW",
  [INPUT_ACTION_SHRINK]         = "SHRINK",
  [INPUT_ACTION_BOOST]          = "BOOST",
  [INPUT_ACTION_SLOW]           = "SLOW",
  [INPUT_ACTION_DROP_MARKER]    = "DROP MARKER",
  [INPUT_ACTION_CLEAR_MARKERS]  = "CLEAR MARKERS",
  [INPUT_ACTION_TOGGLE_OUTLINE] = "OUTLINE",
  [INPUT_ACTION_RESET_POSITION] = "RESET POS",
  [INPUT_ACTION_HELP]           = "HELP",
  [INPUT_ACTION_PAUSE]          = "PAUSE",
};

static void draw_pad_debug_line(int pad_index, int y)
{
  char line[64];
  int  n = sprintf(line, "P%d TYPE %X ", pad_index + 1, input_pad_type(pad_index));

  for (int action = 0; action < INPUT_ACTION_COUNT; ++action) {
    line[n++] = input_action_held(pad_index, action) ? ACTION_LETTERS[action] : '.';
  }
  line[n] = 0;

  render_text(8, y, 0, line);
}

//
// Prints every bound action of the pad's CURRENT schema with its button
// name, pulled from the schema table itself. Flip the preset in the menu and
// come back: this list changes without any code in this file knowing how.
//
static void draw_help_overlay(int pad_index)
{
  const Input_Schema* schema = input_action_current_schema(pad_index);
  char binding[40];
  char line[64];
  int  y = 62;

  render_tile(16, 56, 288, 172, 0, 0, 0, 2);
  sprintf(line, "P%d BINDINGS (PRESET %c)", pad_index + 1, 'A' + (schema == input_schema_gameplay(INPUT_GAMEPLAY_PRESET_B)));
  render_text(24, y, 0, line);
  y += 12;

  for (int action = INPUT_ACTION_MOVE_UP; action < INPUT_ACTION_COUNT; ++action, y += 9) {
    input_schema_describe(schema, action, binding, sizeof(binding));
    sprintf(line, "%-14s %s", ACTION_LABELS[action], binding);
    render_text(24, y, 0, line);
  }
}

static void draw_box(Test_Scene* scene)
{
  const uint8_t* color = BOX_COLORS[scene->box_color_index];
  Rectangle32    b     = scene->box;

  if (scene->box_outline) {
    render_tile(b.x,           b.y,           b.w, 2,   color[0], color[1], color[2], 1);
    render_tile(b.x,           b.y + b.h - 2, b.w, 2,   color[0], color[1], color[2], 1);
    render_tile(b.x,           b.y,           2,   b.h, color[0], color[1], color[2], 1);
    render_tile(b.x + b.w - 2, b.y,           2,   b.h, color[0], color[1], color[2], 1);
  } else {
    render_tile(b.x, b.y, b.w, b.h, color[0], color[1], color[2], 1);
  }

  for (int i = 0; i < scene->marker_count; ++i) {
    render_tile(scene->markers[i].x, scene->markers[i].y, MARKER_SIZE, MARKER_SIZE, 200, 200, 200, 3);
  }
}

static void scene_draw(Test_Scene* scene)
{
  char line[64];

  draw_box(scene);

  sprintf(line, "MODE %s  P1 PRESET %c  P2 PRESET %c",
          scene->mode == SCENE_MODE_MENU ? "MENU" : "GAME",
          'A' + scene->settings.gameplay_preset[0],
          'A' + scene->settings.gameplay_preset[1]);
  render_text(8, 8, 0, line);

  draw_pad_debug_line(P1_PAD, 20);
  draw_pad_debug_line(P2_PAD, 30);

  render_text(8, 44, 0, scene->status);

  if (scene->mode == SCENE_MODE_MENU) {
    static const char* MENU_LABELS[MENU_ITEM_COUNT] = { "P1 PRESET", "P2 PRESET", "SAVE TO CARD", "LOAD FROM CARD" };
    int y = 116;

    render_tile(52, 104, 216, 92, 0, 0, 0, 2);

    for (int item = 0; item < MENU_ITEM_COUNT; ++item, y += 12) {
      char cursor = item == scene->menu_cursor ? '>' : ' ';
      if (item == MENU_ITEM_P1_PRESET || item == MENU_ITEM_P2_PRESET) {
        sprintf(line, "%c %s: < TYPE %c >", cursor, MENU_LABELS[item], 'A' + scene->settings.gameplay_preset[item]);
      } else {
        sprintf(line, "%c %s", cursor, MENU_LABELS[item]);
      }
      render_text(64, y, 0, line);
    }
    render_text(64, y + 8, 0, "CONFIRM/LEFT/RIGHT: CHANGE");
    render_text(64, y + 18, 0, "CANCEL OR MENU: BACK TO GAME");
  } else if (input_action_held(P1_PAD, INPUT_ACTION_HELP)) {
    draw_help_overlay(P1_PAD);
  } else {
    render_text(8, 220, 0, "HOLD HELP FOR BINDINGS   PAUSE FOR MENU");
  }
}

// ----------------------------------------------------------------------------
//  Entry
// ----------------------------------------------------------------------------

int main(int argc, const char **argv)
{
  static Test_Scene scene;

  render_initialize();
  cd_start();
  memory_card_initialize();
  input_pad_initialize();
  input_pad_start();

  //
  // The memory card header needs a 16x16 4bpp icon; keep loading it off the
  // disc as before.
  //
  {
    CD_File icon = cd_file_open("\\RES\\SAVICO.TIM");
    if (icon.valid) {
      cd_file_read_sync_uncached(&icon, g_iconfile, CD_SECTOR_SIZE);
    } else {
      printf("[MAIN] icon file not found on disc\n");
    }
  }

  scene_reset_box(&scene);
  settings_load(&scene);
  scene_enter_gameplay(&scene);

  printf("Hello PSX\n");

  for (;;) {
    //
    // Frame order matters:
    //   input_pad_frame     snapshot "last frame" pad state
    //   ...build frame...   (reads input that arrived during the previous vblank)
    //   render_end_frame    DrawSync + VSync: the BIOS refreshes the pad
    //                       buffers during this vblank
    //   input_action_frame  edges are now (last, new), fire callbacks
    //
    input_pad_frame();

    render_begin_frame();
    scene_update(&scene);
    scene_draw(&scene);
    render_end_frame();

    input_action_frame();
  }

  return 0;
}
