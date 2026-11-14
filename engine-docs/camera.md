# 3D Camera

`source/camera.*` is a fixed-angle follow camera, the kind used in Vagrant Story and Final Fantasy
Tactics. It always looks down at the same angle, eases after the player, and turns only in quarter
steps. `R_render_set_camera` in `source/render.*` turns its position and rotation into the view
matrix the GTE uses.

## Using it

```c
camera_initialize(&camera, player_position);           // snaps, no ease

// every frame, after polling input
if (input_action_pressed(P1_PAD, INPUT_ACTION_CAMERA_LEFT)) camera_turn(&camera, -1);
direction = camera_ground_direction(&camera, forward, right);   // D-pad -> world, screen-relative
camera_update(&camera, player_position, frame_divisor);

R_render_set_camera(camera.position, camera.rotation);  // before R_render_end_frame
```

The camera reads no input and draws nothing. Positions are 20.12 fixed point (`ONE` = 1 world
unit), and angles are 4096 = 360°.

## How PS1 games did cameras

The PS1 has no hardware camera. The GTE holds one rotation matrix and one translation vector and
pushes every vertex through them. A camera is a **view matrix** (world to camera) that gets folded
into each mesh's model matrix (mesh to world) before that mesh is loaded:

```
GTE matrix = VIEW x MODEL = CompMatrixLV(&view, &model, &out)
  rotation    = view.rot * model.rot
  translation = view.rot * model.trans + view.trans
```

Commercial games differ only in how they choose the view:

| Style | Games | How the view is built |
|---|---|---|
| Free first person | Doom, King's Field, Medal of Honor | position + yaw/pitch through `RotMatrix`, translation = R * -position |
| Look-at follow | Spyro, MediEvil, Tomb Raider | eye and target into a look-at matrix (Sony's libgs `GsSetRefView2`, which PSn00bSDK does not have) |
| **Fixed-angle follow (ours)** | Vagrant Story, FFT, Tactics Ogre, Diablo | constant rotation, eye = target - forward * distance, eased after the player, turned in steps |
| Fixed / rail | Resident Evil, Silent Hill, Crash | an authored camera per zone, often over pre-rendered backgrounds |

The fixed-angle follow is the cheapest of these. Its rotation is constant except while a turn is
easing, so there is no normalising and no cross product as there would be in a look-at matrix.
It is one `RotMatrix` and a few `isin`/`icos` products. The SDK's own example,
`toolchain-win64/share/psn00bsdk/examples/graphics/fpscam/`, builds its view the same way, and it
also has a `LookAt` if we ever want an orbit camera.

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
a true isometric look (as FFT does), but the camera then has to sit further back. Depth past roughly
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
