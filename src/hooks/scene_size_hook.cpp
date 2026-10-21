// Draws the 3D scene at another size than the Xbox 360's 1120x704 (#30, experimental).
//
// The game draws its scene into textures of 1120x704 and stretches them to the 1280x720 screen,
// 14 % wider and 2 % taller, which is why the picture looks a bit soft. Seen in a RenderDoc capture
// in Chapter 1 (2026-10-21): the main pass writes two 32-bit colour surfaces plus depth, 14 EDRAM
// tiles wide; 3 x 14 x 44 = 1848 tiles fit the 360's 2048, 3 x 16 x 45 = 2160 at 1280x720 would
// not.
//
// The sizes are constants in the code, not data as in Swap Force: every scene texture is created by
// sub_829611D8 (r3 = usage flags, r4 = format, r5 / r6 = width / height, r7 / r8 = levels), called
// with li r5,1120 / li r6,704 at nine places, with 560x352 (half size) at seven and 280x176 at two
// (the last at 0x82869760 picks 560x352 or 280x176 by a setting). With usage bit 0x80 it also
// makes the matching depth texture of the same size (+120 of the 132-byte texture object). One
// texture header (sub_82929EE8 at 0x82929FC8, 1120x704 with four levels) goes straight to the
// header setup sub_823B38E0, and sub_828924B8 keeps the size and a 35 x 22 grid of 32-pixel
// cells (+488 .. +500 of its object). Found by searching the generated code for the constants.
//
// The hooks put scene_size into all of them, the half and quarter sizes halved and quartered.
// The main pass then needs more than the 360's 2048 EDRAM tiles (3 x 16 x 45 = 2160 at 1280x720),
// so like Swap Force's scene_size it needs the SDK's 4096-tile EDRAM (edram_tile_count, which
// ssarecomp_app.h turns on with scene_size); the game places the surfaces beyond tile 2048 by
// itself, no EDRAM hooks needed. Checked 2026-10-21 in Chapter 1 (title, new game, the first
// cutscenes, Spyro on the first island): the picture is right at 1280x720 and 1264x704, the same
// framing as the game's, and the stair steps of the 14 % horizontal stretch are gone.
//
// A configured 1280x720 draws 1264x704, as in Swap Force: there the scene at exactly the screen's
// size crashed while loading (SwapForceRecomp's scene_size_hook.cpp). Here 1 of 6 runs at
// 1280x720 ended in an abort while the save loaded (no log line, cause not found), 0 of 5 at
// 1264x704; 1264x704 is stretched 1.3 % instead of 14 %.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

#include <rex/cvar.h>
#include <rex/graphics/edram_layout.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

REXCVAR_DEFINE_STRING(scene_size, "", "Graphics",
                      "EXPERIMENTAL: draw the 3D scene at this size instead of the Xbox 360's "
                      "1120x704, e.g. 1280x720 (#30). Empty: the game's size")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace {

constexpr int32_t kGameWidth = 1120;
constexpr int32_t kGameHeight = 704;
constexpr int32_t kCellSize = 32;
// The game's screen (back buffer and HUD) size, which the scene doesn't take exactly.
constexpr int32_t kScreenWidth = 1280;
constexpr int32_t kScreenHeight = 720;
constexpr int32_t kScreenSizeStep = 16;

struct Size {
  int32_t width = 0, height = 0;
};

Size ConfiguredSize() {
  Size size;
  const std::string text = REXCVAR_GET(scene_size);
  unsigned w = 0, h = 0;
  if (!text.empty() && std::sscanf(text.c_str(), "%ux%u", &w, &h) == 2 && w >= 320 && h >= 240 &&
      w <= 7680 && h <= 4320) {
    size = {int32_t(w), int32_t(h)};
    if (size.width == kScreenWidth && size.height == kScreenHeight) {
      size = {kScreenWidth - kScreenSizeStep, kScreenHeight - kScreenSizeStep};
      REXLOG_INFO("Scene size: {}x{} is the screen's size; drawing {}x{} instead (#30)",
                  kScreenWidth, kScreenHeight, size.width, size.height);
    }
  } else if (!text.empty()) {
    REXLOG_WARN("Scene size: '{}' is not WIDTHxHEIGHT; keeping the game's size", text);
  }
  return size;
}

const Size& Configured() {
  static const Size size = ConfiguredSize();
  return size;
}

// The game's size divided by `divisor` becomes the configured one divided by it.
bool Swap(PPCRegister& width, PPCRegister& height, const char* where) {
  const Size& size = Configured();
  if (!size.width) return false;
  // Without the larger EDRAM (Vulkan, or a resolution scale area above 25) a larger scene's main
  // pass doesn't fit the 360's 2048 tiles; then the game keeps its own size.
  if (size.width * size.height > kGameWidth * kGameHeight &&
      !rex::graphics::ActiveEdramLayout().is_extended()) {
    static std::atomic<bool> warned{false};
    if (!warned.exchange(true)) {
      REXLOG_WARN("Scene size: {}x{} needs the 4096-tile EDRAM (D3D12, resolution scale up to 5); "
                  "keeping 1120x704",
                  size.width, size.height);
    }
    return false;
  }
  for (int32_t divisor : {1, 2, 4}) {
    if (width.s32 != kGameWidth / divisor || height.s32 != kGameHeight / divisor) continue;
    const int32_t w = size.width / divisor, h = size.height / divisor;
    static std::atomic<int> logged{0};
    if (logged.fetch_add(1) < 24) {
      REXLOG_INFO("Scene size: {} {}x{} -> {}x{}", where, width.s32, height.s32, w, h);
    }
    width.u64 = uint64_t(w);
    height.u64 = uint64_t(h);
    return true;
  }
  return false;
}

}  // namespace

// At the start of sub_829611D8, which creates a texture: r5 / r6 are its width and height.
void SceneTextureSizeHook(PPCRegister& r5, PPCRegister& r6) { Swap(r5, r6, "texture"); }

// Before 0x82929FC8 (bl 0x823b38e0 in sub_82929EE8): r3 / r4 are a texture header's size.
void SceneHeaderSizeHook(PPCRegister& r3, PPCRegister& r4) { Swap(r3, r4, "texture header"); }

// Before 0x828924D8 (stw r11,488(r31) in sub_828924B8): r11 / r10 are the scene size and r9 the
// grid's columns (35, the width in 32-pixel cells), stored next to it.
void SceneGridSizeHook(PPCRegister& r9, PPCRegister& r10, PPCRegister& r11) {
  if (Swap(r11, r10, "grid")) r9.u64 = uint64_t((r11.s32 + kCellSize - 1) / kCellSize);
}

// Before 0x828924E0 (stw r10,492(r31)): r8 is the grid's rows (22) and r10 the height.
void SceneGridRowsHook(PPCRegister& r8, PPCRegister& r10) {
  if (Configured().width && r8.s32 == kGameHeight / kCellSize) {
    r8.u64 = uint64_t((r10.s32 + kCellSize - 1) / kCellSize);
  }
}
