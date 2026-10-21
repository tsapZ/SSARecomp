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
// The HUD (ultrawide_hud): the same function keeps the screen's size for the UI next to the
// aspect, the width at 0x82BB45DC, the height at 0x82BB45E0 and their halves at 0x82BB45E4 /
// 0x82BB45E8 (1280, 720, 640, 360). The UI lays itself out with them (the 0x8267xxxx-0x8268xxxx
// UI code reads the half width, two screen-position helpers the width) and the renderer's aspect
// is taken from width / height again (sub_827F5A10); none of them sizes a render target. With
// the width at the window's width for the UI's height (720 x 2560 / 1080 = 1707) the UI is laid
// out for that wide a screen: the HUD keeps its proportions, the parts the game anchors to the
// centre (the figure's portrait, 480 left of it) stay where they are in 16:9, the ones anchored
// to an edge go to the window's edge. Found 2026-10-21 by poking both values in Chapter 1 in a
// 2560x1080 window: the stretched HUD came out in its own proportions on the next frame.
// Off by default: checked by script with the hook, the dialogue box, the figure's name banner and
// the pause menu keep their proportions, but two notices the game parks left of the 16:9 screen
// (a red "+!" and a small figure head, on the figure's arrival) come into view at the left edge,
// and the pause menu's full-screen dimming is drawn 0 .. 1280 wide and leaves the sides light.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include "ultrawide_hook.h"

#include <rex/cvar.h>
#include <rex/graphics/video_mode_util.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/memory/utils.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

REXCVAR_DECLARE(std::string, resolution);

REXCVAR_DEFINE_BOOL(ultrawide_fov, true, "Graphics",
                    "On a window wider than 16:9 (resolution), show more to the sides instead of "
                    "black bars: a wider field of view and the picture stretched across the "
                    "window");
REXCVAR_DEFINE_BOOL(ultrawide_hud, false, "Graphics",
                    "Experimental: with ultrawide_fov on a wider window, the HUD and the menus in "
                    "their own proportions instead of stretched; a few elements the game keeps "
                    "off-screen show at the left edge and the pause menu's dimming leaves the sides "
                    "light");

namespace {

constexpr double kGameAspect = 16.0 / 9.0;
constexpr uint32_t kScreenWidth = 0x82BB45DC;   // the UI's screen width, then its height, ...
constexpr uint32_t kScreenHeight = 0x82BB45E0;
constexpr uint32_t kScreenHalfWidth = 0x82BB45E4;

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
  if (!REXCVAR_GET(ultrawide_hud)) return;
  // The screen's width and half width, stored just before this (the height too).
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  uint32_t bits = rex::memory::load_and_swap<uint32_t>(base + kScreenHeight);
  float height;
  std::memcpy(&height, &bits, 4);
  if (height <= 0) return;
  const float width = float(double(height) * wide), half = width / 2;
  std::memcpy(&bits, &width, 4);
  rex::memory::store_and_swap<uint32_t>(base + kScreenWidth, bits);
  std::memcpy(&bits, &half, 4);
  rex::memory::store_and_swap<uint32_t>(base + kScreenHalfWidth, bits);
  REXLOG_INFO("Ultrawide: UI screen {} x {}", width, height);
}
