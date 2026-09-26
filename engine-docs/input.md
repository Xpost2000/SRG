# Input System

How controller input works in SRG, why it is built the way it is, and how to use it.

## The one-paragraph version

The pad gives us 16 buttons. Game code never looks at those buttons directly. Instead it asks
about **actions** ("is CONFIRM pressed?", "which way is the player moving?"). A **schema** is a
small table that says which button means which action. Menus use one schema, gameplay uses
another, and the player can choose between two gameplay layouts (Type A / Type B) that we
save on the memory card as a single number. That is the whole system.

```
  physical pad            input_pad.c              input_action.c            your game
 ┌───────────┐   BIOS    ┌──────────────┐  masks   ┌─────────────────┐        ┌───────────┐
 │ 16 buttons│ ───────►  │ held/pressed │ ───────► │ schema table    │ ─────► │ JUMP?     │
 │ (raw bits)│  vblank   │ /released    │          │ action -> mask  │  poll  │ CONFIRM?  │
 └───────────┘           └──────────────┘          │ callbacks       │   or   │ axis      │
                                                   │ repeat, axis    │  call- │           │
                                                   └─────────────────┘  back  └───────────┘
```

## What the hardware actually does

Everything below was checked against the SDK headers in `toolchain-win64/include/libpsn00b/`
and the nocash hardware docs.

- **The BIOS reads the pads for us, once per frame.** `InitPAD` gives it two buffers, `StartPAD`
  tells it to fill them every vertical blank (60 times a second on NTSC). We never talk to the
  controller port ourselves. PSn00bSDK ships a faster 250 Hz driver in its `io/pads` example,
  but we do not need it for a game that runs at frame rate.
  Source: [nocash PSX-SPX, BIOS joypad functions](https://problemkaputt.de/psxspx-bios-joypad-functions.htm)
- **Buttons are active-low.** A pressed button is a `0` bit, not a `1`. `input_pad.c` inverts
  this once so nothing above it has to remember.
  Source: [psx-spx, controllers](https://psx-spx.consoledev.net/controllersandmemorycards/)
- **The buffers start zeroed.** Until the first vblank fills them in, the packet reads as
  "connected, every button held". The pad type byte is also 0, which is not a real type, so the
  type check in `input_pad.c` is what protects us from a phantom all-buttons press at boot.
- **Read pads after `VSync(0)`.** The BIOS updates the buffers during the vblank interrupt, so a
  frame's input is only complete once `VSync` returns. The main loop snapshots "last frame"
  before `VSync` and dispatches actions after it.
- **`StartPAD` breaks `VSync` unless you fix it.** Interrupt handlers form a chain. `StartPAD`
  (and `StartCARD`) tell the pad handler to swallow the vblank interrupt, so PSn00bSDK's
  `VSync` never hears it, waits out a multi-second timeout, prints `psxgpu: VSync() timeout`
  and then repairs things itself. Calling `ChangeClearPAD(0)` right after `StartPAD` passes the
  interrupt along and avoids the stall. Sony's own samples do this.
  Source: `system-docs/md/libref.md`, entries `StartPAD` and `ChangeClearPAD`;
  `system-docs/md/psn00b.md`, entry `VSync`.
- **Memory card I/O stops the pad driver.** They share the same BIOS interrupt handler.
  Anything that touches the card must call `input_pad_start()` afterwards. `StopPAD` also stops
  card I/O and can knock a DualShock back into digital mode, so we never call it.
- **This game is digital-only.** A DualShock in analog mode reports a different type ID but the
  same 16 button bits in the same place, so both types are handled identically and the stick
  bytes are ignored. We decided not to support sticks at all.

## How other PS1 games did it

We looked at what shipped before designing anything. The pattern was consistent.

- **Named presets were the norm, not free remapping.** Resident Evil 2 offered *Type A / B / C*
  ([TCRF](https://tcrf.net/Resident_Evil_2_(PlayStation)/en)). Ace Combat 2 had *Novice / Expert*
  and Ace Combat 3 had *Analog / Digital* configs
  ([StrategyWiki](https://strategywiki.org/wiki/Ace_Combat_3:_Electrosphere/Controls)).
  Crash Bandicoot: Warped had no button options at all
  ([Crash Mania](https://www.crashmania.net/en/games/crash-bandicoot-warped/overview/)).
- **Per-action remapping was the exception.** Final Fantasy VII let you remap about ten abstract
  actions (OK, CANCEL, MENU, SWITCH, ...)
  ([Caves of Narshe](https://www.cavesofnarshe.com/ff7/buttons.php)). That "abstract action"
  idea is exactly what our action enum is.
- **Config lived inside the game save.** A separate settings file costs one of the card's 15
  blocks, so nobody did it. Our preset is a byte in the save struct for the same reason.
- **A cautionary tale.** FF7 had code paths that read the raw default button instead of the
  remapped action, so a custom layout only half worked. Our rule: game code never calls
  `input_pad_mask_button*`. It goes through actions.
- **Edge detection idiom.** Keeping this frame's and last frame's button state and comparing
  them is how everyone does "just pressed": raylib's `rcore.c` and Quake's `cl_input.c` do the
  same thing on PC.

## The three ideas

**Action** is a verb the game understands: `INPUT_ACTION_UI_CONFIRM`, `INPUT_ACTION_MOVE_LEFT`,
`INPUT_ACTION_PAUSE`. Defined in the game-specific block at the top of `source/input_action.h`.

**Schema** is a table with one 16-bit button mask per action. `0` means "this action does not
exist here". Masks can OR several buttons so that either one works.

```c
[INPUT_ACTION_UI_CONFIRM] = PAD_CROSS,
[INPUT_ACTION_UI_CANCEL]  = PAD_TRIANGLE | PAD_CIRCLE,   // either button
```

**Context** is which schema is live on a pad right now. Menus bind the UI schema; gameplay
binds one of the gameplay schemas. Switching is one call.

**Preset** is which gameplay schema the player picked. `Input_Gameplay_Preset` is the enum that
goes on the memory card. Only ever append values to it; renumbering changes what old saves mean.

Context and preset are kept separate on purpose. Context is decided by game state, preset by
the player.

## Decisions and why

| Decision | Why |
| --- | --- |
| Two layers, `input_pad` under `input_action` | The low-level module already existed and is correct. The action layer is a thin table lookup on top; nothing is duplicated. |
| Schemas are `const` tables, not runtime remapping | Matches what most PS1 games shipped. A preset is an array index, so saving it is one byte and there is no allocation or validation of arbitrary layouts. |
| Digital buttons only | Product decision. Removes dead zones, analog-mode detection and stick tuning entirely. |
| Both polling and callbacks | Poll for continuous things (movement, held-to-grow). Use callbacks for discrete things (pause, confirm) so `if (pressed)` checks do not pile up in the game loop. Callbacks are a loop over the same polling primitives, not a second system. |
| One context per pad, no stack | Two players can be on different presets. A push/pop stack was not needed yet; add it if pause-over-gameplay ever has to restore the previous context automatically. |
| The input layer never touches the memory card | The preset is game save data. Keeping card start/stop quirks out of the input code keeps it small and testable. |
| Menu auto-repeat lives in the input layer | Every menu needs it. It is a per-action held-frame counter, a few lines. |

## Using it

### 1. Define actions and presets

Edit the game-defined block in `source/input_action.h`:

```c
typedef enum Input_Action {
  INPUT_ACTION_UI_UP, INPUT_ACTION_UI_DOWN, INPUT_ACTION_UI_CONFIRM, INPUT_ACTION_UI_CANCEL,
  INPUT_ACTION_MOVE_LEFT, INPUT_ACTION_MOVE_RIGHT,
  INPUT_ACTION_JUMP, INPUT_ACTION_ATTACK, INPUT_ACTION_PAUSE,
  INPUT_ACTION_COUNT
} Input_Action;

typedef enum Input_Gameplay_Preset {
  INPUT_GAMEPLAY_PRESET_A = 0,
  INPUT_GAMEPLAY_PRESET_B,
  INPUT_GAMEPLAY_PRESET_COUNT
} Input_Gameplay_Preset;
```

### 2. Fill in the tables

In `source/input_action.c`. Anything not listed is unbound in that schema.

```c
static const Input_Schema g_schema_ui = { .buttons = {
  [INPUT_ACTION_UI_UP]      = PAD_UP,
  [INPUT_ACTION_UI_DOWN]    = PAD_DOWN,
  [INPUT_ACTION_UI_CONFIRM] = PAD_CROSS,
  [INPUT_ACTION_UI_CANCEL]  = PAD_TRIANGLE | PAD_CIRCLE,
}};

static const Input_Schema g_schema_gameplay[INPUT_GAMEPLAY_PRESET_COUNT] = {
  [INPUT_GAMEPLAY_PRESET_A] = { .buttons = {
    [INPUT_ACTION_MOVE_LEFT]  = PAD_LEFT,
    [INPUT_ACTION_MOVE_RIGHT] = PAD_RIGHT,
    [INPUT_ACTION_JUMP]       = PAD_CROSS,
    [INPUT_ACTION_ATTACK]     = PAD_SQUARE,
    [INPUT_ACTION_PAUSE]      = PAD_START,
  }},
  [INPUT_GAMEPLAY_PRESET_B] = { .buttons = {
    [INPUT_ACTION_MOVE_LEFT]  = PAD_LEFT,
    [INPUT_ACTION_MOVE_RIGHT] = PAD_RIGHT,
    [INPUT_ACTION_JUMP]       = PAD_L1,      // shoulder jump
    [INPUT_ACTION_ATTACK]     = PAD_CROSS,
    [INPUT_ACTION_PAUSE]      = PAD_START,
  }},
};
```

### 3. Start the pads and bind a context

```c
input_pad_initialize();
input_pad_start();

// after loading settings from the card:
input_action_bind(P1_PAD, input_schema_gameplay(settings.gameplay_preset[0]), on_player_action, &player);
```

### 4. Frame loop

```c
for (;;) {
  input_pad_frame();          // snapshot last frame's pad state

  update(&player);            // polling reads happen here
  draw();
  render_end_frame();         // DrawSync + VSync; BIOS refreshes pads during this vblank

  input_action_frame();       // compute edges, fire callbacks
}
```

### 5. Poll for continuous things

```c
void update(Player* p) {
  // -1, 0 or +1. Both directions held cancels to 0.
  p->velocity.x = input_action_axis(P1_PAD, INPUT_ACTION_MOVE_LEFT, INPUT_ACTION_MOVE_RIGHT) * RUN_SPEED;

  if (input_action_released(P1_PAD, INPUT_ACTION_JUMP) && p->velocity.y < 0) {
    p->velocity.y >>= 1;      // short hop: cut the jump when the button is let go early
  }
}
```

### 6. Callbacks for discrete things

```c
static void on_player_action(int pad, Input_Action action, Input_Event event, void* user) {
  Player* p = user;
  if (event != INPUT_EVENT_PRESSED) return;

  switch (action) {
    case INPUT_ACTION_JUMP:   if (p->on_ground) p->velocity.y = -JUMP_SPEED; break;
    case INPUT_ACTION_ATTACK: player_start_attack(p);                        break;
    case INPUT_ACTION_PAUSE:  input_action_bind(pad, input_schema_ui(), on_menu_action, &menu); break;
    default: break;
  }
}
```

Opening the pause menu is that one `input_action_bind` call. From the next frame Cross means
CONFIRM instead of JUMP, and JUMP cannot fire at all. Closing the menu binds the gameplay
schema back.

### 7. Menu navigation with auto-repeat

```c
// true on first press, then every 6 frames after being held 20 frames
if (input_action_repeat(P1_PAD, INPUT_ACTION_UI_DOWN, 20, 6)) {
  menu->cursor = (menu->cursor + 1) % MENU_ITEM_COUNT;
}
```

### 8. Save and load the preset

The preset is just a byte in your save struct. Remember to restart the pads after any card access.

```c
typedef struct Game_Settings {
  char    magic[4];             // "SRGS"
  uint8_t version;
  uint8_t gameplay_preset[2];   // Input_Gameplay_Preset, per pad
} Game_Settings;

memory_card_start();
memory_card_write(savename, icon_tim, &settings, sizeof(settings));
memory_card_stop();
input_pad_start();              // card I/O stopped the pad driver

// on load, validate before trusting it:
if (settings.gameplay_preset[0] >= INPUT_GAMEPLAY_PRESET_COUNT) settings.gameplay_preset[0] = INPUT_GAMEPLAY_PRESET_A;
input_action_bind(P1_PAD, input_schema_gameplay(settings.gameplay_preset[0]), on_player_action, &player);
```

### 9. Show bindings on screen

```c
char text[40];
input_schema_describe(input_action_current_schema(P1_PAD), INPUT_ACTION_JUMP, text, sizeof(text));
// text is now "CROSS" for preset A, "L1" for preset B, "-" if unbound
```

## The test scene

`source/main.c` is a controllable box that exercises everything above. Every one of the 16
buttons does something visible, the two presets mirror each other, Start opens a menu where you
can change presets and save or load them, and holding Select prints the live bindings straight
out of the schema table. Run it with `pcsx-run.bat`.

| Action | Type A | Type B |
| --- | --- | --- |
| Move | D-pad | D-pad |
| Colour next / prev (tap) | Cross / Circle | Square / Triangle |
| Grow / shrink (hold) | Square / Triangle | Cross / Circle |
| Boost / slow (hold) | R1 / L1 | L1 / R1 |
| Drop marker / clear markers (tap) | R2 / L2 | L2 / R2 |
| Toggle outline / reset position (tap) | R3 / L3 | L3 / R3 |
| Help overlay (hold) | Select | Select |
| Pause menu | Start | Start |

## Rules of thumb

1. Game code calls `input_action_*`, never `input_pad_mask_button*`.
2. Call `input_action_frame()` once per frame, after `VSync`.
3. After any memory card access, call `input_pad_start()`.
4. Never call `StopPAD()`.
5. Add new actions to the enum and the tables; do not add `if (button == PAD_X)` anywhere.
6. Only append to `Input_Gameplay_Preset`; never reorder it.

## Further reading

- `toolchain-win64/include/libpsn00b/psxpad.h`: the `PADTYPE` packet and `PadButton` bits.
- `toolchain-win64/share/psn00bsdk/examples/io/pads/main.c`: the high-speed SPI driver we chose
  not to use, with notes on DualShock quirks.
- [Lameguy64's PSn00bSDK tutorial, chapter 1.4: Controllers](http://lameguy64.net/tutorials/pstutorials/chapter1/4-controllers.html)
- [nocash PSX-SPX: controllers and memory cards](https://psx-spx.consoledev.net/controllersandmemorycards/)
- [nocash PSX-SPX: BIOS joypad functions](https://problemkaputt.de/psxspx-bios-joypad-functions.htm)
