#include "camera.h"

//
// The renderer turns rotation into a view matrix with RotMatrix, which builds
// Rx(pitch) * Ry(yaw). The rows of that matrix are the camera's own axes
// written in world coordinates, and the PS1 convention is Y pointing DOWN:
//
//   right   = row 0 = ( cos y,          0,       sin y         )
//   forward = row 2 = ( -cos p * sin y, sin p,   cos p * cos y )
//
// so a positive pitch tips forward towards +Y, which is looking down.
//
//            eye    = target - forward * CAMERA_DISTANCE
//              \
//               \   pitch
//                \
//   ------------ target ----------------------------------- floor
//
// Everything below is those two rows. isin/icos return 4.12 (4096 = 1.0), so
// a 4.12 direction times a plain integer distance lands directly in 20.12,
// the format positions are kept in. No division anywhere.
//

void camera_initialize(Camera* camera, VECTOR target)
{
  camera->target      = target;
  camera->rotation.vx = CAMERA_PITCH;
  camera->rotation.vy = CAMERA_YAW_START;
  camera->rotation.vz = 0;
  camera->yaw_goal    = CAMERA_YAW_START;

  camera_update(camera, target, 0);
}

void camera_turn(Camera* camera, int quarter_turns)
{
  camera->yaw_goal = (camera->yaw_goal + quarter_turns * 1024) & 4095;
}

void camera_update(Camera* camera, VECTOR follow, int frames)
{
  int cos_pitch;
  int sin_pitch;
  int cos_yaw;
  int sin_yaw;
  int i;

  //
  // Easing by a shift closes a fixed fraction of the gap each frame, which
  // starts fast and settles gently. It is run once per vblank that passed,
  // rather than scaled, because the curve is not linear.
  //
  // The yaw gap is wrapped into -2048..2047 first, so a turn across the
  // 4095 -> 0 seam goes the short way instead of spinning 270° backwards.
  // The shift would stop just short of the goal forever, so the last few
  // units snap.
  //
  for (i = 0; i < frames; ++i) {
    int yaw_gap = ((camera->yaw_goal - camera->rotation.vy + 2048) & 4095) - 2048;

    camera->target.vx += (follow.vx - camera->target.vx) >> CAMERA_FOLLOW_SHIFT;
    camera->target.vy += (follow.vy - camera->target.vy) >> CAMERA_FOLLOW_SHIFT;
    camera->target.vz += (follow.vz - camera->target.vz) >> CAMERA_FOLLOW_SHIFT;

    if (abs(yaw_gap) < (1 << CAMERA_TURN_SHIFT)) {
      camera->rotation.vy = camera->yaw_goal;
    } else {
      camera->rotation.vy = (camera->rotation.vy + (yaw_gap >> CAMERA_TURN_SHIFT)) & 4095;
    }
  }

  cos_pitch = icos(camera->rotation.vx);
  sin_pitch = isin(camera->rotation.vx);
  cos_yaw   = icos(camera->rotation.vy);
  sin_yaw   = isin(camera->rotation.vy);

  // eye = target - forward * distance
  camera->position.vx = camera->target.vx + ((cos_pitch * sin_yaw) >> 12) * CAMERA_DISTANCE;
  camera->position.vy = camera->target.vy - sin_pitch * CAMERA_DISTANCE;
  camera->position.vz = camera->target.vz - ((cos_pitch * cos_yaw) >> 12) * CAMERA_DISTANCE;
}

//
// forward and right with the pitch dropped, so walking "up the screen" never
// tries to walk into the floor.
//
VECTOR camera_ground_direction(const Camera* camera, int forward, int right)
{
  int    cos_yaw = icos(camera->rotation.vy);
  int    sin_yaw = isin(camera->rotation.vy);
  VECTOR result;

  result.vx = -sin_yaw * forward + cos_yaw * right;
  result.vy = 0;
  result.vz =  cos_yaw * forward + sin_yaw * right;

  return result;
}
