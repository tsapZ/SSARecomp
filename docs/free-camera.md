# Free camera

An optional camera for the levels and the hub, off by default. Turn it on in the settings menu
(F4 > Game > Free camera, which also has its field of view, distance and up / down inversion) or
with `free_camera = true` in `ssarecomp.toml` (section `[Game]`). The code is
`src/hooks/free_camera_hook.cpp`; how it hooks the game is described at the top of that file.

## Cameras

Left trigger + right stick click cycles through three cameras:

1. **The game's camera**, as the game has it.
2. **Third person**: behind the player at a kept distance, turning with the way the player faces
   (also on the spot), quicker for bigger turns; running back towards the camera only swings it
   after half a second, and it holds still in the air, so landings can be steered.
3. **Turned**: hold the left trigger and move the right stick (or hold the right mouse button and
   move the mouse) to point it. It keeps its direction in the world while the player walks, and
   swings back behind the player after a few seconds without turning it.

Moving the right stick with the left trigger held from the game's or the third-person camera
switches to the turned camera. Going back to the game's camera is immediate; the others ease in.

## What the third-person and turned cameras do

- Stay in front of walls, props and the ground, using the game's own collision ray (invisible walls
  that keep the player on the island are ignored), and rise to look over the player when a wall
  pushes them in close.
- Stay at least `free_camera_min_height` above the player's feet (water has no collision).
- Trail the player a little and don't bob on small bumps; follow the player up and zoom out when a
  bounce pad launches the player; tilt down while falling; follow the slope of the ground they look
  along (lower going uphill, higher going downhill).
- Third person only: widen the view at speed (sprinting, flying).
- Turn the sticks with the camera, so up goes into the screen; aimed right stick actions (bombs) go
  by where the player faces.
- Give the camera back to the game in talks, cutscenes, mini-games that hold the player, and on
  level changes (third person comes back by itself afterwards), and lean over to the game's own
  shots, such as an enemy's introduction.

## Settings

All in `[Game]`, all starting with `free_camera_`: `speed`, `invert_x`, `invert_y`, `hold_lt`,
`collision`, `collision_mask` (48), `min_height` (1.5), `chase_distance` (10), `chase_elevation`
(20), `chase_look_above` (3), `chase_turn_rate` (15), `chase_max_turn_speed` (360),
`chase_hold_in_air`, `chase_follow_facing`, `fov` (100; 0 = the game's), `jump_smoothing`,
`smooth_follow`, `auto_recenter`, `recenter_delay`, `speed_fov` (0.5), `slope_tilt` (0.5),
`bounce_zoom`, `mouse`, `mouse_sensitivity`, `aim_by_facing`, `relative_movement`.

## Limits

- The camera can't detect water or decorative objects without collision.
- Spyro's Adventure only; the hooks are this game's.
- Where the player object keeps its facing isn't known; the mod learns it from a few seconds of
  walking straight (the log says where it found it) and follows the walk until then.
