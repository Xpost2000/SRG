#ifndef CAMERA_H
#define CAMERA_H

#include "common.h"

#include <psxgte.h>

//
// Fixed-angle follow camera: it always looks down at the same angle, eases
// after whatever it follows, and only turns in quarter steps. See
// engine-docs/camera.md, which also covers wiring it to player controls.
//
// It reads no input and draws nothing. Game code moves the player, then
// hands the result to the renderer:
//
//   camera_update(&camera, player_position, frame_divisor);
//   R_render_set_camera(camera.position, camera.rotation);
//
// Positions are 20.12 fixed point (ONE = 1 world unit), angles are 4096 = 360°.
//

//
// Tuning knobs. Change these in the emulator until it feels right.
//
#define CAMERA_PITCH        (400) // ~35° looking down
#define CAMERA_DISTANCE     (700) // world units from the eye to the target
#define CAMERA_YAW_START    (512) // 45°, the diagonal "isometric" view
#define CAMERA_FOLLOW_SHIFT (3)   // each frame closes 1/8 of the gap to the player
#define CAMERA_TURN_SHIFT   (2)   // each frame closes 1/4 of a quarter turn

typedef struct Camera Camera;

struct Camera {
  VECTOR  target;   // the point looked at, eases toward what it follows
  VECTOR  position; // where the eye is, derived from the rest every update
  SVECTOR rotation; // vx = pitch (fixed), vy = yaw (eases toward yaw_goal)
  int     yaw_goal; // CAMERA_YAW_START + n * 1024, kept in 0..4095
};

//
// Snaps straight to target with no easing. Use when entering a scene or
// teleporting, otherwise the camera visibly flies across the level.
//
void camera_initialize(Camera* camera, VECTOR target);

//
// +1 or -1 quarter turns. Turns queue up: tapping twice turns 180°.
//
void camera_turn(Camera* camera, int quarter_turns);

//
// frames is how many vblanks passed, so the easing is the same speed at 30
// and 60 fps.
//
void camera_update(Camera* camera, VECTOR follow, int frames);

//
// A ground-plane direction (4.12, y = 0) for "forward" and "right" as seen on
// screen, so the D-pad keeps meaning up-the-screen whichever way the camera
// faces. forward and right are -1, 0 or +1, as from input_action_axis.
//
VECTOR camera_ground_direction(const Camera* camera, int forward, int right);

#endif
