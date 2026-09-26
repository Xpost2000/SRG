# Code Guide

How code in this repo is written. `source/memory_card.c` and `source/cdfs.c` are the reference;
when in doubt, ask "what would `memory_card.c` do?" and match it.

This is plain C compiled for a 33 MHz MIPS with 2 MB of RAM and no floating-point unit. Most
rules below exist because of that.

## 1. Files and modules

One concern per module, as a `name.h` / `name.c` pair. The header opens with a banner that says
what the module is, then `NOTE`s about gotchas, then a `TODO` list if there is one.

```c
#ifndef MEMORY_CARD_H
#define MEMORY_CARD_H

//
// This is the low level memory card system.
//
// NOTE:
// - Make sure after saving or reading that you start the gamepad again,
//   the PS1 stops the gamepad when the card is running.
//
// TODO:
// - [ ] Need to get _card_load working to save to different ports
//

#include "common.h"
```

- Include guard is `NAME_H`. `#include "common.h"` comes first, then other project headers,
  then SDK headers in angle brackets.
- Modules do not know about each other unless they must. The input layer never touches the
  memory card; the card module never touches rendering. If two modules need to cooperate, the
  caller (usually the scene) does the wiring.
- No file in `run-tree/` is ever edited by hand.

## 2. Naming

| Thing | Style | Example |
| --- | --- | --- |
| Public function | `module_verb_object` | `memory_card_file_exists`, `cd_file_open` |
| Internal helper | `static`, optionally `_leading_underscore` | `_wait_memcard_event`, `serialize_card` |
| Module global | `static` with `g_` prefix | `g_padpacket`, `g_memcard_events` |
| Type | `Pascal_Snake` | `Memory_Arena`, `CD_File_Buffer`, `Rectangle32` |
| Enum member | `UPPER_SNAKE` prefixed with the type | `MEMCARD_EVENT_RESPONSE_TIMEOUT`, `INPUT_SCHEMA_UI` |
| Constant | `#define NAME (value)`, value in parens | `#define MEMCARD_BLOCK_SZ (8192)` |
| Local | `snake_case`, descriptive | `display_environment`, `frame_index`, not `de`, `fi` |
| SDK types | as the SDK spells them | `DISPENV`, `PADTYPE`, `TIM_IMAGE` |

Booleans are `int` holding 1 or 0. Functions that answer a yes/no question are named as the
question: `input_pad_is_valid`, `memory_card_file_exists`.

## 3. Types

Forward-declare the typedef, then define the struct separately. Same shape for enums.

```c
typedef struct CD_File CD_File;
typedef enum   Input_Schema Input_Schema;

struct CD_File {
  CdlFILE file_index;
  int     valid;
  size_t  cursor;
};

enum Input_Schema {
  INPUT_SCHEMA_UI,
  INPUT_SCHEMA_GAMEPLAY_A,
  INPUT_SCHEMA_COUNT
};
```

- Use sized integers (`uint8_t`, `uint16_t`, `int16_t`) for anything that touches hardware or
  goes on the memory card. Use `int` for ordinary arithmetic.
- **No `float` or `double`, ever.** The build passes `-msoft-float`, so they compile and then run
  hundreds of times slower than integers. Use fixed-point (`value << 12`, see the GTE docs) if
  you need fractions.
- Enums that are saved to the card are append-only. Say so in a comment above them.

## 4. Formatting

- 2-space indent. K&R braces: opening brace on the same line, `else` on the closing-brace line.
- `switch` cases carry their own block: `case X: { ... } break;`.
- One blank line between functions. `for (;;)` for the main loop.
- Column-align a group of related declarations, prototypes or calls when it makes them scan as
  a table. `cdfs.h`'s prototypes and `main.c`'s `SetDefDispEnv` / `SetDefDrawEnv` pairs are
  the model.

```c
SetDefDispEnv(&display_environment[0], 0,   0,   320, 240);
SetDefDrawEnv(&draw_environment[0],    0,   0,   320, 240);

SetDefDispEnv(&display_environment[1], 0,   240, 320, 240);
SetDefDrawEnv(&draw_environment[1],    0,   240, 320, 240);
```

## 5. Errors

Programmer mistakes are asserts with a tagged message. Things that can genuinely fail at runtime
return 0/1 and log.

```c
assert(pad_index >= 0 && pad_index < array_count(g_padpacket) && "[INPUT] Bad pad index.");

if (fd == -1) {
  _debugprintf("[MEMORY-CARD] Failed to write");
  return 0;
}
```

- The tag in square brackets is the module name in caps: `[INPUT]`, `[MEMORY-CARD]`, `[CDFS]`,
  `[RENDER]`.
- `_debugprintf` compiles out in release builds; `printf` does not. Use `_debugprintf` for
  tracing.
- When a function has several exit paths that all need the same cleanup, use `goto bye;` to a
  single label at the bottom (`memory_card_port_connected` does this).

## 6. Comments

The comment style is the most distinctive thing in this codebase and it is worth keeping.

- Banner blocks use `//` on their own line above and below:

  ```c
  //
  // Buttons are inverted state (floating signal on hardware?)
  //
  ```

- Explain **why**, and explain the **hardware**. A comment that restates the line is noise.
- `NOTE(name):` marks a personal observation or a caveat the next reader should know.
- ASCII diagrams are welcome. The VRAM map in `render.c` is the model.
- When you learn something about the hardware the hard way, write it down where it bit you.

## 7. Memory

- There is no `malloc`. Use static arrays sized by a `#define`, or a `Memory_Arena` for bulk
  data with a known lifetime.
- A function that returns a pointer to a static buffer gets a `NOTE` saying so, and the caller
  copies if it needs to keep the result (`get_game_save_name`, `input_action_button_name`).
- Big buffers are `static` at file scope, not on the stack. The PS1 stack is small.

## 8. Keep it simple

- **Data tables over branching code.** A button layout is a `const` array, not a chain of
  `if`s. A lookup that can be a table should be a table.
- **Polling over callbacks.** Check state where you need it, in sequential code you can read top
  to bottom. Function pointers are for the hardware SDK, not for us.
- **No abstraction with one implementation.** No interface, wrapper, or config for something
  that has exactly one form today.
- **Delete no-op functions.** If a function exists only to make a call sequence look symmetric,
  remove it.
- **Fewer files.** A new module earns its file by having state and a clear boundary, not by
  being a category.
- Mark deliberate shortcuts with a `ponytail:` comment naming the ceiling and the upgrade path.

## 9. Hardware rules

One line each. The reasons are in `CLAUDE.md` and `engine-docs/input.md`.

- Read pads only after `VSync(0)`; the BIOS fills the buffers during vblank.
- Always `ChangeClearPAD(0)` after `StartPAD()`, or `VSync` stalls for seconds.
- After any memory card access, call `input_pad_start()`.
- Never call `StopPAD()`.
- Game code uses `input_action_*`, never `input_pad_mask_button*` or `PAD_*` masks.
- VRAM holds framebuffers and textures together; every texture steals from the same 1 MB.
- Writes to the card must be multiples of 128 bytes; the card module handles this.

## 10. Docs and PRs

- Every engine system gets an `engine-docs/<system>.md`: what it does, why it is shaped that
  way, examples, and links to the sources you learned from.
- Update `CLAUDE.md`'s layout table when you add a source file.
- PRs follow `.github/pull_request_template.md` at about three sentences per section.
