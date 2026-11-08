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

#include "ps1_mem_limit.h"
#include "filesystem.h"
#include "memory_card.h"
#include "input_pad.h"
#include "input_action.h"
#include "render.h"
#include "font.h"
#include "vram.h"
#include "audio.h"
#include "fileslist.g.h"

//
// psxgpu just to directly create primitives for font testing
//
#include <psxgpu.h>

static int d = -2572;
static int last_vblank = 0;
static int vblank_now = 0;
static int frame_divisor = 0;
static int intensity = 1;

//
// TEST START
//
#define CUBE_VERTS 36

SVECTOR cube_verts[CUBE_VERTS] = {
  // -Z face
  { -100, -100, -100, 0 },
  {  100, -100, -100, 0 },
  { -100,  100, -100, 0 },

  {  100, -100, -100, 0 },
  {  100,  100, -100, 0 },
  { -100,  100, -100, 0 },

  // +Z face
  {  100, -100,  100, 0 },
  { -100, -100,  100, 0 },
  {  100,  100,  100, 0 },

  { -100, -100,  100, 0 },
  { -100,  100,  100, 0 },
  {  100,  100,  100, 0 },

  // -Y face
  { -100, -100,  100, 0 },
  {  100, -100,  100, 0 },
  { -100, -100, -100, 0 },

  {  100, -100,  100, 0 },
  {  100, -100, -100, 0 },
  { -100, -100, -100, 0 },

  // +Y face
  {  100,  100,  100, 0 },
  { -100,  100,  100, 0 },
  {  100,  100, -100, 0 },

  { -100,  100,  100, 0 },
  { -100,  100, -100, 0 },
  {  100,  100, -100, 0 },

  // -X face
  { -100, -100, -100, 0 },
  { -100,  100, -100, 0 },
  { -100, -100,  100, 0 },

  { -100, -100,  100, 0 },
  { -100,  100, -100, 0 },
  { -100,  100,  100, 0 },

  // +X face
  {  100,  100, -100, 0 },
  {  100, -100, -100, 0 },
  {  100, -100,  100, 0 },

  {  100, -100, -100, 0 },
  {  100, -100,  100, 0 },
  {  100,  100,  100, 0 },
};

SVECTOR cube_norms[CUBE_VERTS] = {
  // -Z face
  { 0, 0, -ONE, 0 }, { 0, 0, -ONE, 0 }, { 0, 0, -ONE, 0 },
  { 0, 0, -ONE, 0 }, { 0, 0, -ONE, 0 }, { 0, 0, -ONE, 0 },

  // +Z face
  { 0, 0, ONE, 0 }, { 0, 0, ONE, 0 }, { 0, 0, ONE, 0 },
  { 0, 0, ONE, 0 }, { 0, 0, ONE, 0 }, { 0, 0, ONE, 0 },

  // -Y face
  { 0, -ONE, 0, 0 }, { 0, -ONE, 0, 0 }, { 0, -ONE, 0, 0 },
  { 0, -ONE, 0, 0 }, { 0, -ONE, 0, 0 }, { 0, -ONE, 0, 0 },

  // +Y face
  { 0, ONE, 0, 0 }, { 0, ONE, 0, 0 }, { 0, ONE, 0, 0 },
  { 0, ONE, 0, 0 }, { 0, ONE, 0, 0 }, { 0, ONE, 0, 0 },

  // -X face
  { -ONE, 0, 0, 0 }, { -ONE, 0, 0, 0 }, { -ONE, 0, 0, 0 },
  { -ONE, 0, 0, 0 }, { -ONE, 0, 0, 0 }, { -ONE, 0, 0, 0 },

  // +X face
  { ONE, 0, 0, 0 }, { ONE, 0, 0, 0 }, { ONE, 0, 0, 0 },
  { ONE, 0, 0, 0 }, { ONE, 0, 0, 0 }, { ONE, 0, 0, 0 },
};

SVECTOR cube_colors[CUBE_VERTS] = {
  // -Z face
  { 255, 0, 0, 0 }, { 255, 0, 0, 0 }, { 255, 0, 0, 0 },
  { 255, 0, 0, 0 }, { 255, 0, 0, 0 }, { 255, 0, 0, 0 },

  // +Z face
  { 0, 255, 0, 0 }, { 0, 255, 0, 0 }, { 0, 255, 0, 0 },
  { 0, 255, 0, 0 }, { 0, 255, 0, 0 }, { 0, 255, 0, 0 },

  // -Y face
  { 0, 0, 255, 0 }, { 0, 0, 255, 0 }, { 0, 0, 255, 0 },
  { 0, 0, 255, 0 }, { 0, 0, 255, 0 }, { 0, 0, 255, 0 },

  // +Y face
  { 255, 255, 0, 0 }, { 255, 255, 0, 0 }, { 255, 255, 0, 0 },
  { 255, 255, 0, 0 }, { 255, 255, 0, 0 }, { 255, 255, 0, 0 },

  // -X face
  { 255, 0, 255, 0 }, { 255, 0, 255, 0 }, { 255, 0, 255, 0 },
  { 255, 0, 255, 0 }, { 255, 0, 255, 0 }, { 255, 0, 255, 0 },

  // +X face
  { 0, 255, 255, 0 }, { 0, 255, 255, 0 }, { 0, 255, 255, 0 },
  { 0, 255, 255, 0 }, { 0, 255, 255, 0 }, { 0, 255, 255, 0 },
};

Render_Mesh g_testcube;
Render_Mesh g_m1;
Render_Mesh g_m2;

//
// TEST END
//


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

#define LOGO_SPEED       (2)  // pixels per frame, each axis
#define LOGO_CAPTION     ("RETRO")
#define LOGO_CAPTION_GAP (2)
#define LOGO_Z           (5)

//
// 16 bytes each, so this is 2 packet buffers worth: it has to flush at least
// once a frame.
//
#define RENDER_STRESS_TILE_COUNT (3000)
#define RENDER_STRESS_TILE_SIZE  (4)

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
  MENU_ITEM_MUSIC, // start / fade out the sequenced test song
  MENU_ITEM_RENDER_STRESS,
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

  Vector2       logo_position;
  Vector2       logo_velocity;
  int           logo_color_index;
  int           render_stress; // deliberately overflow the 2D packet buffer
};

static const uint8_t g_box_colors[][3] = {
  { 255, 200,  40 },
  {  60, 220, 120 },
  {  80, 140, 255 },
  { 255,  90,  90 },
  { 230, 230, 230 },
};

//
// These multiply a white texture, where 128 means unchanged (see Render_Color)
//
static const Render_Color g_logo_colors[] = {
  { 128, 128, 128 },
  { 128,  40,  40 },
  {  40, 128,  60 },
  {  50,  80, 128 },
  { 128, 110,  20 },
  { 110,  50, 128 },
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
  "P1 LAYOUT", "P2 LAYOUT", "SAVE TO CARD", "LOAD FROM CARD", "MUSIC", "2D STRESS",
};

static uint8_t      g_iconfile[CD_SECTOR_SIZE]; // memory card icon TIM, one CD sector
static Font         g_font1;
static Font         g_font2;
static Vram_Texture g_logo;

//
// Files that end up in VRAM only pass through here at load time. The GPU's DMA
// needs word alignment and a byte array makes no such promise on its own.
//
static uint8_t g_texturefile[2048 * 3] __attribute__((aligned(4))); // biggest is 5KB, but just in case.

static uint8_t g_meshfile1[2048 * 20]; // they're actually like 30KB but just in case.
static uint8_t g_meshfile2[2048 * 20];

//
// Audio test song, MML text from res/audio (see engine-docs/audio.md): a
// four-bar loop. Sound effects are single notes on bank instruments, so they
// need no handle at all.
//
#define MUSIC_TEST_PATH   ("\\AUDIO\\THEME.MML")
#define MUSIC_FADE_FRAMES (30)

static Audio_Song g_song_theme = AUDIO_SONG_INVALID;

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
//  Audio test helpers
// ----------------------------------------------------------------------------

static void music_toggle(Test_Scene* scene)
{
  if (audio_music_is_playing()) {
    audio_music_fade_out(MUSIC_FADE_FRAMES);
    strcpy(scene->status, "MUSIC FADING OUT");
  } else {
    audio_music_play(g_song_theme, 1);
    audio_music_fade_in(MUSIC_FADE_FRAMES);
    strcpy(scene->status, audio_music_is_playing() ? "MUSIC PLAYING FROM RAM" : "MUSIC FAILED TO START");
  }
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
  int dx          = input_action_axis(P1_PAD, INPUT_ACTION_MOVE_LEFT, INPUT_ACTION_MOVE_RIGHT) * (vblank_now - last_vblank);
  int dy          = input_action_axis(P1_PAD, INPUT_ACTION_MOVE_UP,   INPUT_ACTION_MOVE_DOWN) * (vblank_now - last_vblank);
  int speed       = BOX_SPEED_NORMAL;
  int target_size = BOX_SIZE_NORMAL;

  //
  // Held things.
  //
  if (input_action_held(P1_PAD, INPUT_ACTION_BOOST))  speed = BOX_SPEED_BOOST;
  if (input_action_held(P1_PAD, INPUT_ACTION_SLOW))   speed = BOX_SPEED_SLOW;
  if (input_action_held(P1_PAD, INPUT_ACTION_GROW))   target_size = BOX_SIZE_LARGE;
  if (input_action_held(P1_PAD, INPUT_ACTION_SHRINK)) target_size = BOX_SIZE_SMALL;

  if (input_action_pressed(P1_PAD, INPUT_ACTION_GROW))  intensity++;
  if (input_action_pressed(P1_PAD, INPUT_ACTION_SHRINK))  intensity--;

  d += dx * 4;
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
  //
  // Every tap makes a noise so the voice pool can be heard working: mash
  // these and the blips overlap instead of cutting each other off. COLOR-
  // plays the same instrument an octave down to show pitch is free, and
  // CLEAR plays a snare from the same bank the music uses.
  //
  if (input_action_pressed(P1_PAD, INPUT_ACTION_COLOR_NEXT)) {
    scene->box_color_index = (scene->box_color_index + 1) % color_count;
    audio_sfx_play(AUDIO_INSTRUMENT_BLIP, AUDIO_NOTE_ROOT, AUDIO_VOLUME_MAX, AUDIO_VOLUME_MAX);
  }
  if (input_action_pressed(P1_PAD, INPUT_ACTION_COLOR_PREV)) {
    scene->box_color_index = (scene->box_color_index + color_count - 1) % color_count;
    audio_sfx_play(AUDIO_INSTRUMENT_BLIP, AUDIO_NOTE_ROOT - 12, AUDIO_VOLUME_MAX, AUDIO_VOLUME_MAX);
  }
  if (input_action_pressed(P1_PAD, INPUT_ACTION_DROP_MARKER) && scene->marker_count < MARKER_CAPACITY) {
    Vector2* marker = &scene->markers[scene->marker_count++];
    marker->x = scene->box.x + scene->box.w / 2 - MARKER_SIZE / 2;
    marker->y = scene->box.y + scene->box.h / 2 - MARKER_SIZE / 2;
    // panned to where the box is: left edge = left speaker
    audio_sfx_play(AUDIO_INSTRUMENT_BLIP, AUDIO_NOTE_ROOT,
                   AUDIO_VOLUME_MAX - (AUDIO_VOLUME_MAX * marker->x / RENDER_SCREEN_WIDTH),
                   AUDIO_VOLUME_MAX * marker->x / RENDER_SCREEN_WIDTH);
  }
  if (input_action_pressed(P1_PAD, INPUT_ACTION_CLEAR_MARKERS)) {
    scene->marker_count = 0;
    audio_sfx_play(AUDIO_INSTRUMENT_SNARE, AUDIO_NOTE_ROOT, AUDIO_VOLUME_MAX, AUDIO_VOLUME_MAX);
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
    case MENU_ITEM_MUSIC: {
      music_toggle(scene);
    } break;
    case MENU_ITEM_RENDER_STRESS: {
      scene->render_stress ^= 1;
    } break;
    }
  }

  if (input_action_pressed(P1_PAD, INPUT_ACTION_UI_CANCEL) ||
      input_action_pressed(P1_PAD, INPUT_ACTION_UI_MENU)) {
    scene_enter_gameplay(scene);
  }
}

//
// The screensaver. Each wall reverses one axis, and the colour changes so that
// a hit is easy to see (and hear).
//
static void scene_update_logo(Test_Scene* scene)
{
  int limit_x = RENDER_SCREEN_WIDTH  - g_logo.width;
  int limit_y = RENDER_SCREEN_HEIGHT - g_logo.height - LOGO_CAPTION_GAP - g_font1.glyph_height;
  int bounced = 0;

  scene->logo_position.x += scene->logo_velocity.x * frame_divisor;
  scene->logo_position.y += scene->logo_velocity.y * frame_divisor;

  if (scene->logo_position.x < 0 || scene->logo_position.x > limit_x) {
    scene->logo_velocity.x = -scene->logo_velocity.x;
    scene->logo_position.x = clamp(scene->logo_position.x, limit_x, 0);
    bounced = 1;
  }
  if (scene->logo_position.y < 0 || scene->logo_position.y > limit_y) {
    scene->logo_velocity.y = -scene->logo_velocity.y;
    scene->logo_position.y = clamp(scene->logo_position.y, limit_y, 0);
    bounced = 1;
  }

  if (bounced) {
    scene->logo_color_index = (scene->logo_color_index + 1) % array_count(g_logo_colors);
    audio_sfx_play(AUDIO_INSTRUMENT_BLIP, AUDIO_NOTE_ROOT, AUDIO_VOLUME_MAX / 4, AUDIO_VOLUME_MAX / 4);
  }
}

static void scene_update(Test_Scene* scene)
{
  scene_update_logo(scene);

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

  render_tile(52, 104, 216, 116, 0, 0, 0, 2);

  for (int item = 0; item < MENU_ITEM_COUNT; ++item, y += 12) {
    char cursor = item == scene->menu_cursor ? '>' : ' ';
    if (item == MENU_ITEM_P1_SCHEMA || item == MENU_ITEM_P2_SCHEMA) {
      sprintf(line, "%c %s: < TYPE %c >", cursor, g_menu_labels[item], schema_letter(scene->settings.gameplay_schema[item]));
    } else if (item == MENU_ITEM_MUSIC) {
      sprintf(line, "%c %s: %s", cursor, g_menu_labels[item], audio_music_is_playing() ? "STOP" : "PLAY");
    } else if (item == MENU_ITEM_RENDER_STRESS) {
      sprintf(line, "%c %s: %s", cursor, g_menu_labels[item], scene->render_stress ? "ON" : "OFF");
    } else {
      sprintf(line, "%c %s", cursor, g_menu_labels[item]);
    }
    render_text(64, y, 0, line);
  }
  render_text(64, y + 8,  0, "CONFIRM/LEFT/RIGHT: CHANGE");
  render_text(64, y + 18, 0, "CANCEL OR MENU: BACK TO GAME");
}

static void draw_logo(Test_Scene* scene)
{
  Render_Color color        = g_logo_colors[scene->logo_color_index];
  Vector2      caption_size = font_get_cstr_dimensions(&g_font1, LOGO_CAPTION);
  Vector2      caption_position;

  caption_position.x = scene->logo_position.x + (g_logo.width - caption_size.x) / 2;
  caption_position.y = scene->logo_position.y + g_logo.height + LOGO_CAPTION_GAP;

  render_sprite(&g_logo, (Rectangle32) {0, 0, g_logo.width, g_logo.height}, scene->logo_position, color, LOGO_Z);
  render_font_text(&g_font1, caption_position, LOGO_CAPTION, color, LOGO_Z);
}

//
// Drawn before everything else, so that the flushes it causes all land
// underneath the rest of the scene. 7 and 13 only exist to scatter the tiles.
//
static void draw_render_stress(void)
{
  int i;

  for (i = 0; i < RENDER_STRESS_TILE_COUNT; ++i) {
    render_tile((i * 7)  % (RENDER_SCREEN_WIDTH  - RENDER_STRESS_TILE_SIZE),
                (i * 13) % (RENDER_SCREEN_HEIGHT - RENDER_STRESS_TILE_SIZE),
                RENDER_STRESS_TILE_SIZE, RENDER_STRESS_TILE_SIZE,
                i & 0x7F, (i * 3) & 0x7F, (i * 5) & 0x7F, RENDER_2D_OT_LENGTH - 1);
  }
}

static void scene_draw(Test_Scene* scene)
{
  char         line[64];
  Render_Stats stats = render_get_stats();

  if (scene->render_stress) {
    draw_render_stress();
  }

  draw_logo(scene);
  draw_box(scene);

  sprintf(line, "2D PEAK: %d, FLUSH: %d, ANG: %d", stats.packets_2d_peak, stats.flushes_last_frame, d);
  render_text(8, 8, 0, line);

  draw_pad_debug_line(P1_PAD, 20);
  draw_pad_debug_line(P2_PAD, 30);

  render_text(8, 44, 0, scene->status);

  render_text(8, 54, 0, audio_music_is_playing() ? "MUSIC ON" : "MUSIC OFF  (PAUSE > MUSIC)");

  if (scene->mode == SCENE_MODE_MENU) {
    draw_menu(scene);
  } else if (input_action_held(P1_PAD, INPUT_ACTION_HELP)) {
    draw_help_overlay(P1_PAD);
  } else {
    render_text(8, 220, 0, "HOLD HELP FOR BINDINGS   PAUSE FOR MENU");
  }

  render_font_text(&g_font1, (Vector2) {100, 100}, "HI SRG TEAM", (Render_Color) {128, 128, 128}, 0);
  render_font_text(&g_font2, (Vector2) {100, 116}, "Another Font,\nOh man!", (Render_Color) {128, 128, 128}, 0);
}

// ----------------------------------------------------------------------------
//  Loading textures
// ----------------------------------------------------------------------------

//
// Fonts are a TIM with extra metadata, so once the TIM is found these all upload
// the same way. The font only keeps its glyph table and where it ended up.
//
static Font load_font_to_vram(uint8_t* data, int size)
{
  Font         result;
  TIM_IMAGE    tim;
  Vram_Texture texture;

  result  = font_load_from_memory(data, size);
  tim     = font_get_tim_info(&result);
  texture = vram_upload_tim(&tim);

  font_set_vram_information(&result, (Vector2) {texture.image_rect.x, texture.image_rect.y}, texture.clut_location);

  return result;
}

static Vram_Texture load_tim_to_vram(uint8_t* data)
{
  TIM_IMAGE tim;

  GetTimInfo((const uint32_t*) data, &tim);

  return vram_upload_tim(&tim);
}

//
// Walks the build-time list so that uploads arrive tallest first, which is what
// the VRAM shelves want. Adding an image to iso.xml adds it to this list.
//
static void load_textures(void)
{
  int i;

  vram_reset();

  for (i = 0; i < TEXTURE_ORDERED_FILE_COUNT; ++i) {
    uint16_t    file_id = texture_ordered_file_id_table[i];
    File_Handle file;
    int         bytes;

    //
    // The card icon goes to the memory card, it is never drawn by the GPU.
    //
    if (file_id == FIDX__RES_SAVICO_TIM) {
      continue;
    }

    file = filesystem_open(file_strings_table[file_id]);

    if (file == 0) {
      printf("[MAIN] texture file not found on disc: %s\n", file_strings_table[file_id]);
      continue;
    }

    bytes = filesystem_read(file, g_texturefile, sizeof(g_texturefile));
    filesystem_close(file);

    if (bytes < 0) {
      printf("[MAIN] failed reading texture file: %s\n", file_strings_table[file_id]);
      continue;
    }

    switch (file_id) {
    case FIDX__RES_FONT1_SRGFNT: {
      g_font1 = load_font_to_vram(g_texturefile, bytes);
    } break;
    case FIDX__RES_FONT2_SRGFNT: {
      g_font2 = load_font_to_vram(g_texturefile, bytes);
    } break;
    case FIDX__RES_LOGO_TIM: {
      g_logo = load_tim_to_vram(g_texturefile);
    } break;
    default: {
      assert(0 && "[MAIN] Texture file with nothing to receive it.");
    } break;
    }
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
  audio_initialize(); // after cd_start: the SPU must be reset after the CD controller
  memory_card_initialize();
  input_pad_initialize();
  input_pad_start();

  {
    uintptr_t remaining_memory = system_get_remaining_allocatable_memory();
    _debugprintf("[MEMORY]: %d bytes, %d kb, %d mb left\n", remaining_memory, remaining_memory / 1024, remaining_memory / (1024*1024));
  }

  //
  // The memory card header needs a 16x16 4bpp icon; keep loading it off the
  // disc as before.
  //
  {
    File_Handle icon = filesystem_open("\\RES\\SAVICO.TIM");
    if (icon != 0) {
      int bytes = filesystem_read(icon, g_iconfile, CD_SECTOR_SIZE);
      filesystem_close(icon);
      if (bytes < 0) {
        printf("[MAIN] icon file not found on disc\n");
      }
    } else {
      printf("[MAIN] icon file not found on disc\n");
    }
  }

  load_textures();

  //
  // Songs. Parsed once into note events in main RAM; the instruments they
  // play are already in sound RAM from audio_initialize(). Nothing about
  // audio touches the disc after this.
  //
  g_song_theme = audio_song_load(MUSIC_TEST_PATH);

  scene_reset_box(&scene);
  scene.logo_position = (Vector2) {16, 16};
  scene.logo_velocity = (Vector2) {LOGO_SPEED, LOGO_SPEED};
  settings_load(&scene);
  scene_enter_gameplay(&scene);

  printf("Hello PSX\n");

  g_testcube.vertices = cube_verts;
  g_testcube.normals = cube_norms;
  g_testcube.colors = cube_colors;
  g_testcube.vertex_count = array_count(cube_verts);
  _debugprintf("test mesh has: %d vertices", g_testcube.vertex_count);

  //
  // loading mesh files
  //
  {
    File_Handle file = filesystem_open("\\RES\\M1.BIN");
    if (file != 0) {
      int bytes = filesystem_read(file, g_meshfile1, sizeof(g_meshfile1));
      filesystem_close(file);
      if (bytes >= 0) {

        //
        // setup mesh from cd file memory
        //
        {
          uint8_t* cursor = g_meshfile1;
          g_m1.vertex_count = *((uint16_t*)cursor);

          cursor += 2;
          g_m1.vertices = (SVECTOR*)cursor;

          cursor += sizeof(SVECTOR) * g_m1.vertex_count;
          g_m1.normals = (SVECTOR*)cursor;
        }
        printf("[MESH] vtxcount=%d\n", g_m1.vertex_count);
      } else {
        printf("[MAIN] model file not found on disk\n");
      }
    } else {
      printf("[MAIN] model file not found on disk\n");
    }
  }

  {
    File_Handle file = filesystem_open("\\RES\\M2.BIN");
    if (file != 0) {
      int bytes = filesystem_read(file, g_meshfile2, sizeof(g_meshfile2));
      filesystem_close(file);
      if (bytes >= 0) {

        //
        // setup mesh from cd file memory
        //
        {
          uint8_t* cursor = g_meshfile2;
          g_m2.vertex_count = *((uint16_t*)cursor);

          cursor += 2;
          g_m2.vertices = (SVECTOR*)cursor;

          cursor += sizeof(SVECTOR) * g_m2.vertex_count;
          g_m2.normals = (SVECTOR*)cursor;
        }
        printf("[MESH] vtxcount=%d\n", g_m2.vertex_count);
      } else {
        printf("[MAIN] model file not found on disk\n");
      }
    } else {
      printf("[MAIN] model file not found on disk\n");
    }
  }



  for (;;) {
    //
    // Frame order matters. pressed() means "down now, up in the snapshot",
    // so the snapshot has to be taken from the OLD packet, then a vblank
    // has to deliver a NEW packet, and only then may we poll:
    //
    //   scene_update       poll actions: "now" is the packet from the last
    //                      vblank, "last frame" is the snapshot before it
    //   audio_update       step the sequencer: key the notes due this frame,
    //                      step fades; never blocks
    //   R_render_end_frame sort the 3D first, so that if 2D overflows and
    //                      flushes, the world goes out ahead of the UI
    //   scene_draw         build the 2D part of the frame
    //   input_pad_frame    snapshot the packet we just finished reading and
    //                      bump the held-frame counters
    //   render_end_frame   DrawSync + VSync: the BIOS writes a fresh packet
    //                      during this vblank, ready for the next poll
    //
    // Calling input_pad_frame() right before scene_update() instead would
    // make snapshot and current identical, and no tap would ever register.
    //
    R_render_new_frame();
    vblank_now = VSync(-1);
    frame_divisor = vblank_now - last_vblank;
    scene_update(&scene);
    audio_update();
    // draw another box!
#if 0
    {
      static int e = 0;
      R_render_push_mesh((Render_Command_Mesh) {
	.position = { isin(e += (4096/120)) * 100, 0, 0 },
	/* .position = { ONE * 200, 0, 0 }, */
	.scale = { ONE, ONE, ONE },
	.rotation = { 1024, 0, d},
	.mesh = &g_m1
      });
      e+=4;
    }
#else
    {
      static int e = 0;
      int i;
      int j;

      for (i = 0; i < 3; ++i) {
	for (j = 0; j < 3 * intensity; ++j) {
	  R_render_push_mesh((Render_Command_Mesh) {
	    .position = { (j * 200 - 240) * ONE + isin(e) * 100, ONE * 50, ONE * (40 + 150 * i) },
	    /* .position = { ONE * 200, 0, 0 }, */
	    .scale = { ONE, ONE, ONE },
	    .rotation = { 1024, 0, 1024*3, 0 },
	    .mesh = &g_m1
	  });
	}
      }

      R_render_push_mesh((Render_Command_Mesh) {
	.position = { 0, ONE * 200, ONE * 200 },
	.scale = { ONE*3, ONE/4, ONE*3 },
	.rotation = { e, 0, 0, 0 },
	.mesh = &g_testcube
      });
      e += (4096/120) * frame_divisor;
    }
#endif

    R_render_end_frame();
    scene_draw(&scene);

    input_pad_frame();
    last_vblank = VSync(-1);
    render_end_frame();
  }

  return 0;
}
