# Input System

How controller input works in SRG, why it is built the way it is, and how to use it.

## The one-paragraph version

The pad gives us 16 buttons. Game code never looks at those buttons directly. Instead it polls
**actions** ("is CONFIRM pressed?", "which way is the player moving?"). A **schema** is a small
table that says which button means which action. Menus use one schema, gameplay uses another,
and the player can choose between two gameplay layouts (Type A / Type B) that we save on the
memory card as a single number. Everything is polled; there are no callbacks. That is the whole
system.

```
  physical pad            input_pad.c              input_action.c            your game
 ┌───────────┐   BIOS    ┌──────────────┐  masks   ┌─────────────────┐        ┌───────────┐
 │ 16 buttons│ ───────►  │ held/pressed │ ───────► │ schema table    │ ─────► │ JUMP?     │
 │ (raw bits)│  vblank   │ /released    │          │ action -> mask  │  poll  │ CONFIRM?  │
 └───────────┘           │ held frames  │          │ repeat, axis    │        │ axis      │
                         └──────────────┘          └─────────────────┘        └───────────┘
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
  before `VSync` and polls after it.
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
  blocks, so nobody did it. Our layout choice is a byte in the save struct for the same reason.
- **A cautionary tale.** FF7 had code paths that read the raw default button instead of the
  remapped action, so a custom layout only half worked. Our rule: game code never calls
  `input_pad_mask_button*`. It goes through actions.
- **Edge detection idiom.** Keeping this frame's and last frame's button state and comparing
  them is how everyone does "just pressed": raylib's `rcore.c` and Quake's `cl_input.c` do the
  same thing on PC.

## The two ideas

**Action** is a verb the game understands: `INPUT_ACTION_UI_CONFIRM`, `INPUT_ACTION_MOVE_LEFT`,
`INPUT_ACTION_PAUSE`. Defined in the game-specific block at the top of `source/input_action.h`.

**Schema** is a table with one 16-bit button mask per action, named by `enum Input_Schema`:
`INPUT_SCHEMA_UI`, `INPUT_SCHEMA_GAMEPLAY_A`, `INPUT_SCHEMA_GAMEPLAY_B`. `0` means "this action
does not exist here". Masks can OR several buttons so that either one works.

```c
[INPUT_ACTION_UI_CONFIRM] = PAD_CROSS,
[INPUT_ACTION_UI_CANCEL]  = PAD_TRIANGLE | PAD_CIRCLE,   // either button
```

Each pad has exactly one schema set at a time. Opening a menu sets the UI schema; closing it
sets the player's gameplay schema back. The gameplay schema value is what we save to the card,
so the enum is append-only.

## Decisions and why

| Decision | Why |
| --- | --- |
| Two layers, `input_pad` under `input_action` | The low-level module already existed and is correct. The action layer is a thin table lookup on top; nothing is duplicated. |
| Schemas are `const` tables named by one enum | Matches what most PS1 games shipped. The live schema is one integer per pad, saving it is one byte, and there are no pointers or accessor functions to follow. |
| Digital buttons only | Product decision. Removes dead zones, analog-mode detection and stick tuning entirely. |
| Polling only, no callbacks | A first version had callbacks. They split one frame's logic across three functions, let game code rebind the input layer from inside the input layer's own dispatch loop, and needed a re-entrancy guard to survive it. Polling is sequential code you read top to bottom, and a schema switch simply applies to the next poll. |
| Held-frame counters live in `input_pad`, per button | They track physical buttons, so they do not care which schema is live and never need resetting on a schema switch. The action layer stays stateless apart from "which schema". |
| The input layer never touches the memory card | The schema choice is game save data. Keeping card start/stop quirks out of the input code keeps it small. |
| Menu auto-repeat is in the input layer, with a fixed cadence | Every menu needs it and every menu should feel the same, so the delay and rate are two constants in `input_action.h`, not parameters a call site could get wrong. |

## Using it

### 1. Define actions and schemas

Edit the game-defined block in `source/input_action.h`:

```c
enum Input_Action {
  INPUT_ACTION_UI_UP, INPUT_ACTION_UI_DOWN, INPUT_ACTION_UI_CONFIRM, INPUT_ACTION_UI_CANCEL,
  INPUT_ACTION_MOVE_LEFT, INPUT_ACTION_MOVE_RIGHT,
  INPUT_ACTION_JUMP, INPUT_ACTION_ATTACK, INPUT_ACTION_PAUSE,
  INPUT_ACTION_COUNT
};

enum Input_Schema {
  INPUT_SCHEMA_UI,
  INPUT_SCHEMA_GAMEPLAY_A,   // gameplay layouts stay contiguous so menus can cycle them
  INPUT_SCHEMA_GAMEPLAY_B,
  INPUT_SCHEMA_COUNT
};
```

### 2. Fill in the table

In `source/input_action.c`. Anything not listed is unbound in that schema.

```c
static const uint16_t g_schemas[INPUT_SCHEMA_COUNT][INPUT_ACTION_COUNT] = {
  [INPUT_SCHEMA_UI] = {
    [INPUT_ACTION_UI_UP]      = PAD_UP,
    [INPUT_ACTION_UI_DOWN]    = PAD_DOWN,
    [INPUT_ACTION_UI_CONFIRM] = PAD_CROSS,
    [INPUT_ACTION_UI_CANCEL]  = PAD_TRIANGLE | PAD_CIRCLE,
  },
  [INPUT_SCHEMA_GAMEPLAY_A] = {
    [INPUT_ACTION_MOVE_LEFT]  = PAD_LEFT,
    [INPUT_ACTION_MOVE_RIGHT] = PAD_RIGHT,
    [INPUT_ACTION_JUMP]       = PAD_CROSS,
    [INPUT_ACTION_ATTACK]     = PAD_SQUARE,
    [INPUT_ACTION_PAUSE]      = PAD_START,
  },
  [INPUT_SCHEMA_GAMEPLAY_B] = {
    [INPUT_ACTION_MOVE_LEFT]  = PAD_LEFT,
    [INPUT_ACTION_MOVE_RIGHT] = PAD_RIGHT,
    [INPUT_ACTION_JUMP]       = PAD_L1,      // shoulder jump
    [INPUT_ACTION_ATTACK]     = PAD_CROSS,
    [INPUT_ACTION_PAUSE]      = PAD_START,
  },
};
```

### 3. Start the pads and pick a schema

```c
input_pad_initialize();
input_pad_start();

// after loading settings from the card:
input_action_set_schema(P1_PAD, settings.gameplay_schema[0]);
```

### 4. Frame loop

```c
for (;;) {
  update(&player);            // all polling happens here
  draw();
  input_pad_frame();          // snapshot the packet we just read, bump held counters
  render_end_frame();         // DrawSync + VSync; BIOS writes a fresh packet during this vblank
}
```

The order is load-bearing. `pressed` means "down now, up in the snapshot", so the snapshot must
be taken *before* the vblank delivers a new packet and the poll must happen *after*. Snapshotting
right before polling makes them identical and no tap ever registers.

### 5. Poll: held things and tapped things

```c
void update(Player* p) {
  // -1, 0 or +1. Both directions held cancels to 0.
  p->velocity.x = input_action_axis(P1_PAD, INPUT_ACTION_MOVE_LEFT, INPUT_ACTION_MOVE_RIGHT) * RUN_SPEED;

  // pressed() is true for exactly one frame per press
  if (input_action_pressed(P1_PAD, INPUT_ACTION_JUMP) && p->on_ground) {
    p->velocity.y = -JUMP_SPEED;
  }
  if (input_action_released(P1_PAD, INPUT_ACTION_JUMP) && p->velocity.y < 0) {
    p->velocity.y >>= 1;      // short hop: cut the jump when the button is let go early
  }
  if (input_action_pressed(P1_PAD, INPUT_ACTION_PAUSE)) {
    input_action_set_schema(P1_PAD, INPUT_SCHEMA_UI);
    game->state = GAME_STATE_PAUSED;
  }
}
```

Opening the pause menu is that one `input_action_set_schema` call. From the next poll Cross
means CONFIRM instead of JUMP, and JUMP cannot fire at all. Closing the menu sets the gameplay
schema back.

### 6. Menu navigation with auto-repeat

```c
// true on first press, then every INPUT_REPEAT_RATE_FRAMES after being held
// INPUT_REPEAT_DELAY_FRAMES. One cadence for every menu; tune the constants
// in input_action.h, never per call site.
if (input_action_repeat(P1_PAD, INPUT_ACTION_UI_DOWN)) {
  menu->cursor = (menu->cursor + 1) % MENU_ITEM_COUNT;
}
if (input_action_pressed(P1_PAD, INPUT_ACTION_UI_CANCEL)) {
  input_action_set_schema(P1_PAD, settings.gameplay_schema[0]);
  game->state = GAME_STATE_PLAYING;
}
```

### 7. Save and load the layout

The schema value is just a byte in your save struct. Remember to restart the pads after any card
access.

```c
struct Game_Settings {
  char    magic[4];             // "SRGS"
  uint8_t version;
  uint8_t gameplay_schema[2];   // Input_Schema, per pad
};

memory_card_start();
memory_card_write(savename, icon_tim, &settings, sizeof(settings));
memory_card_stop();
input_pad_start();              // card I/O stopped the pad driver

// on load, validate before trusting it:
if (settings.gameplay_schema[0] < INPUT_SCHEMA_GAMEPLAY_FIRST ||
    settings.gameplay_schema[0] >= INPUT_SCHEMA_COUNT) {
  settings.gameplay_schema[0] = INPUT_SCHEMA_GAMEPLAY_A;
}
input_action_set_schema(P1_PAD, settings.gameplay_schema[0]);
```

### 8. Show bindings on screen

```c
// "CROSS" for layout A, "L1" for layout B, "-" if unbound. Static buffer: copy to keep.
char* name = input_action_button_name(input_action_get_schema(P1_PAD), INPUT_ACTION_JUMP);
```

## The test scene

`source/main.c` is a controllable box that exercises everything above. Every one of the 16
buttons does something visible, the two layouts mirror each other, Start opens a menu where you
can change layouts and save or load them, and holding Select prints the live bindings straight
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
2. Poll first, then `input_pad_frame()`, then `VSync`. Never snapshot right before polling.
3. After any memory card access, call `input_pad_start()`.
4. Never call `StopPAD()`.
5. Add new actions to the enum and the table; do not add `if (button == PAD_X)` anywhere.
6. Only append to `Input_Schema`; never reorder it.

## Further reading

- `engine-docs/code-guide.md`: the conventions this module follows.
- `toolchain-win64/include/libpsn00b/psxpad.h`: the `PADTYPE` packet and `PadButton` bits.
- `toolchain-win64/share/psn00bsdk/examples/io/pads/main.c`: the high-speed SPI driver we chose
  not to use, with notes on DualShock quirks.
- [Lameguy64's PSn00bSDK tutorial, chapter 1.4: Controllers](http://lameguy64.net/tutorials/pstutorials/chapter1/4-controllers.html)
- [nocash PSX-SPX: controllers and memory cards](https://psx-spx.consoledev.net/controllersandmemorycards/)
- [nocash PSX-SPX: BIOS joypad functions](https://problemkaputt.de/psxspx-bios-joypad-functions.htm)
