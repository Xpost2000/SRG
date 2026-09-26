//
// INPUT TEST SCENE
//
// A coloured box you push around with the D-pad, plus a pause menu. It
// exists to exercise the input action layer end to end so anyone can plug in
// a pad (real or emulated) and see:
//
//   - gameplay schema:  every one of the 16 buttons does something visible
//   - UI schema:        menu navigation with auto-repeat, confirm, cancel
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
// Bump the version whenever the struct changes or a field changes meaning.
// (Version 1 stored a 0/1 preset index; version 2 stores the Input_Schema
// value directly.)
//
#define GAME_SETTINGS_MAGIC   "SRGS"
#define GAME_SETTINGS_VERSION (2)

typedef struct Game_Settings Game_Settings;

struct Game_Settings {
  char    magic[4];
  uint8_t version;
  uint8_t gameplay_schema[2]; // Input_Schema, one per pad, always a GAMEPLAY_* value
};

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

typedef struct Test_Scene Test_Scene;

enum Scene_Mode {
  SCENE_MODE_GAMEPLAY,
  SCENE_MODE_MENU,
};

enum Menu_Item {
  MENU_ITEM_P1_SCHEMA,
  MENU_ITEM_P2_SCHEMA,
  MENU_ITEM_SAVE,
  MENU_ITEM_LOAD,
  MENU_ITEM_COUNT
};

struct Test_Scene {
  int           mode; // Scene_Mode
  Game_Settings settings;

  Rectangle32   box;
  int           box_color_index;
  int           box_outline;

  Vector2       markers[MARKER_CAPACITY];
  int           marker_count;

  int           menu_cursor; // Menu_Item
  char          status[48];  // last save / load result, shown on screen
};

static const uint8_t g_box_colors[][3] = {
  { 255, 200,  40 },
  {  60, 220, 120 },
  {  80, 140, 255 },
  { 255,  90,  90 },
  { 230, 230, 230 },
};

//
// Short labels for the help overlay and the held-action debug line, in
// Input_Action order.
//
static const char* const g_action_labels[INPUT_ACTION_COUNT] = {
  [INPUT_ACTION_UI_UP]          = "UI UP",
  [INPUT_ACTION_UI_DOWN]        = "UI DOWN",
  [INPUT_ACTION_UI_LEFT]        = "UI LEFT",
  [INPUT_ACTION_UI_RIGHT]       = "UI RIGHT",
  [INPUT_ACTION_UI_CONFIRM]     = "CONFIRM",
  [INPUT_ACTION_UI_CANCEL]      = "CANCEL",
  [INPUT_ACTION_UI_MENU]        = "MENU",
  [INPUT_ACTION_MOVE_UP]        = "UP",
  [INPUT_ACTION_MOVE_DOWN]      = "DOWN",
  [INPUT_ACTION_MOVE_LEFT]      = "LEFT",
  [INPUT_ACTION_MOVE_RIGHT]     = "RIGHT",
  [INPUT_ACTION_COLOR_NEXT]     = "COLOR+",
  [INPUT_ACTION_COLOR_PREV]     = "COLOR-",
  [INPUT_ACTION_GROW]           = "GROW",
  [INPUT_ACTION_SHRINK]         = "SHRINK",
  [INPUT_ACTION_BOOST]          = "BOOST",
  [INPUT_ACTION_SLOW]           = "SLOW",
  [INPUT_ACTION_DROP_MARKER]    = "MARKER",
  [INPUT_ACTION_CLEAR_MARKERS]  = "CLEAR",
  [INPUT_ACTION_TOGGLE_OUTLINE] = "OUTLINE",
  [INPUT_ACTION_RESET_POSITION] = "RESET",
  [INPUT_ACTION_HELP]           = "HELP",
  [INPUT_ACTION_PAUSE]          = "PAUSE",
};

static const char* const g_menu_labels[MENU_ITEM_COUNT] = {
  "P1 LAYOUT", "P2 LAYOUT", "SAVE TO CARD", "LOAD FROM CARD",
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
  settings->gameplay_schema[0] = INPUT_SCHEMA_GAMEPLAY_A;
  settings->gameplay_schema[1] = INPUT_SCHEMA_GAMEPLAY_A;
}

static int settings_valid(const Game_Settings* settings)
{
  if (memcmp(settings->magic, GAME_SETTINGS_MAGIC, sizeof(settings->magic)) != 0) return 0;
  if (settings->version != GAME_SETTINGS_VERSION)                                  return 0;

  for (int pad_index = 0; pad_index < 2; ++pad_index) {
    int schema = settings->gameplay_schema[pad_index];
    if (schema < INPUT_SCHEMA_GAMEPLAY_FIRST || schema >= INPUT_SCHEMA_COUNT) return 0;
  }
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
//  Mode switching
// ----------------------------------------------------------------------------

//
// Opening or closing the menu is nothing more than pointing both pads at a
// different schema. From the next poll the same physical button means
// UI_CONFIRM instead of COLOR_NEXT, and COLOR_NEXT can no longer fire at all.
//
static void scene_enter_gameplay(Test_Scene* scene)
{
  scene->mode = SCENE_MODE_GAMEPLAY;
  for (int pad_index = 0; pad_index < 2; ++pad_index) {
    input_action_set_schema(pad_index, scene->settings.gameplay_schema[pad_index]);
  }
}

static void scene_enter_menu(Test_Scene* scene)
{
  scene->mode        = SCENE_MODE_MENU;
  scene->menu_cursor = 0;
  for (int pad_index = 0; pad_index < 2; ++pad_index) {
    input_action_set_schema(pad_index, INPUT_SCHEMA_UI);
  }
}

static void scene_reset_box(Test_Scene* scene)
{
  scene->box.x = (RENDER_SCREEN_WIDTH  - BOX_SIZE_NORMAL) / 2;
  scene->box.y = (RENDER_SCREEN_HEIGHT - BOX_SIZE_NORMAL) / 2;
  scene->box.w = BOX_SIZE_NORMAL;
  scene->box.h = BOX_SIZE_NORMAL;
}

//
// Steps a pad's saved gameplay layout forward or back, wrapping. The layouts
// are contiguous in Input_Schema so this is plain modular arithmetic.
//
static void settings_cycle_schema(Test_Scene* scene, int pad_index, int direction)
{
  int index = scene->settings.gameplay_schema[pad_index] - INPUT_SCHEMA_GAMEPLAY_FIRST;
  index = (index + INPUT_SCHEMA_GAMEPLAY_COUNT + direction) % INPUT_SCHEMA_GAMEPLAY_COUNT;
  scene->settings.gameplay_schema[pad_index] = INPUT_SCHEMA_GAMEPLAY_FIRST + index;
  strcpy(scene->status, "LAYOUT CHANGED (NOT SAVED)");
}

// ----------------------------------------------------------------------------
//  Per-frame update: everything is a poll
// ----------------------------------------------------------------------------

static void scene_update_gameplay(Test_Scene* scene)
{
  int color_count = array_count(g_box_colors);
  int dx          = input_action_axis(P1_PAD, INPUT_ACTION_MOVE_LEFT, INPUT_ACTION_MOVE_RIGHT);
  int dy          = input_action_axis(P1_PAD, INPUT_ACTION_MOVE_UP,   INPUT_ACTION_MOVE_DOWN);
  int speed       = BOX_SPEED_NORMAL;
  int target_size = BOX_SIZE_NORMAL;

  //
  // Held things.
  //
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

  //
  // Tapped things. pressed() is true for exactly one frame per press.
  //
  if (input_action_pressed(P1_PAD, INPUT_ACTION_COLOR_NEXT)) {
    scene->box_color_index = (scene->box_color_index + 1) % color_count;
  }
  if (input_action_pressed(P1_PAD, INPUT_ACTION_COLOR_PREV)) {
    scene->box_color_index = (scene->box_color_index + color_count - 1) % color_count;
  }
  if (input_action_pressed(P1_PAD, INPUT_ACTION_DROP_MARKER) && scene->marker_count < MARKER_CAPACITY) {
    Vector2* marker = &scene->markers[scene->marker_count++];
    marker->x = scene->box.x + scene->box.w / 2 - MARKER_SIZE / 2;
    marker->y = scene->box.y + scene->box.h / 2 - MARKER_SIZE / 2;
  }
  if (input_action_pressed(P1_PAD, INPUT_ACTION_CLEAR_MARKERS)) {
    scene->marker_count = 0;
  }
  if (input_action_pressed(P1_PAD, INPUT_ACTION_TOGGLE_OUTLINE)) {
    scene->box_outline ^= 1;
  }
  if (input_action_pressed(P1_PAD, INPUT_ACTION_RESET_POSITION)) {
    scene_reset_box(scene);
  }
  if (input_action_pressed(P1_PAD, INPUT_ACTION_PAUSE)) {
    scene_enter_menu(scene);
  }
}

static void scene_update_menu(Test_Scene* scene)
{
  int on_layout_item = scene->menu_cursor == MENU_ITEM_P1_SCHEMA || scene->menu_cursor == MENU_ITEM_P2_SCHEMA;

  //
  // Held-to-scroll cursor movement through the repeat helper.
  //
  if (input_action_repeat(P1_PAD, INPUT_ACTION_UI_DOWN)) {
    scene->menu_cursor = (scene->menu_cursor + 1) % MENU_ITEM_COUNT;
  }
  if (input_action_repeat(P1_PAD, INPUT_ACTION_UI_UP)) {
    scene->menu_cursor = (scene->menu_cursor + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
  }

  if (on_layout_item && input_action_pressed(P1_PAD, INPUT_ACTION_UI_LEFT)) {
    settings_cycle_schema(scene, scene->menu_cursor, -1);
  }
  if (on_layout_item && input_action_pressed(P1_PAD, INPUT_ACTION_UI_RIGHT)) {
    settings_cycle_schema(scene, scene->menu_cursor, +1);
  }

  if (input_action_pressed(P1_PAD, INPUT_ACTION_UI_CONFIRM)) {
    switch (scene->menu_cursor) {
      case MENU_ITEM_P1_SCHEMA:
      case MENU_ITEM_P2_SCHEMA: {
        settings_cycle_schema(scene, scene->menu_cursor, +1);
      } break;
      case MENU_ITEM_SAVE: {
        settings_save(scene);
      } break;
      case MENU_ITEM_LOAD: {
        settings_load(scene);
      } break;
    }
  }

  if (input_action_pressed(P1_PAD, INPUT_ACTION_UI_CANCEL) ||
      input_action_pressed(P1_PAD, INPUT_ACTION_UI_MENU)) {
    scene_enter_gameplay(scene);
  }
}

static void scene_update(Test_Scene* scene)
{
  switch (scene->mode) {
    case SCENE_MODE_GAMEPLAY: {
      scene_update_gameplay(scene);
    } break;
    case SCENE_MODE_MENU: {
      scene_update_menu(scene);
    } break;
  }
}

// ----------------------------------------------------------------------------
//  Drawing
// ----------------------------------------------------------------------------

static char schema_letter(int schema)
{
  return (schema >= INPUT_SCHEMA_GAMEPLAY_FIRST) ? 'A' + (schema - INPUT_SCHEMA_GAMEPLAY_FIRST) : 'U';
}

//
// "P1 TYPE 4  UP RIGHT BOOST" - the labels of whatever actions the pad's
// current schema is producing right now. Lets a tester see the mapping live.
//
static void draw_pad_debug_line(int pad_index, int y)
{
  char line[64];
  int  n = sprintf(line, "P%d TYPE %X ", pad_index + 1, input_pad_type(pad_index));

  for (int action = 0; action < INPUT_ACTION_COUNT && n < 40; ++action) {
    if (input_action_held(pad_index, action)) {
      n += snprintf(line + n, sizeof(line) - n, " %s", g_action_labels[action]);
    }
  }

  render_text(8, y, 0, line);
}

//
// Prints every gameplay action with its button name for the pad's CURRENT
// schema, pulled from the schema table itself. Flip the layout in the menu
// and come back: this list changes without this file knowing how.
//
static void draw_help_overlay(int pad_index)
{
  Input_Schema schema = input_action_get_schema(pad_index);
  char         line[64];
  int          y = 62;

  render_tile(16, 56, 288, 172, 0, 0, 0, 2);
  sprintf(line, "P%d BINDINGS (LAYOUT %c)", pad_index + 1, schema_letter(schema));
  render_text(24, y, 0, line);
  y += 12;

  for (int action = INPUT_ACTION_MOVE_UP; action < INPUT_ACTION_COUNT; ++action, y += 9) {
    sprintf(line, "%-8s %s", g_action_labels[action], input_action_button_name(schema, action));
    render_text(24, y, 0, line);
  }
}

static void draw_box(Test_Scene* scene)
{
  const uint8_t* color = g_box_colors[scene->box_color_index];
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

static void draw_menu(Test_Scene* scene)
{
  char line[64];
  int  y = 116;

  render_tile(52, 104, 216, 92, 0, 0, 0, 2);

  for (int item = 0; item < MENU_ITEM_COUNT; ++item, y += 12) {
    char cursor = item == scene->menu_cursor ? '>' : ' ';
    if (item == MENU_ITEM_P1_SCHEMA || item == MENU_ITEM_P2_SCHEMA) {
      sprintf(line, "%c %s: < TYPE %c >", cursor, g_menu_labels[item], schema_letter(scene->settings.gameplay_schema[item]));
    } else {
      sprintf(line, "%c %s", cursor, g_menu_labels[item]);
    }
    render_text(64, y, 0, line);
  }
  render_text(64, y + 8,  0, "CONFIRM/LEFT/RIGHT: CHANGE");
  render_text(64, y + 18, 0, "CANCEL OR MENU: BACK TO GAME");
}

static void scene_draw(Test_Scene* scene)
{
  char line[64];

  draw_box(scene);

  sprintf(line, "MODE %s  P1 LAYOUT %c  P2 LAYOUT %c",
          scene->mode == SCENE_MODE_MENU ? "MENU" : "GAME",
          schema_letter(scene->settings.gameplay_schema[0]),
          schema_letter(scene->settings.gameplay_schema[1]));
  render_text(8, 8, 0, line);

  draw_pad_debug_line(P1_PAD, 20);
  draw_pad_debug_line(P2_PAD, 30);

  render_text(8, 44, 0, scene->status);

  if (scene->mode == SCENE_MODE_MENU) {
    draw_menu(scene);
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
  
  {
    uintptr_t remaining_memory = system_get_remaining_allocatable_memory();
    _debugprintf("[MEMORY]: %d bytes, %d kb, %d mb left\n", remaining_memory, remaining_memory / 1024, remaining_memory / (1024*1024));
  }

  scene_reset_box(&scene);
  settings_load(&scene);
  scene_enter_gameplay(&scene);

  printf("Hello PSX\n");

  for (;;) {
    //
    // Frame order matters. pressed() means "down now, up in the snapshot",
    // so the snapshot has to be taken from the OLD packet, then a vblank
    // has to deliver a NEW packet, and only then may we poll:
    //
    //   scene_update       poll actions: "now" is the packet from the last
    //                      vblank, "last frame" is the snapshot before it
    //   scene_draw         build the frame
    //   input_pad_frame    snapshot the packet we just finished reading and
    //                      bump the held-frame counters
    //   render_end_frame   DrawSync + VSync: the BIOS writes a fresh packet
    //                      during this vblank, ready for the next poll
    //
    // Calling input_pad_frame() right before scene_update() instead would
    // make snapshot and current identical, and no tap would ever register.
    //
    scene_update(&scene);
    scene_draw(&scene);
    input_pad_frame();
    render_end_frame();
  }

  return 0;
}
