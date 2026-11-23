// Free camera (free_camera, off by default): two more cameras besides the game's own, cycled with
// the left trigger + a click of the right stick: a third-person camera behind the player, and a
// turned camera the right stick (left trigger held) or the mouse (right button held) points, kept
// in the world while the player walks. Both follow the player at a kept distance, stay in front of
// walls and above the ground, zoom out for bounce pads (and, third person, at speed), and hand the
// camera back to the game in talks, cutscenes and on level changes. The sticks are turned so that
// "up" goes into the screen, and the right stick aims by the player's facing.
//
// Where the camera goes in (found 2026-11-20 with the input script's write watches in the hub):
// - sub_828578E8, the game thread's per-frame build of the main 3D scene, gets the game camera's
//   transform in r4 (12 floats). It reads them at its start (lfs f31..f20, 0..44(r4)) and once
//   more through sub_82907138 (the call at 0x828579C8, r4 untouched until 0x828579CC). From them
//   it makes the view, writes it into the main scene's render state (+0x170) and the data it
//   culls the scene's objects with. The wrapper around it (REX_HOOK_RAW below) writes the turned
//   transform into that memory first. It stays there for the rest of the frame, so what is drawn
//   after the scene (the button prompts over characters face the camera with it) sees it too;
//   FreeCameraFrameStart, at the start of the game's next per-frame update (sub_8280A008,
//   where_hook.cpp), puts the game's own values back before its camera logic runs.
// - sub_826340A0 derives a render state's matrices (world, view x projection, culling planes)
//   from its view (+0x170). FreeCameraHook at its start, for the main scene (name pointer
//   0x820A9AB0 = "Transformers2 - Main scene" at +0x310), learns once how the 12 floats are laid
//   out (which ones are the position, which the axes) by comparing them with the view made from
//   them. Until then, or if that fails, it turns the view itself as the first version did
//   (drawing turns, but the game's culling doesn't).
//   Other passes take their camera from elsewhere: the interaction button prompts ("3D Button",
//   name pointer 0x820A9B18) got the game's unturned camera (2026-11-21, live in the hub with the
//   camera turned). So a view of any state whose camera is the frame's unturned one (same eye and
//   forward as the scene build's input) gets the same turn.
// - The controller: sub_82854xxx reads pad states through sub_822DE498 (XamInputGetState) into an
//   X_INPUT_STATE at r1+96 (thumb_lx at +104, thumb_ly at +106). FreeCameraPadHook, right after
//   that call (0x82854AEC, r3 = the result), turns the sticks by the camera's yaw while the main
//   scene's camera is being turned, so "up" goes into the screen, and aims the right stick by the
//   player's facing.
//
// Walls: the turned camera is kept in front of level geometry with the game's own collision ray,
// sub_82847AC8(r3 = start, r4 = end, r5 = mask, r6 = object to ignore, r7 = 0, r8 = result,
// f1 = radius; returns r3 != 0 on a hit; result +0 the hit point, +12 the normal, +28 a distance
// value that isn't the fraction for every shape, +32 what was hit), called with radius 0 as the
// game's characters call it (sub_826EC180 and five others) from the wrapper, on the game thread,
// with a stack frame of its own below the scene build's. The characters use mask 16: the level's
// ground and walls, not props. Tried bit by bit (2026-11-21): level walls are seen by masks 1, 16,
// 32 and 256 (shape vtable 820A9DC4), rocks and props by 1, 32 and 256 (vtable 820A9E14), the
// invisible walls that keep the player on the island by 1 and 256 only (vtable 820A9D9C), and
// 4, 64, 128, 512, 1024 and 16384 the player's own body. So the camera uses 16 + 32 = 48.
//
// The right stick turns the camera while the left trigger is held (free_camera_hold_lt; the game
// doesn't use that trigger), and the game doesn't see the right stick then. Left trigger + a click
// of the right stick puts the camera back where the game wants it.

#include <algorithm>
#include <atomic>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/memory/utils.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

#include "hooks/free_camera_hook.h"

REXCVAR_DEFINE_BOOL(free_camera, false, "Game",
                    "the right stick turns the camera around your Skylander (click it to put "
                    "the camera back)");
REXCVAR_DEFINE_DOUBLE(free_camera_speed, 1.0, "Game",
                      "how fast the right stick turns the camera, 1 = normal");
REXCVAR_DEFINE_BOOL(free_camera_invert_x, false, "Game", "swap left and right for the camera");
REXCVAR_DEFINE_BOOL(free_camera_invert_y, false, "Game", "swap up and down for the camera");
REXCVAR_DEFINE_BOOL(free_camera_hold_lt, true, "Game",
                    "the right stick turns the camera only while the left trigger is held "
                    "(the game keeps the right stick otherwise)");
REXCVAR_DEFINE_BOOL(free_camera_collision, true, "Game",
                    "the turned camera stays in front of walls instead of looking through them");
REXCVAR_DEFINE_INT32(free_camera_collision_mask, 48, "Game",
                     "collision mask for the camera's wall test (16 = the level's ground and walls, "
                     "32 = props such as rocks and blocks; 48 = both. 1 also has invisible walls)");
REXCVAR_DEFINE_DOUBLE(free_camera_min_height, 1.5, "Game",
                      "the turned camera's least height above your Skylander's feet (keeps it "
                      "above water, which it can't detect, and out from under ledges)");
REXCVAR_DEFINE_DOUBLE(free_camera_chase_distance, 10.0, "Game",
                      "the third-person camera's distance from your Skylander");
REXCVAR_DEFINE_DOUBLE(free_camera_chase_elevation, 20.0, "Game",
                      "how far the third-person camera looks down at your Skylander, in degrees");
REXCVAR_DEFINE_DOUBLE(free_camera_chase_look_above, 3.0, "Game",
                      "how far above your Skylander the third-person camera aims (higher puts "
                      "your Skylander lower on the screen)");
REXCVAR_DEFINE_DOUBLE(free_camera_chase_turn_rate, 15.0, "Game",
                      "how quickly the third-person camera swings behind your Skylander (share "
                      "of the remaining angle per second)");
REXCVAR_DEFINE_DOUBLE(free_camera_chase_max_turn_speed, 360.0, "Game",
                      "the third-person camera's fastest swing, in degrees per second");
REXCVAR_DEFINE_BOOL(free_camera_chase_hold_in_air, true, "Game",
                    "the third-person camera doesn't swing while your Skylander is in the air");
REXCVAR_DEFINE_DOUBLE(free_camera_fov, 100.0, "Game",
                      "field of view of the third-person and turned cameras, horizontal degrees "
                      "(0 = the game's own, about 85 on a 16:9 screen)");
REXCVAR_DEFINE_BOOL(free_camera_jump_smoothing, true, "Game",
                    "the third-person and turned cameras don't bob up and down with jumps");
REXCVAR_DEFINE_BOOL(free_camera_smooth_follow, true, "Game",
                    "the third-person and turned cameras trail your Skylander a little");
REXCVAR_DEFINE_BOOL(free_camera_auto_recenter, true, "Game",
                    "the turned camera swings back behind your Skylander after a while without "
                    "turning it, while moving");
REXCVAR_DEFINE_DOUBLE(free_camera_recenter_delay, 3.0, "Game",
                      "seconds without turning before the turned camera recenters");
REXCVAR_DEFINE_DOUBLE(free_camera_speed_fov, 0.5, "Game",
                      "how much wider the third-person camera's view gets when moving fast (0 = off)");
REXCVAR_DEFINE_BOOL(free_camera_chase_follow_facing, true, "Game",
                    "the third-person camera turns with the way your Skylander faces (turning on "
                    "the spot too), not only with where it walks");
REXCVAR_DEFINE_DOUBLE(free_camera_slope_tilt, 0.5, "Game",
                      "how much the third-person and turned cameras follow the slope of the "
                      "ground they look along: lower going uphill, higher going downhill "
                      "(0.5 = half the slope's angle, 0 = off)");
REXCVAR_DEFINE_DOUBLE(free_camera_bounce_zoom, 0.6, "Game",
                      "how much further the third-person and turned cameras pull back when "
                      "a bounce pad launches your Skylander (0.6 = 60 % further, 0 = off)");
REXCVAR_DEFINE_BOOL(free_camera_mouse, true, "Game",
                    "hold the right mouse button and move the mouse to turn the camera");
REXCVAR_DEFINE_DOUBLE(free_camera_mouse_sensitivity, 1.0, "Game", "mouse look sensitivity");
REXCVAR_DEFINE_BOOL(free_camera_aim_by_facing, true, "Game",
                    "the right stick's aimed actions (bombs) go relative to where your "
                    "Skylander faces (up = straight ahead) instead of relative to the camera");
REXCVAR_DEFINE_BOOL(free_camera_relative_movement, true, "Game",
                    "the left stick moves your Skylander relative to the turned camera");

namespace {

constexpr uint32_t kMainSceneName = 0x820A9AB0;  // "Transformers2 - Main scene"
constexpr uint32_t kNameOffset = 0x310;
constexpr uint32_t kViewOffset = 0x170;
constexpr uint32_t kPlayers = 0x82AB9C94;  // player list (where_hook.cpp)
constexpr uint32_t kPlayerPosition = 0x22C;

constexpr float kPi = 3.14159265358979f;
constexpr float kYawSpeed = 150.0f * kPi / 180.0f;  // radians per second at full stick
constexpr float kPitchSpeed = 90.0f * kPi / 180.0f;
constexpr float kMinElevation = -20.0f * kPi / 180.0f;  // looking up at most this much
constexpr float kMaxElevation = 75.0f * kPi / 180.0f;   // looking down at most this much
constexpr float kDeadzone = 7849.0f / 32767.0f;
constexpr uint8_t kTriggerThreshold = 30;  // XINPUT_GAMEPAD_TRIGGER_THRESHOLD
constexpr float kMouseRadiansPerPixel = 0.0035f;
// Following the player (jump smoothing, smooth follow).
constexpr float kFollowRate = 8.0f;       // per second, of the way left, on the ground
constexpr float kFollowZRate = 5.0f;      // per second, height on the ground
constexpr float kFollowFallRate = 4.0f;   // per second, height when falling below it in the air
constexpr float kFallBelow = 1.5f;        // in the air, followed down once this far below
constexpr float kFollowSnap = 15.0f;      // further than this: a teleport, follow at once
// Recentering the turned camera behind where the player goes.
constexpr float kRecenterRate = 0.8f, kRecenterMaxSpeed = 60.0f * 3.14159265358979f / 180.0f;
// Speed zoom: from this speed of the player on, full at that one.
// (Over kSpeedWindow, 2026-11-21: walking 7.2 to 8.1, flying 11 to 12.8, sprinting 12 to 19.5.
// Frame by frame the peaks were about twice that: the game moves the player every other frame.)
// Fixed for every character. Walking, measured over kSpeedWindow (2026-11-21), by the speed stat:
// 35 about 5.7, 40 about 6.3, 50 about 6.7, 59 about 6.8, Spyro's 98 about 7.7 (at most 8.1);
// Spyro flying 11 to 12.8, sprinting 12 to 19.5. So it starts just above the fastest walk and is
// full at flying speed.
constexpr float kSpeedFovStart = 8.5f, kSpeedFovFull = 13.0f;
// Falling: from this downward speed on the camera tilts down to show where the player lands, up
// to kFallTilt at kFallFull.
constexpr float kFallStart = 4.0f, kFallFull = 25.0f;
constexpr float kFallTilt = 25.0f * 3.14159265358979f / 180.0f;
constexpr float kFallTiltInRate = 3.0f, kFallTiltOutRate = 1.5f;  // per second
// Slopes: the ground's height kSlopeReach in front of and behind the player along the camera's
// view, from which the slope's angle; free_camera_slope_tilt of it (at most kSlopeMaxTilt) lowers
// the camera going uphill and raises it going downhill. Under kSlopeDeadband nothing (bumps,
// stairs' edges), eased at kSlopeRate of kSlopeMaxTilt per second; kept while briefly airborne.
constexpr float kSlopeReach = 2.5f;
constexpr float kSlopeDeadband = 5.0f * 3.14159265358979f / 180.0f;
constexpr float kSlopeMaxTilt = 18.0f * 3.14159265358979f / 180.0f;
constexpr float kSlopeRate = 1.2f;
constexpr float kSpeedWindow = 0.3f;         // seconds the ground speed is measured over
// Zoomed out, it only goes back in when the speed drops this much below the speed that zoomed it
// out (flying varies between 11 and 12.8: no in and out while flying), then at once, no pause.
constexpr float kSpeedFovDeadband = 3.0f;  // (2 still wobbled a little while flying)
// Back in: eased out (quick at first, gentle at the end), about done within this (chosen in
// testing, 2026-11-21).
constexpr float kSpeedFovOutSeconds = 0.5f;
// Bounce pads: from this upward speed on the camera zooms out and follows up, fully at that one.
constexpr float kRiseStart = 8.0f, kRiseFull = 45.0f;
constexpr float kRiseFollowRate = 6.0f;  // per second, height while rising fast
constexpr float kRiseUpRate = 4.0f, kRiseDownRate = 0.8f;  // the zoom's easing, per second

using Clock = std::chrono::steady_clock;

rex::input::InputSystem* g_input = nullptr;

std::mutex g_mutex;
float g_yaw = 0, g_pitch = 0;  // this frame's turn; pitch: added elevation (looking further down), radians
// The stick's input since it was last used, and a reset (left trigger + right stick click).
float g_delta_yaw = 0, g_delta_pitch = 0;
bool g_reset = false;
// The camera's mode, cycled by left trigger + a click of the right stick: the game's camera, the
// third-person camera (behind the player, turning with where the player goes) and the turned
// camera (the right stick's direction kept in the world). g_reset is that click.
enum class Mode { kGame, kChase, kFree };
Mode g_mode = Mode::kGame;
const char* ModeName(Mode m) { return m == Mode::kGame ? "the game's" : m == Mode::kChase ? "third person" : "turned"; }
// The third-person camera's heading: where the player last went (from the player's movement).
bool g_move_valid = false;
float g_move_heading = 0;
// The player's speed on the ground (smoothed, units per second) and when the player last moved.
float g_speed = 0;
Clock::time_point g_move_time{};
// When the camera was last turned by hand (the turned camera's recentering waits for a while).
Clock::time_point g_last_turn_time{};
// Mouse look: the cursor's place when the right button went down (it is kept there).
bool g_mouse_active = false;
long g_mouse_x = 0, g_mouse_y = 0;
// The speed zoom's share now (0..1, eased).
float g_speed_fov = 0;
// The player's upward speed (smoothed), and the bounce zoom's share now (0..1, eased).
float g_vz = 0;
float g_rise = 0;
// Whether the player stands on something (UpdateFollow's ray under the feet), and since when not.
bool g_grounded = true;
Clock::time_point g_airborne_since{};
// The way the player last stepped, frame by frame (aiming by the facing: quick turns count at once).
bool g_step_valid = false;
float g_step_heading = 0;
// The fall tilt now (radians of added elevation, eased).
float g_fall_tilt = 0;
float g_slope_tilt = 0;
float g_view_dir[3] = {0, 0, 0};  // the turned camera's forward, last frame
// The game holds the player (a talk, a cutscene): the camera stays the game's (AutoRelease).
std::atomic<bool> g_held{false};
// The turned camera's own direction in the world, kept while g_free.
bool g_free = false;
float g_heading = 0, g_elevation = 0;  // heading: atan2 of the forward's y and x; elevation: looking down
// ... and its point to look at (relative to the player's feet) and distance from it, from the first turn.
bool g_lock_valid = false;
float g_lock_distance = 15;
// How far the camera is into the player's turn (0 = the game's camera, 1 = the player's): eased
// towards 1 when the player turns it, so it doesn't jump; a reset sets it to 0 at once.
float g_blend = 0;
Clock::time_point g_blend_time{};
constexpr float kBlendSeconds = 0.5f;
// The third-person camera follows where the player goes from this speed of the player on.
constexpr float kChaseMinSpeed = 1.5f;
// Turning round (2026-11-21, chosen in testing): running back
// towards the camera (more than kChaseRunBackAngle off) only swings it after kChaseRunBackSeconds;
// a swing of more than kChaseSideStart keeps its side until under kChaseSideEnd (kept beyond
// kChaseSideKeep); turning speeds up and slows down at kChaseTurnAccel (radians/s^2), its limit
// up to kChaseBigTurnBoost times free_camera_chase_max_turn_speed for a half turn.
constexpr float kChaseRunBackAngle = 140.0f * 3.14159265358979f / 180.0f;
constexpr float kChaseRunBackSeconds = 0.5f;
constexpr float kChaseSideStart = 100.0f * 3.14159265358979f / 180.0f;
constexpr float kChaseSideEnd = 30.0f * 3.14159265358979f / 180.0f;
constexpr float kChaseSideKeep = 150.0f * 3.14159265358979f / 180.0f;
constexpr float kChaseTurnAccel = 12.0f;
constexpr float kChaseBigTurnBoost = 3.0f;
constexpr float kDeferStart = 0.6f, kDeferFull = 1.0f, kDeferSeconds = 0.4f;
// The player held by the game (AutoRelease): a flag at +0x750, or what holds the player at +0x114.
constexpr uint32_t kHeldFlagsOffset = 0x750, kHeldFlag = 0x00010000, kHolderOffset = 0x114;
// How far the camera leaves the player's turn to the game's own camera for a shot of its own (an
// enemy's introduction zoomed the game's camera onto the enemy while the turned camera stayed on
// the player, 2026-11-21; the player keeps the controls then): 0 = the player's, 1 = the game's.
float g_defer = 0;
float Smooth(float t) { return t * t * (3 - 2 * t); }
float EasedBlend() { return Smooth(g_blend) * (1 - Smooth(g_defer)); }
Clock::time_point g_last_input{};
bool g_rs_was_down = false;
// The yaw the main camera was last drawn with, and when (for the left stick).
std::atomic<float> g_applied_yaw{0};
std::atomic<int64_t> g_applied_at{0};
// The game's own camera heading and the player's facing (the way the player last went), this frame
// (for aiming the right stick by the player's facing), and when they were set.
std::atomic<float> g_aim_turn{0};
std::atomic<int64_t> g_aim_at{0};

// The input transform's layout, learned once: where its position is, and whether the axes are its
// rows or its columns; which of them is the forward / right axis, with its sign.
enum class Layout { kUnknown, kFailed, kKnown };
Layout g_layout = Layout::kUnknown;
int g_pos[3];          // indices of the position
int g_axis[3][3];      // g_axis[a][i]: index of component i of axis a (axes in the 3x3's order)
int g_fwd_axis = 0, g_right_axis = 0;
float g_fwd_sign = 1, g_right_sign = 1;
// The input of the current scene build, for learning the layout from the view made of it.
bool g_have_input_copy = false;
float g_input_copy[12];
int g_layout_tries = 0;
// This frame's turn, and the unturned camera it was made for (to turn other passes alike).
bool g_turn_valid = false;
float g_orig_eye[3]{}, g_orig_fwd[3]{};
// How far the camera is pulled in towards the pivot by a wall (eased back out).
float g_pull = 0;
Clock::time_point g_pull_time{};
// How much higher (radians of elevation) the camera is because a wall squeezes it.
float g_squeeze_tilt = 0;
Clock::time_point g_squeeze_time{};
// The game's own values while the turned ones are in its memory.
bool g_restore_pending = false;
uint32_t g_restore_address = 0;
uint32_t g_restore_words[12];
uint32_t g_turned_words[12];  // what was written there, to restore only if it's still ours
// The field of view the scene build read (its r5 + 148, vertical degrees) while ours is there.
bool g_fov_pending = false;
uint32_t g_fov_address = 0, g_fov_word = 0, g_fov_ours = 0;
// The first version's view turning (before the layout is known / if it can't be).
bool g_have_cache = false;
uint32_t g_original[16], g_rotated[16];
float g_cache_yaw = 0, g_cache_pitch = 0;

struct V3 {
  float x, y, z;
};
V3 g_lock_target{0, 0, 0};  // with g_lock_valid: the point the turned camera looks at, from the player's feet
// The point the third-person and turned cameras follow: the player's feet, trailed a little (smooth
// follow) and kept level through jumps (jump smoothing). UpdateFollow, once a frame.
V3 g_follow{0, 0, 0};
bool g_follow_valid = false;
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Length(V3 a) { return std::sqrt(Dot(a, a)); }
V3 Normalized(V3 a) {
  const float l = Length(a);
  return l > 1e-6f ? a * (1.0f / l) : a;
}
V3 Rotate(V3 v, V3 k, float angle) {  // Rodrigues, k a unit axis
  const float c = std::cos(angle), s = std::sin(angle);
  return v * c + Cross(k, v) * s + k * (Dot(k, v) * (1 - c));
}

uint8_t* Base() { return rex::system::kernel_state()->memory()->virtual_membase(); }
uint32_t Load32(uint8_t* base, uint32_t a) { return rex::memory::load_and_swap<uint32_t>(base + a); }
void Store32(uint8_t* base, uint32_t a, uint32_t v) { rex::memory::store_and_swap<uint32_t>(base + a, v); }
float AsFloat(uint32_t bits) {
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}
uint32_t AsBits(float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, 4);
  return bits;
}
bool HeapPointer(uint32_t v) { return v >= 0x40000000 && v < 0x80000000; }
bool GuestPointer(uint32_t v) { return v >= 0x40000000 && v < 0xA0000000; }
int64_t NowTicks() { return Clock::now().time_since_epoch().count(); }

bool PlayerPosition(uint8_t* base, V3& out) {  // where_hook.cpp's PlayerOne
  const uint32_t node = Load32(base, kPlayers + 4);
  if (node == kPlayers || !HeapPointer(node)) return false;
  const uint32_t object = Load32(base, node + 8), counts = Load32(base, node + 12);
  if (!HeapPointer(object) || !HeapPointer(counts) || Load32(base, counts + 4) == 0) return false;
  out = {AsFloat(Load32(base, object + kPlayerPosition)),
         AsFloat(Load32(base, object + kPlayerPosition + 4)),
         AsFloat(Load32(base, object + kPlayerPosition + 8))};
  return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

// Mouse look: while the right mouse button is held in the game's window, the mouse turns the camera
// (the keyboard and mouse mode binds no mouse button, and the game doesn't see the mouse here).
// The cursor is kept where the button went down. Called with g_mutex held.
void MouseLook() {
#ifdef _WIN32
  bool down = false;
  if (REXCVAR_GET(free_camera_mouse) && (GetAsyncKeyState(VK_RBUTTON) & 0x8000)) {
    const HWND window = GetForegroundWindow();
    DWORD process = 0;
    if (window) GetWindowThreadProcessId(window, &process);
    down = process == GetCurrentProcessId();
  }
  if (!down) {
    g_mouse_active = false;
    return;
  }
  POINT cursor;
  if (!GetCursorPos(&cursor)) return;
  if (!g_mouse_active) {
    g_mouse_active = true;
    g_mouse_x = cursor.x, g_mouse_y = cursor.y;
    return;
  }
  const float dx = float(cursor.x - g_mouse_x), dy = float(cursor.y - g_mouse_y);
  if (dx == 0 && dy == 0) return;
  SetCursorPos(int(g_mouse_x), int(g_mouse_y));
  const float k = kMouseRadiansPerPixel * float(std::clamp(REXCVAR_GET(free_camera_mouse_sensitivity), 0.05, 10.0));
  float x = dx * k, y = dy * k;
  if (REXCVAR_GET(free_camera_invert_x)) x = -x;
  if (REXCVAR_GET(free_camera_invert_y)) y = -y;
  // Mouse right: the camera goes round to the right; mouse up (dy < 0): the camera goes down.
  // The other way round felt inverted in testing (2026-11-21), like the stick.
  g_delta_yaw += x;
  g_delta_pitch -= y;
#endif
}

// Reads the right stick and moves the angles. Called with g_mutex held.
void UpdateAngles() {
  const auto now = Clock::now();
  if (g_last_input.time_since_epoch().count() == 0) g_last_input = now;
  const float dt = std::chrono::duration<float>(now - g_last_input).count();
  if (dt < 0.004f) return;
  g_last_input = now;
  MouseLook();
  if (!g_input) return;
  rex::input::X_INPUT_STATE state{};
  if (g_input->GetState(0, &state) != 0) return;
  const auto& pad = state.gamepad;
  const bool rs_down = (uint16_t(pad.buttons) & rex::input::X_INPUT_GAMEPAD_RIGHT_THUMB) != 0;
  const bool active = !REXCVAR_GET(free_camera_hold_lt) || pad.left_trigger > kTriggerThreshold;
  if (!active) {
    g_rs_was_down = rs_down;
    return;
  }
  if (rs_down && !g_rs_was_down) g_reset = true;
  g_rs_was_down = rs_down;
  auto axis = [](int16_t raw) {
    float v = std::max(-1.0f, float(raw) / 32767.0f);
    if (std::fabs(v) < kDeadzone) return 0.0f;
    return (v - std::copysign(kDeadzone, v)) / (1 - kDeadzone);
  };
  float x = axis(int16_t(pad.thumb_rx)), y = axis(int16_t(pad.thumb_ry));
  if (REXCVAR_GET(free_camera_invert_x)) x = -x;
  if (REXCVAR_GET(free_camera_invert_y)) y = -y;
  const float speed = float(std::clamp(REXCVAR_GET(free_camera_speed), 0.1, 5.0));
  const float step = std::min(dt, 0.1f) * speed;
  // Stick right: the camera goes round the player to the right; stick up: the camera goes up and
  // looks down. The other way round felt inverted in testing (2026-11-21).
  g_delta_yaw += x * kYawSpeed * step;
  g_delta_pitch += y * kPitchSpeed * step;
}

// Gives the camera back to the game (as a reset does) when the game wants to show something itself:
// a talk turned the game's camera to the speaker while the player's turned camera kept showing the
// player (2026-11-21). When the game takes the player's controls (a talk or a cutscene), on a level
// change and while the game isn't playing (loading). Guessing it from the game's camera instead
// (a cut, the player off its view's middle) also fired in normal play in the hub. Called with
// g_mutex held, once a frame.
//
// The player held by the game: found by comparing player 1's object (2048 words) standing, in a
// talk with an NPC in the hub and after it (2026-11-21, an idea from testing: in a cutscene the player
// can't move). Only two words differed in the talk alone: +0x750 (00500000 -> 00510000, but see
// PlayerHeld) and +0x114 (0 -> 7C161A2B, which looks like a name hash: what holds the player).
uint32_t PlayerObject(uint8_t* base);  // below

bool PlayerHeld(uint8_t* base, uint32_t& flags, uint32_t& holder) {
  const uint32_t player = PlayerObject(base);
  if (!player) return false;
  flags = Load32(base, player + kHeldFlagsOffset);
  holder = Load32(base, player + kHolderOffset);
  // Only the holder: +0x750 isn't a flags word, in play it held pointers to the level's collision
  // pieces (the ground under the player?) whose bit 0x10000 came and went, a false hold every few
  // seconds (2026-11-21); in the talk snapshot it was 00500000 -> 00510000 by chance.
  return holder != 0;
}

void AutoRelease(uint8_t* base) {
  constexpr uint32_t kCurrentLevel = 0x82CA3700 + 12;  // where_hook.cpp
  constexpr uint32_t kGameState = 0x82AA602C;          // 2 while playing
  static bool have_level = false, was_held = false;
  static uint32_t last_level = 0;
  const char* why = nullptr;
  const uint32_t level = Load32(base, kCurrentLevel);
  if (Load32(base, kGameState) != 2) {
    why = "the game isn't playing (loading)";
  } else if (have_level && level != last_level) {
    why = "level change";
  }
  last_level = level;
  have_level = true;
  uint32_t flags = 0, holder = 0;
  const bool held = PlayerHeld(base, flags, holder);
  if (held != was_held) {
    REXLOG_INFO("Free camera: the game {} the player's controls (flags {:08X}, holder {:08X})",
                held ? "takes" : "gives back", flags, holder);
  }
  if (!why && held && !was_held) why = "the game takes the player's controls";
  was_held = held;
  g_held.store(held);  // and while it lasts the camera can't be turned (chosen in testing)
  if (why && (g_free || g_blend > 0)) {
    g_free = false;
    g_blend = 0;
    g_delta_yaw = g_delta_pitch = 0;
    // The turned camera stays the game's afterwards; the third-person camera comes back by itself
    // (ApplyFreeAngles) once the game lets go.
    if (g_mode == Mode::kFree) g_mode = Mode::kGame;
    REXLOG_INFO("Free camera: back to the game's camera ({})", why);
  }
}

// The way the player faces, read from the player object. Where the game keeps it isn't known, so
// it is learned while the player walks straight: of the unit horizontal vectors in the object
// (+0x100..+0x3FF) that form a rotation with the vector 16 bytes before or after them, the one
// that points along the walk (or a quarter or half turn from it) in nearly every sample. The
// third-person camera then turns with the player even on the spot, where the walk tells nothing
// (turning on the spot is common in play, 2026-11-21). Unlearned if it disagrees with long straight
// walks. Called with g_mutex held.
constexpr uint32_t kFacingFirst = 0x100, kFacingEnd = 0x400;
constexpr int kFacingSlots = int((kFacingEnd - kFacingFirst) / 4) * 4;  // offset x quarter turns
constexpr int kFacingSamples = 40;
int g_facing_offset = -1, g_facing_quarter = 0;
bool g_facing_valid = false;
float g_facing_heading = 0;

bool FacingAt(uint8_t* base, uint32_t object, uint32_t o, V3& v) {
  v = {AsFloat(Load32(base, object + o)), AsFloat(Load32(base, object + o + 4)),
       AsFloat(Load32(base, object + o + 8))};
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
         std::fabs(v.x * v.x + v.y * v.y - 1) < 0.03f && std::fabs(v.z) < 0.05f;
}

bool MatrixRow(uint8_t* base, uint32_t object, uint32_t o, V3 v) {
  for (const int d : {-16, 16}) {
    const int64_t n = int64_t(o) + d;
    if (n < 0) continue;
    V3 w{AsFloat(Load32(base, object + uint32_t(n))), AsFloat(Load32(base, object + uint32_t(n) + 4)),
         AsFloat(Load32(base, object + uint32_t(n) + 8))};
    if (!std::isfinite(w.x) || !std::isfinite(w.y) || !std::isfinite(w.z)) continue;
    if (std::fabs(Length(w) - 1) < 0.03f && std::fabs(Dot(v, w)) < 0.05f) return true;
  }
  return false;
}

void TrackFacing(uint8_t* base, bool straight, float walk_heading) {
  static int hits[kFacingSlots];
  static int samples = 0, disagree = 0;
  g_facing_valid = false;
  const uint32_t object = PlayerObject(base);
  if (!object) return;
  if (g_facing_offset >= 0) {
    V3 v;
    if (!FacingAt(base, object, uint32_t(g_facing_offset), v)) return;
    g_facing_heading = std::remainder(std::atan2(v.y, v.x) - g_facing_quarter * kPi / 2, 2 * kPi);
    g_facing_valid = true;
    if (straight) {
      const float off = std::fabs(std::remainder(g_facing_heading - walk_heading, 2 * kPi));
      disagree = off > kPi / 4 ? disagree + 1 : std::max(0, disagree - 2);
      if (disagree > 30) {  // a second or so of straight walking the other way: wrong after all
        REXLOG_WARN("Free camera: the facing at +0x{:X} disagrees with the walk, learning again",
                    g_facing_offset);
        g_facing_offset = -1, g_facing_valid = false, samples = 0, disagree = 0;
        std::fill(std::begin(hits), std::end(hits), 0);
      }
    }
    return;
  }
  if (!straight) return;
  for (uint32_t o = kFacingFirst; o < kFacingEnd; o += 4) {
    V3 v;
    if (!FacingAt(base, object, o, v)) continue;
    const float angle = std::atan2(v.y, v.x);
    for (int q = 0; q < 4; ++q) {
      if (std::fabs(std::remainder(angle - q * kPi / 2 - walk_heading, 2 * kPi)) < 0.26f) {
        ++hits[int((o - kFacingFirst) / 4) * 4 + q];
      }
    }
  }
  if (++samples < kFacingSamples) return;
  int best = -1, best_hits = 0;
  for (int i = 0; i < kFacingSlots; ++i) {
    if (hits[i] < kFacingSamples * 9 / 10) continue;
    const uint32_t o = kFacingFirst + uint32_t(i / 4) * 4;
    V3 v;
    if (!FacingAt(base, object, o, v) || !MatrixRow(base, object, o, v)) continue;
    // Ties: rows of the matrix whose last row is the position (+0x22C) first, then the most hits.
    const bool row = o == 0x1FC || o == 0x20C || o == 0x21C;
    const int score = hits[i] * 2 + (row ? 1 : 0);
    if (score > best_hits) best_hits = score, best = i;
  }
  samples = 0;
  std::fill(std::begin(hits), std::end(hits), 0);
  if (best < 0) return;  // nothing steady: another round
  g_facing_offset = int(kFacingFirst) + (best / 4) * 4;
  g_facing_quarter = best % 4;
  REXLOG_INFO("Free camera: the player's facing is at +0x{:X} (a quarter turn x{})", g_facing_offset,
              g_facing_quarter);
}

// Where the player goes (for the third-person camera): the heading of the player's movement on the
// ground, while it is quick enough to tell. Called with g_mutex held, once a frame.
void TrackMovement(uint8_t* base, float dt) {
  // Everything over the last kSpeedWindow seconds, not frame by frame: the game moves the player
  // at its own 30 a second whatever the frame rate (every 2nd frame at 60, every 8th at 240), so
  // a frame's step is 0 or a jump (the speed zoom jittered in testing, 2026-11-21).
  (void)dt;
  static std::deque<std::pair<Clock::time_point, V3>> samples;
  static bool have_last = false;
  static V3 last{};
  V3 player;
  if (!PlayerPosition(base, player)) {
    samples.clear();
    have_last = false;
    return;
  }
  const auto now = Clock::now();
  // Frame by frame for aiming: a frame the player moved on (frames without a step are skipped,
  // so it works at any frame rate) gives the way the player faces now.
  if (have_last) {
    const V3 step = player - last;
    const float d = std::sqrt(step.x * step.x + step.y * step.y);
    if (d > 0.01f && d < 5.0f) {
      g_step_heading = std::atan2(step.y, step.x);
      g_step_valid = true;
    }
  }
  last = player;
  have_last = true;
  samples.emplace_back(now, player);
  while (samples.size() > 2 && now - samples[1].first > std::chrono::duration<float>(kSpeedWindow)) {
    samples.pop_front();
  }
  const float span = std::chrono::duration<float>(now - samples.front().first).count();
  if (span < 0.05f) return;
  const V3 moved = player - samples.front().second;
  const float ground = std::sqrt(moved.x * moved.x + moved.y * moved.y) / span;
  const float up = moved.z / span;
  if (ground > 100.0f || std::fabs(up) > 200.0f) {  // a teleport
    samples.clear();
    return;
  }
  // Over a moment (2026-11-21): walking 7.2 to 8.1, flying 11 to 12.8, sprinting 12 to 19.5 on the
  // ground; bounce pads launch at about half the frame-by-frame peaks of 30 to 77 upward.
  g_speed = ground;
  g_vz = up;
  // The way the player goes (third-person camera, aiming by the facing), while moving.
  if (ground > kChaseMinSpeed) {
    g_move_heading = std::atan2(moved.y, moved.x);
    g_move_valid = true;
    g_move_time = now;
  }
  // Learning (and checking) the facing: only on new game steps (30 a second) of a quick, straight
  // walk on the ground, where the facing is the walk's heading.
  static V3 learn_last{};
  const V3 step = player - learn_last;
  const bool new_step = step.x * step.x + step.y * step.y > 1e-6f;
  learn_last = player;
  const bool straight = new_step && g_grounded && ground > 4.0f && g_step_valid &&
                        std::fabs(std::remainder(g_step_heading - g_move_heading, 2 * kPi)) < 0.12f;
  TrackFacing(base, straight, g_move_heading);
}

// Turns the stick's input into this frame's g_yaw / g_pitch for a game camera looking along `fwd`.
// The turned camera keeps its own direction in the world (heading and elevation), whatever the
// game does with its camera meanwhile (the game swings its camera as the player walks; with the
// turn kept relative to it, the turned camera swung along, 2026-11-21); it starts from the game's
// direction at the first turn, and a reset gives the camera back to the game. Called with g_mutex
// held.
void ApplyFreeAngles(uint8_t* base, V3 eye, V3 fwd) {
  if (g_held.load()) {  // the game holds the player: its camera, no turning
    g_reset = false;
    g_free = false;
    g_blend = 0;
    g_delta_yaw = g_delta_pitch = 0;
    g_yaw = g_pitch = 0;
    return;
  }
  const float heading = std::atan2(fwd.y, fwd.x);
  const float elevation = std::asin(std::clamp(-fwd.z, -1.0f, 1.0f));
  const auto now = Clock::now();
  // The right stick by the player's facing: turned by the facing's heading minus the game camera's
  // (the game maps the stick through its camera: up goes along its heading).
  if (g_step_valid) {
    g_aim_turn.store(std::remainder(g_step_heading - heading, 2 * kPi));
    g_aim_at.store(now.time_since_epoch().count());
  }
  float dt = std::chrono::duration<float>(now - g_blend_time).count();
  g_blend_time = now;
  if (!(dt > 0) || dt > 0.25f) dt = 0.25f;
  TrackMovement(base, dt);
  const float chase_elevation =
      float(std::clamp(REXCVAR_GET(free_camera_chase_elevation), -15.0, 70.0)) * kPi / 180.0f;
  auto start_chase = [&] {  // from the game's camera: glides in, behind where the player goes
    g_free = true;
    g_lock_valid = false;
    g_heading = g_facing_valid ? g_facing_heading : g_move_valid ? g_move_heading : heading;
    g_elevation = chase_elevation;
  };
  if (g_reset) {  // the next mode
    g_reset = false;
    g_delta_yaw = g_delta_pitch = 0;
    const Mode next = g_mode == Mode::kGame ? Mode::kChase : g_mode == Mode::kChase ? Mode::kFree : Mode::kGame;
    REXLOG_INFO("Free camera: {} camera", ModeName(next));
    g_mode = next;
    if (next == Mode::kChase) {
      g_blend = std::min(g_blend, 0.0f);
      start_chase();
    } else if (next == Mode::kGame) {  // straight back to the game's camera (chosen in testing)
      g_free = false;
      g_blend = 0;
    }  // kFree: the third-person camera's direction is kept from here on
  }
  if (g_delta_yaw != 0 || g_delta_pitch != 0) {  // the right stick: the turned camera
    if (g_mode == Mode::kGame || !g_free) {
      g_free = true;
      if (g_blend <= 0) {  // a new turn (not one while easing back): from the game's camera now
        g_lock_valid = false;  // MakeTurn takes the distance and point to look at from this frame
        g_heading = heading;
        g_elevation = elevation;
      }
    }
    if (g_mode != Mode::kFree) REXLOG_INFO("Free camera: turned camera");
    g_mode = Mode::kFree;
    g_last_turn_time = now;
    g_heading = std::remainder(g_heading + g_delta_yaw, 2 * kPi);
    g_elevation = std::clamp(g_elevation + g_delta_pitch, kMinElevation, kMaxElevation);
    g_delta_yaw = g_delta_pitch = 0;
  }
  // The turned camera swings back behind where the player goes after a while without turning,
  // slowly, while the player moves.
  if (g_mode == Mode::kFree && g_free && REXCVAR_GET(free_camera_auto_recenter) && g_move_valid &&
      now - g_last_turn_time >
          std::chrono::duration<double>(std::clamp(REXCVAR_GET(free_camera_recenter_delay), 0.5, 60.0)) &&
      now - g_move_time < std::chrono::milliseconds(300)) {
    const float diff = std::remainder(g_move_heading - g_heading, 2 * kPi);
    const float step = std::clamp(diff * std::min(1.0f, dt * kRecenterRate), -dt * kRecenterMaxSpeed,
                                  dt * kRecenterMaxSpeed);
    g_heading = std::remainder(g_heading + step, 2 * kPi);
  }
  if (g_mode == Mode::kChase) {
    if (!g_free) start_chase();  // back after a talk, a cutscene or a level change
    // Behind where the player goes: turns towards it, quicker the further off it is.
    // Not too fast (it spun quickly when turning, 2026-11-21): a share of the angle left per
    // second, at most free_camera_chase_max_turn_speed. Held still while the player is in the
    // air (steering a landing off a bounce pad with the camera swinging was hard in testing,
    // 2026-11-21); a moment's grace so a bump doesn't hold it.
    const bool airborne = REXCVAR_GET(free_camera_chase_hold_in_air) && !g_grounded &&
                          now - g_airborne_since > std::chrono::milliseconds(150);
    // The player's facing when known (it turns on the spot too), else where the player goes.
    const bool use_facing = g_facing_valid && REXCVAR_GET(free_camera_chase_follow_facing);
    static Clock::time_point back_since{};
    static bool running_back = false;
    static float side = 0;            // the way a big swing goes (+1 / -1), 0 = none
    static float velocity = 0;        // radians per second, eased
    if (!(use_facing || g_move_valid) || airborne) {
      velocity = 0;  // held still in the air; nothing to follow
    } else {
      // Followed at once (a short wait before bigger changes was tried and not liked, 2026-11-21).
      const float target = use_facing ? g_facing_heading : g_move_heading;
      float diff = std::remainder(target - g_heading, 2 * kPi);
      // Running back towards the camera: it stays, unless the player keeps going that way.
      if (std::fabs(diff) > kChaseRunBackAngle) {
        if (!running_back) running_back = true, back_since = now;
        if (now - back_since < std::chrono::duration<float>(kChaseRunBackSeconds)) diff = 0;
      } else {
        running_back = false;
      }
      // A big swing keeps the way it started (no flipping between left and right near 180).
      if (std::fabs(diff) > kChaseSideKeep && side != 0 && (diff > 0 ? 1.0f : -1.0f) != side) {
        diff += side * 2 * kPi;
      }
      if (std::fabs(diff) > kChaseSideStart && side == 0) side = diff > 0 ? 1.0f : -1.0f;
      if (std::fabs(diff) < kChaseSideEnd) side = 0;
      // Quicker the further there is to go: a share of the angle per second, its limit raised
      // for big turns (up to kChaseBigTurnBoost x at a half turn); eased in and out.
      const float rate = float(std::clamp(REXCVAR_GET(free_camera_chase_turn_rate), 0.1, 30.0));
      const float big = std::min(1.0f, std::fabs(diff) / kPi);
      const float limit = float(std::clamp(REXCVAR_GET(free_camera_chase_max_turn_speed), 10.0, 1000.0)) *
                          kPi / 180.0f * (1 + (kChaseBigTurnBoost - 1) * big);
      float wanted = std::clamp(diff * rate * (1 + big), -limit, limit);
      // Able to stop in time: no faster than the easing can brake from.
      const float brake = std::sqrt(2 * kChaseTurnAccel * std::fabs(diff));
      wanted = std::clamp(wanted, -brake, brake);
      velocity += std::clamp(wanted - velocity, -kChaseTurnAccel * dt, kChaseTurnAccel * dt);
      float turn = velocity * dt;
      if (std::fabs(turn) > std::fabs(diff) && turn * diff > 0) turn = diff, velocity = 0;
      g_heading = std::remainder(g_heading + turn, 2 * kPi);
    }
    g_elevation = chase_elevation;
  }
  const float step = dt / kBlendSeconds;
  g_blend = std::clamp(g_free ? g_blend + step : g_blend - step, 0.0f, 1.0f);
  if (g_blend <= 0) {
    g_lock_valid = false;
    g_yaw = g_pitch = 0;
    return;
  }
  // The game's own shot: the player far off the middle of the game's camera (in normal play at
  // most about a third, 2026-11-21), or behind it. From kDeferStart of the distance ahead on the
  // camera leans over to the game's, fully at kDeferFull (the picture's edge); eased both ways.
  {
    float target = 0;
    V3 player;
    if (PlayerPosition(base, player)) {
      const V3 to = player + V3{0, 0, 1.0f} - eye;
      const float along = Dot(to, fwd);
      const float off = Length(to - fwd * along);
      const float share = along > 0.5f ? off / along : 10.0f;
      target = std::clamp((share - kDeferStart) / (kDeferFull - kDeferStart), 0.0f, 1.0f);
    }
    const float was = g_defer;
    g_defer += std::clamp(target - g_defer, -dt / kDeferSeconds, dt / kDeferSeconds);
    if ((was < 0.5f) != (g_defer < 0.5f)) {
      REXLOG_INFO("Free camera: {} the game's own shot", g_defer >= 0.5f ? "over to" : "back from");
    }
  }
  const float e = EasedBlend();
  g_yaw = e * std::remainder(g_heading - heading, 2 * kPi);  // the turn around z (counter-clockwise)
  g_pitch = e * (g_elevation - elevation);
}

// The turn for a camera at `eye` looking along `fwd` with right axis `right`: the pivot and the
// rotation, or false if there is nothing to turn. Called with g_mutex held.
struct Turn {
  V3 pivot, pitch_axis;
  float pitch, yaw;
  V3 base_offset{0, 0, 0};  // moves the turned camera to its kept distance from the player
  V3 offset{0, 0, 0};       // moves it in front of a wall
  V3 Apply(V3 v) const { return Rotate(Rotate(v, pitch_axis, -pitch), V3{0, 0, 1}, yaw); }
  // Where the camera at `p` goes before walls are looked at, and with them.
  V3 Wanted(V3 p) const { return pivot + Apply(p - pivot) + base_offset; }
  V3 ApplyPoint(V3 p) const { return Wanted(p) + offset; }
};
bool MakeTurn(uint8_t* base, V3 eye, V3 fwd, V3 right, Turn& turn, float extra_elevation = 0) {
  if (!std::isfinite(eye.x) || std::fabs(Length(fwd) - 1) > 0.05f) return false;
  float distance = 15.0f;  // the point on the view ray nearest player 1, 15 ahead without one
  V3 player;
  const bool have_player = PlayerPosition(base, player);
  if (have_player) distance = std::max(2.0f, Dot(player - eye, fwd));
  turn.pivot = eye + fwd * distance;
  // Turned by the player, the camera keeps its distance and its point to look at relative to the
  // player as they were at the first turn (from the game's, the pivot moved about the player as
  // the game's camera did, 2026-11-21).
  const bool turned = EasedBlend() > 0;
  if (turned && have_player) {
    if (!g_lock_valid) {
      g_lock_valid = true;
      g_lock_target = V3{0, 0, (turn.pivot - player).z};  // straight above the feet: centred whatever the heading
      g_lock_distance = distance;
    }
    if (g_mode == Mode::kChase) g_lock_distance = float(std::clamp(REXCVAR_GET(free_camera_chase_distance), 2.0, 40.0));
    const float e = EasedBlend();  // from the game's pivot and distance to the kept ones
    // Pulled back while a bounce pad launches the player (UpdateFollow's g_rise).
    const float zoom = 1.0f + float(std::clamp(REXCVAR_GET(free_camera_bounce_zoom), 0.0, 3.0)) *
                                  (g_rise * g_rise * (3 - 2 * g_rise));
    // The third-person camera aims free_camera_chase_look_above higher (the player lower on the
    // screen; the default chosen in testing).
    V3 target = (g_follow_valid ? g_follow : player) + g_lock_target;
    if (g_mode == Mode::kChase) target.z += float(std::clamp(REXCVAR_GET(free_camera_chase_look_above), -5.0, 15.0));
    turn.pivot = turn.pivot + (target - turn.pivot) * e;
    distance = distance + (g_lock_distance * zoom - distance) * e;
  }
  // The camera stays at least free_camera_min_height above the player's feet (below them, past a
  // ledge, the ground's overhanging edge cut through the picture with no wall for the rays to find;
  // and water has no collision at all, every mask bit finds only the sea floor under it: with 1,
  // wading at the beach, the camera went under the water's surface, 2026-11-21; 1.5 by default,
  // chosen in testing): its height above the pivot is distance x sin(elevation).
  float min_elevation = kMinElevation;
  if (have_player) {
    const float min_height = float(std::clamp(REXCVAR_GET(free_camera_min_height), 0.0, 10.0));
    const float s = (player.z + min_height - turn.pivot.z) / distance;
    min_elevation = std::max(min_elevation, std::asin(std::clamp(s, -1.0f, 1.0f)));
  }
  const float elevation = std::asin(std::clamp(-fwd.z, -1.0f, 1.0f));
  // Never pushed past the game's own angle (with the stick at rest the camera stays the game's).
  // Turned by the player it keeps its own elevation within the limits.
  const float lo = turned ? min_elevation : std::min(min_elevation, elevation);
  const float hi = turned ? kMaxElevation : std::max(kMaxElevation, elevation);
  const float pitch = std::clamp(elevation + g_pitch + extra_elevation, std::min(lo, hi), hi) - elevation;
  if (!turned && std::fabs(g_yaw) < 1e-4f && std::fabs(pitch) < 1e-4f) return false;
  // Turning by +a around the horizontal right axis raises the forward (right x forward = up).
  turn.pitch_axis = Normalized(V3{right.x, right.y, 0});
  turn.pitch = pitch;
  turn.yaw = g_yaw;
  // Kept distance: the turned camera at the pivot minus its turned forward times the distance.
  turn.base_offset = V3{0, 0, 0};
  if (turned && have_player) {
    const V3 fwd2 = Normalized(turn.Apply(fwd));
    turn.base_offset = (turn.pivot - fwd2 * distance) - (turn.pivot + turn.Apply(eye - turn.pivot));
  }
  return true;
}

Turn g_turn{};  // valid with g_turn_valid

void MarkApplied(float yaw) {
  g_applied_yaw.store(yaw);
  g_applied_at.store(NowTicks());
}

// The camera of a view (row-vector 4x4: the axes are the columns of its 3x3).
void CameraOfView(const float v[16], V3& eye, V3& right, V3& up, V3& fwd) {
  right = {v[0], v[4], v[8]};
  up = {v[1], v[5], v[9]};
  fwd = {v[2], v[6], v[10]};
  const V3 t{v[12], v[13], v[14]};
  eye = (right * t.x + up * t.y + fwd * t.z) * -1.0f;
}

// Learns the input's layout from the view made of it (both unturned). Called with g_mutex held.
void LearnLayout(const float in[12], const float view[16]) {
  V3 eye, right, up, fwd;
  CameraOfView(view, eye, right, up, fwd);
  const V3 axes[3] = {right, up, fwd};
  const float tolerance = 0.05f + 0.001f * Length(eye);
  // Position: the last three floats (4 rows of 3) or every fourth (3 rows of 4).
  const int pos_a[3] = {9, 10, 11}, pos_b[3] = {3, 7, 11};
  const int rows_a[3][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}};
  const int rows_b[3][3] = {{0, 1, 2}, {4, 5, 6}, {8, 9, 10}};
  for (int layout = 0; layout < 2; ++layout) {
    const int* pos = layout == 0 ? pos_a : pos_b;
    const auto& rows = layout == 0 ? rows_a : rows_b;
    const V3 p{in[pos[0]], in[pos[1]], in[pos[2]]};
    if (Length(p - eye) > tolerance) continue;
    // Axes as the 3x3's rows, then as its columns.
    for (int transposed = 0; transposed < 2; ++transposed) {
      int idx[3][3];
      for (int a = 0; a < 3; ++a)
        for (int i = 0; i < 3; ++i) idx[a][i] = transposed ? rows[i][a] : rows[a][i];
      int fwd_axis = -1, right_axis = -1;
      float fwd_sign = 1, right_sign = 1;
      int matched = 0;
      for (int a = 0; a < 3; ++a) {
        const V3 v = Normalized(V3{in[idx[a][0]], in[idx[a][1]], in[idx[a][2]]});
        for (int k = 0; k < 3; ++k) {
          const float d = Dot(v, axes[k]);
          if (std::fabs(d) > 0.995f) {
            ++matched;
            if (k == 2) fwd_axis = a, fwd_sign = d > 0 ? 1.0f : -1.0f;
            if (k == 0) right_axis = a, right_sign = d > 0 ? 1.0f : -1.0f;
          }
        }
      }
      if (matched != 3 || fwd_axis < 0 || right_axis < 0) continue;
      std::memcpy(g_pos, pos, sizeof(g_pos));
      std::memcpy(g_axis, idx, sizeof(g_axis));
      g_fwd_axis = fwd_axis, g_fwd_sign = fwd_sign;
      g_right_axis = right_axis, g_right_sign = right_sign;
      g_layout = Layout::kKnown;
      REXLOG_INFO("Free camera: camera transform laid out as {} with the axes as {}, forward = {}axis {}, "
                  "right = {}axis {}; turning the camera at the scene build from now on",
                  layout == 0 ? "4 rows of 3" : "3 rows of 4", transposed ? "columns" : "rows",
                  fwd_sign < 0 ? "-" : "", fwd_axis, right_sign < 0 ? "-" : "", right_axis);
      return;
    }
  }
  if (++g_layout_tries >= 30) {
    g_layout = Layout::kFailed;
    REXLOG_WARN("Free camera: couldn't match the camera transform ({} {} {} / {} {} {} / {} {} {} / {} {} {}) "
                "to its view (eye {} {} {}); only the picture turns",
                in[0], in[1], in[2], in[3], in[4], in[5], in[6], in[7], in[8], in[9], in[10], in[11],
                eye.x, eye.y, eye.z);
  }
}

// A view turned by `turn`. False if the result isn't usable.
bool TurnView(const Turn& turn, const float in[16], float out[16]) {
  V3 eye, right, up, fwd;
  CameraOfView(in, eye, right, up, fwd);
  const V3 r2 = Normalized(turn.Apply(right)), u2 = Normalized(turn.Apply(up)),
           f2 = Normalized(turn.Apply(fwd));
  const V3 eye2 = turn.ApplyPoint(eye);
  const float m[16] = {r2.x, u2.x, f2.x, in[3],  r2.y, u2.y, f2.y, in[7],
                       r2.z, u2.z, f2.z, in[11], -Dot(eye2, r2), -Dot(eye2, u2), -Dot(eye2, f2), in[15]};
  std::memcpy(out, m, sizeof(m));
  for (int i = 0; i < 16; ++i)
    if (!std::isfinite(out[i])) return false;
  return true;
}

// The first version: turns a view directly. False if nothing to turn.
bool RotateView(uint8_t* base, const float in[16], float out[16]) {
  V3 eye, right, up, fwd;
  CameraOfView(in, eye, right, up, fwd);
  ApplyFreeAngles(base, eye, fwd);
  Turn turn;
  if (!MakeTurn(base, eye, fwd, right, turn) || !TurnView(turn, in, out)) return false;
  MarkApplied(turn.yaw);
  return true;
}

// Puts the game's own transform back if what is in its memory is still the turned one. Called with
// g_mutex held.
void RestoreIfOurs(uint8_t* base) {
  if (g_fov_pending) {
    g_fov_pending = false;
    if (Load32(base, g_fov_address) == g_fov_ours) Store32(base, g_fov_address, g_fov_word);
  }
  if (!g_restore_pending) return;
  g_restore_pending = false;
  for (int i = 0; i < 12; ++i)
    if (Load32(base, g_restore_address + 4 * i) != g_turned_words[i]) return;  // the game wrote it
  for (int i = 0; i < 12; ++i) Store32(base, g_restore_address + 4 * i, g_restore_words[i]);
}

}  // namespace

namespace ssarecomp {

void FreeCameraFrameStart(uint8_t* base) {
  std::lock_guard lock(g_mutex);
  RestoreIfOurs(base);
}

void InstallFreeCamera(rex::system::IInputSystem* input_system) {
  g_input = dynamic_cast<rex::input::InputSystem*>(input_system);
  REXLOG_INFO("Free camera: {}", g_input ? "ready" : "no input system");
}

}  // namespace ssarecomp

extern "C" void __imp__sub_82847AC8(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_828578E8(PPCContext& __restrict ctx, uint8_t* base);

namespace {

constexpr float kWallMargin = 0.8f;   // kept between the camera and a wall
constexpr float kCameraRadius = 0.7f; // the four extra rays' offset (the picture's edges)
constexpr float kGroundClearance = 0.7f;  // kept between the camera and the ground under it
// Squeezed by a wall: from kSqueezeStart of its distance pulled in on, the camera rises, up to
// kSqueezeTilt more elevation at kSqueezeFull.
constexpr float kSqueezeStart = 0.3f, kSqueezeFull = 0.8f;
constexpr float kSqueezeTilt = 35.0f * kPi / 180.0f;
constexpr float kSqueezeRiseSpeed = 120.0f * kPi / 180.0f, kSqueezeFallSpeed = 45.0f * kPi / 180.0f;
constexpr float kHeadHeight = 1.8f;   // where the rays start, above the player's feet
constexpr float kMinDistance = 1.0f;  // never closer to the pivot than this
constexpr float kPullOutSpeed = 8.0f; // units per second the camera eases back out

struct RayHit {
  bool hit = false;
  float fraction = 1;
  V3 point{}, normal{};
  uint32_t object = 0;
  uint32_t material = 0;  // result +24
};

// The game's collision ray from `from` to `to`. Runs the game's function on a stack frame below
// the current one and leaves ctx as it was.
RayHit CollisionRay(PPCContext& ctx, uint8_t* base, V3 from, V3 to, uint32_t ignore, uint32_t mask) {
  const PPCContext saved = ctx;
  const uint32_t old_sp = ctx.r1.u32;
  const uint32_t sp = (old_sp - 0x300) & ~0xFu;
  const uint32_t data = sp + 0x100, start = data, end = data + 0x10, result = data + 0x20;
  Store32(base, sp, old_sp);  // the back chain, as stwu leaves it
  Store32(base, start + 0, AsBits(from.x)), Store32(base, start + 4, AsBits(from.y)),
      Store32(base, start + 8, AsBits(from.z));
  Store32(base, end + 0, AsBits(to.x)), Store32(base, end + 4, AsBits(to.y)), Store32(base, end + 8, AsBits(to.z));
  for (uint32_t i = 0; i < 0x40; i += 4) Store32(base, result + i, 0);
  ctx.r1.u64 = sp;
  ctx.r3.u64 = start;
  ctx.r4.u64 = end;
  ctx.r5.u64 = mask;
  ctx.r6.u64 = ignore;
  ctx.r7.u64 = 0;
  ctx.r8.u64 = result;
  ctx.f1.f64 = 0.0;
  __imp__sub_82847AC8(ctx, base);
  RayHit r;
  r.hit = (ctx.r3.u32 & 0xFF) != 0;
  auto vec = [&](uint32_t a) {
    return V3{AsFloat(Load32(base, a)), AsFloat(Load32(base, a + 4)), AsFloat(Load32(base, a + 8))};
  };
  r.point = vec(result);
  r.normal = vec(result + 12);
  r.fraction = AsFloat(Load32(base, result + 28));
  r.object = Load32(base, result + 32);
  r.material = Load32(base, result + 24);
  ctx = saved;
  // The fraction field (+28) isn't a 0..1 fraction for every kind of shape (2026-11-21: with mask
  // 16 it read 1.0 for walls hit 1.5 into a 17 long ray), the hit point is: the fraction from it.
  const V3 way = to - from;
  const float length2 = Dot(way, way);
  if (r.hit && length2 > 1e-8f && std::isfinite(r.point.x) && std::isfinite(r.point.y) && std::isfinite(r.point.z)) {
    r.fraction = std::clamp(Dot(r.point - from, way) / length2, 0.0f, 1.0f);
  } else {
    r.hit = false;
    r.fraction = 1;
  }
  return r;
}

// The level's surfaces are one-sided: a ray from the player up through the underside of a slope
// doesn't see it, though from the camera's side it is in the way (the camera rose into a grassy
// slope, 2026-11-21). So the way is also looked at backwards, from `to`: the nearer of the two
// (as a fraction from `from`) counts. Hits close to `from` are the player's own body.
float BackwardsToo(PPCContext& ctx, uint8_t* base, V3 from, V3 to, uint32_t player, uint32_t mask, float forward) {
  const float length = Length(to - from);
  const RayHit r = CollisionRay(ctx, base, to, from, player, mask);
  if (!r.hit || (player && r.object == player)) return forward;
  const float at = 1 - r.fraction;  // from `from`
  if (at * length < 0.6f) return forward;
  return std::min(forward, at);
}

// The first hit from `from` to `to` that isn't the player (nor right at the start): its fraction
// of the whole way, 1 for none.
float FirstWall(PPCContext& ctx, uint8_t* base, V3 from, V3 to, uint32_t player, uint32_t mask,
                RayHit* first = nullptr) {
  const V3 way = to - from;
  const float length = Length(way);
  if (length < 1e-3f) return 1;
  float done = 0;  // fraction of the whole way already passed
  for (int tries = 0; tries < 4; ++tries) {
    const V3 start = from + way * done;
    const RayHit r = CollisionRay(ctx, base, start, to, player, mask);
    if (first && tries == 0) *first = r;
    if (!r.hit) return 1;
    const float at = done + (1 - done) * r.fraction;
    const bool self = player && (r.object == player);
    if (!self && at * length > 0.3f) return BackwardsToo(ctx, base, from, to, player, mask, at);
    done = std::min(1.0f, at + 0.05f / length);  // step past it and look again
    if (done >= 1) break;
  }
  return BackwardsToo(ctx, base, from, to, player, mask, 1);
}

uint32_t PlayerObject(uint8_t* base) {
  const uint32_t node = Load32(base, kPlayers + 4);
  if (node == kPlayers || !HeapPointer(node)) return 0;
  const uint32_t object = Load32(base, node + 8);
  return HeapPointer(object) ? object : 0;
}

// How far the turned camera has to come in towards the pivot to stay in front of walls (before
// easing). Called with g_mutex held.
float WallPull(PPCContext& ctx, uint8_t* base, V3 eye, const Turn& turn) {
  const V3 wanted = turn.Wanted(eye);
  const V3 out = wanted - turn.pivot;
  const float distance = Length(out);
  float needed = 0;
  if (REXCVAR_GET(free_camera_collision) && distance > kMinDistance) {
    // From about the player's head (kHeadHeight above the feet, lower under a low ceiling) to the
    // wanted camera; without a player from the pivot. From the middle, going down stairs with the
    // camera above and behind, the steps behind the player were "walls" (2026-11-21).
    const uint32_t player = PlayerObject(base);
    const uint32_t mask = uint32_t(REXCVAR_GET(free_camera_collision_mask));
    V3 from = turn.pivot, feet;
    if (PlayerPosition(base, feet)) {
      const V3 low = feet + V3{0, 0, 0.3f}, head = feet + V3{0, 0, kHeadHeight};
      const float up = FirstWall(ctx, base, low, head, player, mask);
      from = low + (head - low) * std::max(0.0f, up - 0.2f / (kHeadHeight - 0.3f));
    }
    // Four rays: to the camera and to three points around it (its picture's edges), the worst one
    // decides; then again from where that puts the camera (something beside the new spot that the
    // rays to the wanted spot passed by), up to three times.
    const V3 dir = Normalized(out);
    const V3 cam_right = Normalized(Cross(dir * -1.0f, V3{0, 0, 1}));
    const V3 cam_up = Normalized(Cross(cam_right, dir * -1.0f));
    // No ray below the camera: near the ground at a low angle it hit the ground and pulled the
    // camera in and out over and over (2026-11-21); the ground under the camera is handled below.
    const V3 offsets[4] = {V3{0, 0, 0}, cam_right * kCameraRadius, cam_right * -kCameraRadius,
                           cam_up * kCameraRadius};
    float reach = distance;  // how far from the pivot the camera may be
    for (int round = 0; round < 3; ++round) {
      const V3 spot = turn.pivot + dir * reach;
      float blocked = 0;
      for (int i = 0; i < 4; ++i) {
        const V3 target = spot + offsets[i];
        const float f = FirstWall(ctx, base, from, target, player, mask);
        if (f < 1.0f) blocked = std::max(blocked, (1 - f) * Length(target - from));
      }
      if (blocked <= 0) break;
      const float shorter = std::max(kMinDistance, reach - (blocked + kWallMargin));
      if (shorter >= reach - 1e-3f) break;
      reach = shorter;
    }
    needed = std::clamp(distance - reach, 0.0f, distance - kMinDistance);
  }
  return needed;
}

// Moves the turned camera in by `needed` (in at once, never through the wall; back out eased) and
// keeps it above the ground under it: sets turn.offset. Called with g_mutex held.
void KeepOutOfWalls(PPCContext& ctx, uint8_t* base, V3 eye, Turn& turn, float needed) {
  const auto now = Clock::now();
  float dt = std::chrono::duration<float>(now - g_pull_time).count();
  g_pull_time = now;
  if (!(dt > 0) || dt > 0.25f) dt = 0.25f;
  const V3 wanted = turn.Wanted(eye);
  const V3 out = wanted - turn.pivot;
  const float distance = Length(out);
  g_pull = needed >= g_pull ? needed : std::max(needed, g_pull - kPullOutSpeed * dt);
  if (distance > 1e-4f) turn.offset = out * (-g_pull / distance);
  // Ground: the camera's own spot kept kGroundClearance above what is under it (the ground rising
  // towards the camera put it underground with the player's feet below it, 2026-11-21).
  if (REXCVAR_GET(free_camera_collision)) {
    const V3 eye2 = wanted + turn.offset;
    const V3 top = eye2 + V3{0, 0, 1.5f}, bottom = eye2 - V3{0, 0, kGroundClearance};
    const RayHit ground =
        CollisionRay(ctx, base, top, bottom, PlayerObject(base), uint32_t(REXCVAR_GET(free_camera_collision_mask)));
    if (ground.hit && ground.normal.z > 0.3f) {
      const float lift = ground.point.z + kGroundClearance - eye2.z;
      if (lift > 0) turn.offset = turn.offset + V3{0, 0, std::min(lift, 3.0f)};
    }
  }
}

// Moves g_follow after the player: on the ground towards the player (a little behind with smooth
// follow, at once without); in the air it keeps its height (no bobbing with jumps) unless the
// player falls well below it. Grounded: something under the feet within a short ray. Called with
// g_mutex held, once a frame.
void UpdateFollow(PPCContext& ctx, uint8_t* base) {
  static Clock::time_point last{};
  const auto now = Clock::now();
  float dt = std::chrono::duration<float>(now - last).count();
  last = now;
  if (!(dt > 0) || dt > 0.25f) dt = 0.25f;
  // Bounce zoom: eased towards how fast the player rises (quick out, slow back in).
  {
    const float t = std::clamp((g_vz - kRiseStart) / (kRiseFull - kRiseStart), 0.0f, 1.0f);
    g_rise += std::clamp(t - g_rise, -dt * kRiseDownRate, dt * kRiseUpRate);
  }
  V3 player;
  if (!PlayerPosition(base, player)) {
    g_follow_valid = false;
    return;
  }
  if (!g_follow_valid || Length(player - g_follow) > kFollowSnap) {
    g_follow = player;
    g_follow_valid = true;
    return;
  }
  const bool smooth = REXCVAR_GET(free_camera_smooth_follow);
  const float xy = smooth ? std::min(1.0f, dt * kFollowRate) : 1.0f;
  g_follow.x += (player.x - g_follow.x) * xy;
  g_follow.y += (player.y - g_follow.y) * xy;
  if (!REXCVAR_GET(free_camera_jump_smoothing)) {
    g_follow.z += (player.z - g_follow.z) * xy;
    return;
  }
  const RayHit ground = CollisionRay(ctx, base, player + V3{0, 0, 0.3f}, player - V3{0, 0, 0.6f},
                                     PlayerObject(base), uint32_t(REXCVAR_GET(free_camera_collision_mask)));
  if (ground.hit != g_grounded) {
    g_grounded = ground.hit;
    if (!g_grounded) g_airborne_since = now;
  }
  if (ground.hit) {
    g_follow.z += (player.z - g_follow.z) * std::min(1.0f, dt * kFollowZRate);
  } else if (g_vz > kRiseStart && player.z > g_follow.z) {  // launched up: follow (no jumps here)
    g_follow.z += (player.z - g_follow.z) * std::min(1.0f, dt * kRiseFollowRate);
  } else if (player.z < g_follow.z - kFallBelow) {
    g_follow.z += (player.z + kFallBelow - g_follow.z) * std::min(1.0f, dt * kFollowFallRate);
  }
}

}  // namespace

// The main scene's per-frame build: r4 = the game camera's transform. Turns it first.
REX_HOOK_RAW(sub_828578E8) {
  FreeCameraSceneBuild(ctx, base);
  __imp__sub_828578E8(ctx, base);
}

void FreeCameraSceneBuild(PPCContext& ctx, uint8_t* base) {
  std::lock_guard lock(g_mutex);
  g_have_input_copy = false;
  RestoreIfOurs(base);  // not back yet (no update since the last scene build): never turn twice
  g_turn_valid = false;
  if (!REXCVAR_GET(free_camera)) return;
  // Field of view: the scene build reads it from its r5 + 148 (vertical degrees; it goes to the
  // render state's +0x3E0, 992, from which sub_826340A0 makes the projection and the culling
  // planes), so the game's own culling widens with it. Back at the next update like the transform
  // (on the stack it is the caller's own copy and stays).
  {
    const double fov = REXCVAR_GET(free_camera_fov);
    const uint32_t address = ctx.r5.u32 + 148;
    // Speed zoom (with the third-person or turned camera): a wider view when moving fast, eased.
    {
      static Clock::time_point last{};
      const auto now = Clock::now();
      float dt = std::chrono::duration<float>(now - last).count();
      last = now;
      if (!(dt > 0) || dt > 0.25f) dt = 0.25f;
      // Only with the third-person camera (chosen in testing, 2026-11-21).
      const bool ours = g_mode == Mode::kChase && g_blend > 0 && !g_held.load();
      const float start = kSpeedFovStart, full = kSpeedFovFull;
      float target = ours ? std::clamp((g_speed - start) / (full - start), 0.0f, 1.0f) : 0.0f;
      // Held: out at once, back in only when clearly slower (kSpeedFovDeadband).
      static float held_target = 0;
      const float band = kSpeedFovDeadband / (full - start);
      if (!ours) {
        held_target = 0;
      } else if (target >= held_target || target < held_target - band) {
        held_target = target;
      }
      if (held_target >= g_speed_fov) {
        g_speed_fov = std::min(held_target, g_speed_fov + dt * 1.5f);
      } else {  // 95 % of the way in kSpeedFovOutSeconds
        g_speed_fov += (held_target - g_speed_fov) * (1.0f - std::exp(-3.0f * dt / kSpeedFovOutSeconds));
        if (g_speed_fov < 0.002f) g_speed_fov = 0;
      }
    }
    const float widen = 1.0f + float(std::clamp(REXCVAR_GET(free_camera_speed_fov), 0.0, 1.0)) *
                                   (g_speed_fov * g_speed_fov * (3 - 2 * g_speed_fov));
    const uint32_t old = GuestPointer(ctx.r5.u32) ? Load32(base, address) : 0;
    const bool valid = AsFloat(old) > 1 && AsFloat(old) < 179;
    // The setting's field of view only with the cameras the mod adds (chosen in testing: the
    // game's own camera keeps the game's), eased in and out with them.
    const float ours = (g_mode != Mode::kGame && !g_held.load()) ? Smooth(g_blend) : 0.0f;
    if (valid && !g_held.load() && ((fov > 0 && ours > 0) || widen > 1.0001f)) {
      float aspect = AsFloat(Load32(base, 0x82BB45EC));  // the display's (ultrawide_hook.cpp)
      if (!(aspect > 0.5f && aspect < 5.0f)) aspect = 16.0f / 9.0f;
      // The vertical field of view: the game's, towards the setting's (horizontal), then widened.
      double tan_half = std::tan(double(AsFloat(old)) * kPi / 360.0);
      if (fov > 0) {
        const double mine = std::tan(std::clamp(fov, 30.0, 150.0) * kPi / 360.0) / aspect;
        tan_half += (mine - tan_half) * ours;
      }
      tan_half *= widen;
      const float vertical = float(2.0 * std::atan(tan_half) * 180.0 / kPi);
      {
        g_fov_ours = AsBits(vertical);
        Store32(base, address, g_fov_ours);
        g_fov_pending = ctx.r5.u32 < 0x70000000;  // not on the stack
        g_fov_address = address, g_fov_word = old;
      }
    }
  }
  const uint32_t address = ctx.r4.u32;
  if (!GuestPointer(address)) return;
  UpdateAngles();
  uint32_t words[12];
  float in[12];
  for (int i = 0; i < 12; ++i) {
    words[i] = Load32(base, address + 4 * i);
    in[i] = AsFloat(words[i]);
    if (!std::isfinite(in[i])) return;
  }
  if (g_layout == Layout::kUnknown) {  // FreeCameraHook learns it from the view made of this
    std::memcpy(g_input_copy, in, sizeof(in));
    g_have_input_copy = true;
    return;
  }
  if (g_layout != Layout::kKnown) return;
  auto axis = [&](int a) { return V3{in[g_axis[a][0]], in[g_axis[a][1]], in[g_axis[a][2]]}; };
  const V3 eye{in[g_pos[0]], in[g_pos[1]], in[g_pos[2]]};
  const V3 fwd = Normalized(axis(g_fwd_axis) * g_fwd_sign);
  const V3 right = Normalized(axis(g_right_axis) * g_right_sign);
  AutoRelease(base);
  ApplyFreeAngles(base, eye, fwd);
  UpdateFollow(ctx, base);
  // Falling (in the air, going down): tilted down to see where the player lands, eased.
  {
    static Clock::time_point last{};
    const auto now = Clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    if (!(dt > 0) || dt > 0.25f) dt = 0.25f;
    const float t = (g_mode != Mode::kGame && !g_grounded)
                        ? std::clamp((-g_vz - kFallStart) / (kFallFull - kFallStart), 0.0f, 1.0f)
                        : 0.0f;
    const float target = Smooth(t) * kFallTilt;
    const float rate = target > g_fall_tilt ? kFallTiltInRate : kFallTiltOutRate;
    g_fall_tilt += std::clamp(target - g_fall_tilt, -dt * rate * kFallTilt, dt * rate * kFallTilt);
    // Slopes, on the ground: two rays down, ahead of and behind the player along the view.
    const float amount = float(std::clamp(REXCVAR_GET(free_camera_slope_tilt), 0.0, 1.0));
    float slope_target = g_slope_tilt;  // in the air: kept (no change mid-bounce)
    V3 feet;
    const V3 flat = Normalized(V3{g_view_dir[0], g_view_dir[1], 0});
    if (g_mode == Mode::kGame || amount <= 0 || Length(flat) < 0.5f) {
      slope_target = 0;
    } else if (g_grounded && PlayerPosition(base, feet)) {
      const uint32_t mask = uint32_t(REXCVAR_GET(free_camera_collision_mask));
      const uint32_t player = PlayerObject(base);
      auto height = [&](V3 at, float& z) {
        const RayHit r = CollisionRay(ctx, base, at + V3{0, 0, 3.0f}, at - V3{0, 0, 4.0f}, player, mask);
        if (r.hit) z = r.point.z;
        return r.hit;
      };
      float ahead = 0, behind = 0;
      slope_target = 0;
      if (height(feet + flat * kSlopeReach, ahead) && height(feet - flat * kSlopeReach, behind)) {
        float angle = std::atan2(ahead - behind, 2 * kSlopeReach);  // > 0: uphill along the view
        const float sign = angle < 0 ? -1.0f : 1.0f;
        angle = std::max(0.0f, std::fabs(angle) - kSlopeDeadband) * sign;
        slope_target = std::clamp(-angle * amount, -kSlopeMaxTilt, kSlopeMaxTilt);
      }
    }
    g_slope_tilt += std::clamp(slope_target - g_slope_tilt, -dt * kSlopeRate * kSlopeMaxTilt,
                               dt * kSlopeRate * kSlopeMaxTilt);
  }
  const float tilt = g_fall_tilt + g_slope_tilt;
  Turn turn;
  g_turn_valid = false;
  if (!MakeTurn(base, eye, fwd, right, turn, tilt)) {
    g_pull = 0;
    g_squeeze_tilt = 0;
    return;
  }
  // Squeezed by a wall (pulled in a long way): the camera rises and looks down over the player,
  // more the more it is squeezed. How squeezed is judged at the angle the stick asks for (so the
  // rise doesn't feed back into it), the rise eased.
  float needed = WallPull(ctx, base, eye, turn);
  {
    const float distance = Length(turn.Wanted(eye) - turn.pivot);
    const float squeeze = distance > 1e-3f ? needed / distance : 0;
    float t = std::clamp((squeeze - kSqueezeStart) / (kSqueezeFull - kSqueezeStart), 0.0f, 1.0f);
    t = t * t * (3 - 2 * t);
    const float target = t * kSqueezeTilt;
    const auto now = Clock::now();
    float dt = std::chrono::duration<float>(now - g_squeeze_time).count();
    g_squeeze_time = now;
    if (!(dt > 0) || dt > 0.25f) dt = 0.25f;
    const float rate = target > g_squeeze_tilt ? kSqueezeRiseSpeed : kSqueezeFallSpeed;
    g_squeeze_tilt += std::clamp(target - g_squeeze_tilt, -rate * dt, rate * dt);
    Turn tilted;
    if (g_squeeze_tilt > 0.01f && MakeTurn(base, eye, fwd, right, tilted, tilt + g_squeeze_tilt)) {
      turn = tilted;
      needed = WallPull(ctx, base, eye, turn);
    }
  }
  KeepOutOfWalls(ctx, base, eye, turn, needed);
  {
    const V3 v = turn.Apply(fwd);
    g_view_dir[0] = v.x, g_view_dir[1] = v.y, g_view_dir[2] = v.z;
  }
  g_turn = turn;
  g_orig_eye[0] = eye.x, g_orig_eye[1] = eye.y, g_orig_eye[2] = eye.z;
  g_orig_fwd[0] = fwd.x, g_orig_fwd[1] = fwd.y, g_orig_fwd[2] = fwd.z;
  g_turn_valid = true;
  float out[12];
  std::memcpy(out, in, sizeof(out));
  for (int a = 0; a < 3; ++a) {
    const V3 v = turn.Apply(axis(a));
    out[g_axis[a][0]] = v.x, out[g_axis[a][1]] = v.y, out[g_axis[a][2]] = v.z;
  }
  const V3 p = turn.ApplyPoint(eye);
  out[g_pos[0]] = p.x, out[g_pos[1]] = p.y, out[g_pos[2]] = p.z;
  for (float f : out)
    if (!std::isfinite(f)) return;
  std::memcpy(g_restore_words, words, sizeof(words));
  g_restore_address = address;
  g_restore_pending = true;
  for (int i = 0; i < 12; ++i) {
    g_turned_words[i] = AsBits(out[i]);
    Store32(base, address + 4 * i, g_turned_words[i]);
  }
  MarkApplied(turn.yaw);
}

// At the start of sub_826340A0 (derives a render state's matrices from its view); r3 = the state.
void FreeCameraHook(PPCRegister& r3) {
  if (!REXCVAR_GET(free_camera)) return;
  const uint32_t state = r3.u32;
  if (!HeapPointer(state)) return;
  uint8_t* base = Base();
  const bool main_scene = Load32(base, state + kNameOffset) == kMainSceneName;
  std::lock_guard lock(g_mutex);
  if (!main_scene || g_layout == Layout::kKnown) {
    // Another pass (or the main one, whose input is turned already): turn its view if its camera
    // is the frame's unturned one.
    if (!g_turn_valid) return;
    float view[16];
    for (int i = 0; i < 16; ++i) view[i] = AsFloat(Load32(base, state + kViewOffset + 4 * i));
    V3 eye, right, up, fwd;
    CameraOfView(view, eye, right, up, fwd);
    const V3 orig_eye{g_orig_eye[0], g_orig_eye[1], g_orig_eye[2]};
    const V3 orig_fwd{g_orig_fwd[0], g_orig_fwd[1], g_orig_fwd[2]};
    if (!(Length(eye - orig_eye) < 0.01f + 1e-4f * Length(orig_eye)) || !(Dot(fwd, orig_fwd) > 0.99999f)) return;
    float out[16];
    if (!TurnView(g_turn, view, out)) return;
    for (int i = 0; i < 16; ++i) Store32(base, state + kViewOffset + 4 * i, AsBits(out[i]));
    static bool logged = false;
    if (!logged && !main_scene) {
      logged = true;
      REXLOG_INFO("Free camera: also turning another pass's view (name pointer {:08X})",
                  Load32(base, state + kNameOffset));
    }
    return;
  }

  uint32_t words[16];
  float view[16];
  for (int i = 0; i < 16; ++i) {
    words[i] = Load32(base, state + kViewOffset + 4 * i);
    view[i] = AsFloat(words[i]);
  }
  if (g_layout == Layout::kUnknown && g_have_input_copy) {
    g_have_input_copy = false;
    LearnLayout(g_input_copy, view);
    if (g_layout == Layout::kKnown) return;  // from the next frame on
  }

  // Until then: the first version's turning of the view itself.
  UpdateAngles();
  if (g_have_cache && std::memcmp(words, g_rotated, sizeof(words)) == 0) return;
  const uint32_t* result = nullptr;
  if (g_have_cache && g_cache_yaw == g_yaw && g_cache_pitch == g_pitch &&
      std::memcmp(words, g_original, sizeof(words)) == 0) {
    result = g_rotated;
  } else {
    float out[16];
    if (!RotateView(base, view, out)) {
      g_have_cache = false;
      return;
    }
    std::memcpy(g_original, words, sizeof(words));
    for (int i = 0; i < 16; ++i) g_rotated[i] = AsBits(out[i]);
    g_have_cache = true;
    g_cache_yaw = g_yaw;
    g_cache_pitch = g_pitch;
    result = g_rotated;
  }
  for (int i = 0; i < 16; ++i) Store32(base, state + kViewOffset + 4 * i, result[i]);
}

// Right after the game reads a pad (0x82854AEC): r3 = the result (0 = read), r1 + 96 = the
// X_INPUT_STATE. Turns the left stick by the camera's yaw while the camera is turned.
void FreeCameraPadHook(PPCRegister& r1, PPCRegister& r3) {
  if (r3.u32 != 0 || !REXCVAR_GET(free_camera)) return;
  uint8_t* base = Base();
  const uint32_t pad = r1.u32 + 96 + 4;  // X_INPUT_GAMEPAD
  // While the left trigger turns the camera, the game sees neither it nor the right stick (not while
  // the game holds the player: then the camera can't be turned and the game gets everything).
  if (REXCVAR_GET(free_camera_hold_lt) && !g_held.load() && base[pad + 2] > kTriggerThreshold) {
    const uint16_t buttons = rex::memory::load_and_swap<uint16_t>(base + pad);
    rex::memory::store_and_swap<uint16_t>(base + pad,
                                          uint16_t(buttons & ~rex::input::X_INPUT_GAMEPAD_RIGHT_THUMB));
    base[pad + 2] = 0;
    rex::memory::store_and_swap<uint16_t>(base + pad + 8, 0);
    rex::memory::store_and_swap<uint16_t>(base + pad + 10, 0);
  }
  auto clamp16 = [](float v) { return int16_t(std::clamp(std::lround(v), -32768L, 32767L)); };
  // Turns the stick at `at` by `angle` around z (counter-clockwise seen from above).
  auto turn_stick = [&](uint32_t at, float angle) {
    const float x = float(int16_t(rex::memory::load_and_swap<uint16_t>(base + at)));
    const float y = float(int16_t(rex::memory::load_and_swap<uint16_t>(base + at + 2)));
    if (x == 0 && y == 0) return;
    const float c = std::cos(angle), s = std::sin(angle);
    rex::memory::store_and_swap<uint16_t>(base + at, uint16_t(clamp16(x * c - y * s)));
    rex::memory::store_and_swap<uint16_t>(base + at + 2, uint16_t(clamp16(x * s + y * c)));
  };
  const auto now = NowTicks();
  // The right stick aims (bombs): by the player's facing (chosen in testing, 2026-11-21: up =
  // straight ahead of the player, with any camera); while the left trigger turns the camera it is
  // 0 above. Only while the game builds its scene (not in menus or loading screens).
  bool right_done = false;
  if (REXCVAR_GET(free_camera_aim_by_facing) && !g_held.load() &&
      Clock::duration(now - g_aim_at.load()) < std::chrono::milliseconds(250)) {
    turn_stick(pad + 8, g_aim_turn.load());
    right_done = true;
  }
  if (!REXCVAR_GET(free_camera_relative_movement)) return;
  const float yaw = g_applied_yaw.load();
  if (std::fabs(yaw) < 1e-3f) return;
  // Only while the main scene is being drawn turned (not in menus or loading screens without it).
  if (Clock::duration(now - g_applied_at.load()) > std::chrono::milliseconds(250)) return;
  // The game maps the sticks through its own camera; ours is turned by `yaw` around z, so the
  // left stick (moving) turns the same way, and the right one too if not aimed by the facing
  // (a bomb thrown with it flew off relative to the game's camera, 2026-11-21).
  turn_stick(pad + 4, yaw);
  if (!right_done) turn_stick(pad + 8, yaw);
}
