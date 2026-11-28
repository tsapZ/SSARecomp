// Ultrawide: a wider field of view for the game's 3D camera when the window is wider than 16:9.
//
// This engine has no perspective builder like the later games' (their src/hooks/ultrawide_hook.cpp
// widens its arguments). Its cameras ("Transformers2 - Main scene", "Spyro - PIP Scene", ...: the
// name at +0x00) keep tan(horizontal FOV / 2) at +0xAC, tan(vertical FOV / 2) at +0xB0, the aspect
// at +0xCC and the vertical FOV in degrees at +0xD0, and tan(H / 2) is tan(V / 2) x aspect: the
// game is Hor+ by itself. The aspect comes from one global, the display's: sub_827F56A0 divides the
// video mode's width by its height (1280 / 720) and stores it at 0x82BB45EC (stfs f13,17900(r11)
// at 0x827F5794); five functions copy it into the renderer's state (+988 of the object at
// 0x82BB4F44), from which the cameras take it. Found 2026-10-21: the 16:9 floats in the image's
// data, the code that loads and stores them; then, in Chapter 1 in a 2560x1080 window, poking
// 2560 / 1080 into 0x82BB45EC made the picture keep its proportions and show more at both sides
// on the next frames (screenshots before and after).
// The hook sets that store's value to the window's aspect. The guest frame stays 16:9; with
// present_letterbox off (the app turns it off while this is active, unless set by hand) the
// presenter stretches it across the wider window, which the wider aspect makes up for. The HUD is
// drawn into the same 16:9 frame and would be stretched.
//
// The HUD (ultrawide_hud): the UI is drawn by sub_826828C0 into a view of its own: at 0x82682998
// it has set up the renderer's state (the object at 0x82BB4F44, r11 there) for the HUD pass, an
// orthographic camera (+836 = 0) with the half width at +996 (426.67 for the 1280-wide layout)
// and the half height at +1000, the screen rect at +908..+920 (-1, -1, 1, 1) and the view rect
// at +924..+936 (left, bottom, right, top; also -1..1). The next call (sub_82255B88) makes the
// view from it; on the render thread sub_82630B78 sets the full render target as the viewport
// and turns a view rect other than -1..1 into a scissor (sub_82299D20 with the rect in pixels).
// The hook makes the half width wider by the window's aspect over 16:9 and the view rect's left
// and right narrower by the same share: the 1280-wide layout lands in the middle of the frame
// in its own proportions once the presenter stretches the frame across the window, and the
// scissor cuts what the game lays out beyond the 16:9 screen (two notices it parks at about 800
// left of the centre, more icons at 32:9). Found 2026-11-28: changing the HUD's camera in that
// state (a wider half width) moved the HUD into the middle with the parked notices in view at
// the left edge, the view rect cut them; the second UI pass after it (from 0x82682AAC) already
// works in the display's aspect (+988 = the aspect, half width 1) and lines up with the middle.
// What is drawn full screen in the HUD pass covers only the middle too: the pause menu's dimming
// leaves the sides light. Only while a level plays (InLevel, where_hook.cpp: the game state is 2
// and the level isn't "FrontEnd", the title's): the title, the menus before it and the loading
// screens draw no 3D picture, so their sides would
// show what the frame held before (at the title the intro movie's last frame); they stay
// stretched.
//
// Movies: the cutscenes (movies/sl_sc*.bik, sky_800_moment_*.bik), the figures' movies
// (movies/bash.bik, ...) and the logos are 16:9 Bink movies drawn into the 16:9 frame, which the
// presenter would stretch across the window like the 3D picture. While one plays the presenter
// keeps black bars at the sides instead (present_use_actual_aspect off and present_letterbox on
// just for that time, not saved). The game plays one movie at a time
// through one player (its Bink handle at 0x82BB460C): BinkOpen's header hook (intro_skip_hook.cpp)
// marks the start, the start of BinkClose (sub_829EDEB8, called only from the player's close
// sub_82852AA8, which then clears the handle) the end.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include "ultrawide_hook.h"
#include "where_hook.h"

#include <rex/cvar.h>
#include <rex/graphics/video_mode_util.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/memory/utils.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/flags.h>

REXCVAR_DECLARE(std::string, resolution);

REXCVAR_DEFINE_BOOL(ultrawide_fov, true, "Graphics",
                    "On a window wider than 16:9 (resolution), show more to the sides instead of "
                    "black bars: a wider field of view and the picture stretched across the "
                    "window");
REXCVAR_DEFINE_BOOL(ultrawide_hud, true, "Graphics",
                    "With ultrawide_fov on a wider window, the HUD and the menus in the middle 16:9 "
                    "of the window in their own proportions instead of stretched across it");

namespace {

constexpr double kGameAspect = 16.0 / 9.0;

// The window's aspect the cameras got (0 while ultrawide is off), for the HUD pass.
std::atomic<float> g_wide_aspect{0.0f};

// True while a movie plays with the presenter's bars turned on for it; the aspect setting it
// changed for that, to put back.
std::atomic<bool> g_movie_bars{false};
std::atomic<bool> g_movie_actual_aspect{true};

// The window's aspect ratio from `resolution`, or 0 if it isn't set.
double WindowAspect() {
  int32_t width = 0, height = 0;
  if (!rex::graphics::video_mode_util::TryParseResolutionPreset(REXCVAR_GET(resolution), width,
                                                                height) ||
      width <= 0 || height <= 0) {
    return 0;
  }
  return double(width) / double(height);
}

}  // namespace

namespace ssarecomp {

bool UltrawideActive() {
  return REXCVAR_GET(ultrawide_fov) && WindowAspect() > kGameAspect * 1.01;
}

void MovieOpened(uint32_t width, uint32_t height) {
  // Only when the picture fills the wider window (the app turned the bars off for ultrawide).
  if (!UltrawideActive() || REXCVAR_GET(present_letterbox) || g_movie_bars.exchange(true)) return;
  // The presenter takes the frame's aspect from the window's size (present_use_actual_aspect),
  // so the 16:9 frame counts as wide as the window: off for the movie, the console's 16:9 video
  // mode then gives the aspect and present_letterbox the bars.
  g_movie_actual_aspect = rex::cvar::Query<bool>("present_use_actual_aspect");
  rex::cvar::SetFlagByName("present_use_actual_aspect", "false");
  rex::cvar::SetFlagByName("present_letterbox", "true");
  REXLOG_INFO("Ultrawide: movie {}x{} with bars at the sides", width, height);
}

void MovieClosed() {
  if (!g_movie_bars.exchange(false)) return;
  rex::cvar::SetFlagByName("present_letterbox", "false");
  rex::cvar::SetFlagByName("present_use_actual_aspect", g_movie_actual_aspect ? "true" : "false");
}

}  // namespace ssarecomp

// At 0x827F5794 (stfs f13,17900(r11)): f13 = the display's aspect (width / height of the video
// mode) about to be stored at 0x82BB45EC.
void UltrawideAspectHook(PPCRegister& f13) {
  static std::atomic<bool> logged{false};
  if (!ssarecomp::UltrawideActive() || f13.f64 <= 0) {
    if (!logged.exchange(true)) REXLOG_INFO("Ultrawide: display aspect {}", f13.f64);
    return;
  }
  const double wide = WindowAspect();
  if (wide <= f13.f64) return;
  if (!logged.exchange(true)) REXLOG_INFO("Ultrawide: display aspect {} -> {}", f13.f64, wide);
  f13.f64 = double(float(wide));
  g_wide_aspect = float(wide);
}

// At 0x82682998 in sub_826828C0 (stb r30,987(r11)): r11 = the renderer's state, set up for the
// HUD pass (see the top of this file). Once a frame.
void UltrawideHudHook(PPCRegister& r11) {
  const float wide = g_wide_aspect.load(std::memory_order_relaxed);
  if (wide <= 0.0f || !REXCVAR_GET(ultrawide_hud)) return;
  // Only while a level (or the hub) plays: the title, the menus before it and the loading screens
  // draw no 3D picture, so the sides would keep whatever the frame held before (the last frame of
  // the intro movie); there the UI stays stretched as before.
  // Nor while a movie plays with bars at the sides: the frame is shown in 16:9 then, so the skip
  // prompt on top of it needs no squeezing.
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  if (!ssarecomp::InLevel(base) || g_movie_bars.load(std::memory_order_relaxed)) return;
  const float share = float(kGameAspect) / wide;
  uint8_t* state = base + r11.u32;
  auto get = [&](uint32_t offset) {
    const uint32_t bits = rex::memory::load_and_swap<uint32_t>(state + offset);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
  };
  auto set = [&](uint32_t offset, float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    rex::memory::store_and_swap<uint32_t>(state + offset, bits);
  };
  // +996 is stored fresh every frame just before (0x82682988); the view rect is set from the screen
  // rect, not from its own last value, so nothing adds up from frame to frame.
  set(996, get(996) / share);
  const float left = get(908), right = get(916);
  const float centre = (left + right) / 2, half = (right - left) / 2 * share;
  set(924, centre - half);
  set(932, centre + half);
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true)) REXLOG_INFO("Ultrawide: HUD in the middle {:.0f} %", share * 100);
}

// At the start of BinkClose (sub_829EDEB8): the movie is over (or skipped).
void BinkCloseHook() {
  ssarecomp::MovieClosed();
}
