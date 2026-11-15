# 3D Camera

`source/camera.*` is a fixed-angle follow camera. It always looks down at the same angle, eases after
the player, and turns only in quarter steps. `R_render_set_camera` in `source/render.*` turns its
position and rotation into the view matrix the GTE uses.

## Using it

```c
camera_initialize(&camera, player_position);           // snaps, no ease

// every frame
camera_update(&camera, player_position, frame_divisor);
R_render_set_camera(camera.position, camera.rotation);  // before R_render_end_frame
```

The camera reads no input and draws nothing. Positions are 20.12 fixed point (`ONE` = 1 world
unit), and angles are 4096 = 360°.

## Writing the player controls

The test scene has no player controls yet; the cube stands still and the camera holds on it. The
gameplay input still drives the 2D box test from `engine-docs/input.md`, which gives every button a
job. The camera already has the two calls the controls need, `camera_turn` and
`camera_ground_direction`. The work is in `scene_update_gameplay` in `source/main.c` and the action
tables:

1. **Free up buttons and add the actions.** The box test uses all 16 buttons, so retire its verbs
   (COLOR, GROW, MARKER, ...) first. Then add `INPUT_ACTION_RUN`, `INPUT_ACTION_CAMERA_LEFT` and
   `INPUT_ACTION_CAMERA_RIGHT` to `Input_Action` in `input_action.h`, bind them in both gameplay
   schemas in `input_action.c`, and give them labels in `g_action_labels` in `main.c` so the help
   overlay shows them. `MOVE_*` is already on the D-pad; it moves the box today.
2. **Turn the camera** on a tap:

   ```c
   if (input_action_pressed(P1_PAD, INPUT_ACTION_CAMERA_LEFT))  camera_turn(&scene->camera, -1);
   if (input_action_pressed(P1_PAD, INPUT_ACTION_CAMERA_RIGHT)) camera_turn(&scene->camera, +1);
   ```

   `camera_turn` only moves the goal. `camera_update` eases the view there over the next few frames.
3. **Move the player relative to the screen**, so that up on the D-pad walks up the screen in every
   view:

   ```c
   int    forward = input_action_axis(P1_PAD, INPUT_ACTION_MOVE_DOWN, INPUT_ACTION_MOVE_UP);
   int    right   = input_action_axis(P1_PAD, INPUT_ACTION_MOVE_LEFT, INPUT_ACTION_MOVE_RIGHT);
   int    speed   = input_action_held(P1_PAD, INPUT_ACTION_RUN) ? 10 : 4;  // world units per frame
   VECTOR step    = camera_ground_direction(&scene->camera, forward, right);

   scene->player_position.vx += step.vx * speed * frame_divisor;
   scene->player_position.vz += step.vz * speed * frame_divisor;
   ```

   The direction is 4.12 and the position is 20.12, so multiplying by whole world units gives a
   20.12 step with no shifting. Leave `vy` alone until there is gravity.
4. **Keep `camera_update` last** in the function, after the player has moved, so the camera eases
   toward where the player is this frame.

Things to watch for:

- `frame_divisor` currently always reads 1, because it only measures the time from just before
  `render_end_frame` to the start of the next frame. Movement will slow down whenever the game drops
  to 30 fps until that is fixed.
- Taps can be dropped at 30 fps, because `input_pad_frame()` snapshots the pad buffer once per frame
  rather than once per vblank. If camera turns sometimes do not fire, that is the cause.
- If you teleport the player (a respawn or a reset button), call `camera_initialize` instead of
  letting `camera_update` ease, or the camera visibly flies across the level.

## How the camera reaches the GTE

The PS1 has no hardware camera. The GTE holds one rotation matrix and one translation vector and
pushes every vertex through them. Our camera is a **view matrix** (world to camera), `g_view` in
`render.c`. `R_render_set_camera` rebuilds it once a frame, and `R_render_command_present_mesh` folds
it into each mesh's model matrix (mesh to world) before loading that mesh:

```
GTE matrix = VIEW x MODEL = CompMatrixLV(&g_view, &model, &out)
  rotation    = view.rot * model.rot
  translation = view.rot * model.trans + view.trans
```

A scene that never sets a camera gets the identity view: at the origin, looking down +Z.

The rotation never changes except while a quarter turn is easing, so building the view costs one
`RotMatrix` and a few `isin`/`icos` products a frame, with no normalising or cross products. The
SDK example `toolchain-win64/share/psn00bsdk/examples/graphics/fpscam/` builds its view the same
way, and it also has a `LookAt` if we ever want an orbit camera.

## The math

`RotMatrix(&(SVECTOR){pitch, yaw, 0})` builds Rx(pitch) * Ry(yaw). Yaw is applied before pitch, so
the camera turns on the spot and then tilts, which keeps the horizon level. The rows of that matrix
are the camera's axes in world coordinates. Y points **down** on the PS1:

```
right   = row 0 = ( cos y,          0,     sin y         )
forward = row 2 = ( -cos p * sin y, sin p, cos p * cos y )      p > 0 looks down

            eye  = target - forward * CAMERA_DISTANCE
              \
               \   pitch
                \
   ------------ target (eases toward the player) ------------- floor
```

- **View translation** is `R * -eye`, computed with `ApplyMatrixLV`.
- **Screen-relative movement:** `camera_ground_direction` is forward and right with the pitch
  dropped, so up on the D-pad walks up the screen in any of the four views. It uses the *current*
  yaw, so the controls follow the picture during a turn.
- **Follow:** `target += (player - target) >> CAMERA_FOLLOW_SHIFT`, once for each vblank that
  passed.
- **Turn:** the gap to the goal is wrapped into -2048..2047 so that a turn across 4095 -> 0 goes the
  short way. It then eases by a shift, and the last few units snap to the goal, because a shift
  alone would stop just short of it forever.
- **Lighting** stays in the world. The light matrix is taken from the model rotation before the
  view is composed in, so turning the camera does not move the lights.

## Tuning

All the knobs are `#define`s at the top of `camera.h`:

| Knob | Default | Effect |
|---|---|---|
| `CAMERA_PITCH` | 400 (~35°) | how steeply it looks down |
| `CAMERA_DISTANCE` | 700 | world units from the eye to the target |
| `CAMERA_YAW_START` | 512 (45°) | the diagonal "isometric" starting view |
| `CAMERA_FOLLOW_SHIFT` | 3 | larger means lazier following |
| `CAMERA_TURN_SHIFT` | 2 | larger means slower quarter turns |

The field of view is set in `render_initialize` by `gte_SetGeomScreen(RENDER_SCREEN_WIDTH / 2)`,
which gives 90° across. A larger value narrows the field of view and flattens the perspective toward
a true isometric look, but the camera then has to sit further back. Depth past roughly
2000 units falls off the end of the 3D ordering table, so a far camera also needs
`RENDER_OT_LENGTH` or the GTE's Z scale (`ZSF3`) adjusted.

## Known limits

- **No near-plane clipping.** A triangle that crosses the eye projects to garbage. That is fine at
  700 units away, but it will need fixing before the camera can get close to walls.
- **No rails or room bounds.** Both would be a constraint on `target` inside `camera_update`.
- **Diagonals are about 1.4x faster**, because the D-pad direction is not normalised.

## Sources

- `toolchain-win64/include/libpsn00b/psxgte.h`: `RotMatrix`, `CompMatrixLV`, `ApplyMatrixLV`
- `toolchain-win64/share/psn00bsdk/examples/graphics/fpscam/main.c`: the view matrix pattern
- `system-docs/md/libover47.md`: how the GTE transforms and projects
