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
// drawn into the same 16:9 frame and is stretched.

#include <atomic>
#include <cstdint>
#include <string>

#include "ultrawide_hook.h"

#include <rex/cvar.h>
#include <rex/graphics/video_mode_util.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

REXCVAR_DECLARE(std::string, resolution);

REXCVAR_DEFINE_BOOL(ultrawide_fov, true, "Graphics",
                    "On a window wider than 16:9 (resolution), show more to the sides instead of "
                    "black bars: a wider field of view and the picture stretched across the "
                    "window. The HUD is stretched with it");

namespace {

constexpr double kGameAspect = 16.0 / 9.0;

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
}
