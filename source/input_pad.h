#ifndef INPUT_PAD_H
#define INPUT_PAD_H

#include "common.h"

#define P1_PAD (0)
#define P2_PAD (1)

/*
 * NOTE(jerry):
 *
 * Based off basic BIOS controller behavior,
 * I know there's a higher frequency driver implementation
 * but I don't think it's needed, especially for a game that's
 * technically not real-time, and I think the VSync timing is sufficient
 * enough.
 */

//
// This is the LOW LEVEL pad module. It knows about raw button masks
// (PAD_CROSS, PAD_UP, ...) and nothing else.
//
// Game code should not call the mask functions directly; it should go
// through input_action.h, which maps buttons to named actions. If you
// bypass it, a player's chosen button layout silently stops applying to
// your code path (Final Fantasy VII shipped with exactly that bug).
//
// Timing: the BIOS driver refreshes the pad buffers inside the vblank
// interrupt, so a frame's pad state is only complete AFTER VSync(0).
// Read pads after VSync, and call input_pad_frame() once per frame BEFORE
// VSync so the "last frame" snapshot is taken first.
//

void input_pad_initialize(void);
void input_pad_start(void);
void input_pad_frame(void);
int  input_pad_is_valid(int pad_index);

//
// Reports the PadTypeID (psxpad.h) of whatever is plugged in, or
// PAD_ID_NONE when nothing is responding. Mostly for debug displays.
//
int  input_pad_type(int pad_index);

//
// NOTE(jerry):
// for standard 16 button controllers,
// no support for other stuff.
//
// Caller checks for gamepad validity.
//
// A mask may OR several buttons together; the query is true if ANY of them
// satisfies it.
//
int input_pad_mask_button(int pad_index, uint16_t buttonmask);          // held this frame
int input_pad_mask_button_pressed(int pad_index, uint16_t buttonmask);  // down now, up last frame
int input_pad_mask_button_released(int pad_index, uint16_t buttonmask); // up now, down last frame

//
// How many PREVIOUS frames in a row the button has been down, not counting
// the current one. So on the frame a button is first pressed this reads 0,
// the next frame 1, and so on. For an OR'd mask it is the largest count of
// any button in the mask. Used for menu auto-repeat.
//
int input_pad_mask_button_held_frames(int pad_index, uint16_t buttonmask);

#endif
