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

void input_pad_initialize(void);
void input_pad_start(void);
void input_pad_frame(void);
int  input_pad_is_valid(int pad_index);

//
// NOTE(jerry):
// for standard 16 button controllers,
// no support for other stuff.
//
// Caller checks for gamepad validity.
//
int input_pad_mask_button(int pad_index, uint16_t buttonmask);
int input_pad_mask_button_pressed(int pad_index, uint16_t buttonmask);

#endif
