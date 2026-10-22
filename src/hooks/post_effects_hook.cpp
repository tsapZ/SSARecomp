// A glow (bloom) option for Spyro's Adventure, which has the pass but doesn't use it (#35).
//
// sub_8289A898 queues the "Bloom Render" command each frame with a 36-byte parameter block (seven
// words from its r3, two flags at +28 / +29); its callback sub_8289A428 reads them for the bright
// pass (sub_8289A2A8), three blurs (sub_8292AF30) and the composite (sub_8292BC00). The bright pass
// shader's constants are set by CRC32 of their lower-case names: `bloomlevel` (0x1058671A) gets the
// block's +0 (the threshold) and +8, `brightness` (0xD976D6E3) the colour at +12..+20 times +4 (the
// strength). In play all seven words are 0 (+24 0.5): the pass runs with no strength, so nothing
// shows; one scene before a figure is placed has 0.4 / 0.001 / 1.0 with the colour 0.85 / 0.86 / 0.86.
//
// With bloom_strength above 0 the hook at the callback's start (config/default.toml, 0x8289A428)
// fills the block with that scene's threshold and colour and the chosen strength, so the game's own
// pass draws a glow it was never tuned with: a new look, off by default (the owner's call,
// 2026-10-22). The block is made anew each frame, so nothing compounds.
//
// Found 2026-10-22 from the "BloomLevel" key in the shader's constant table (0x8208FC30), the
// render command's name and the CRC32 of the constant names; the values logged every 300 frames
// on the way into Chapter 1.

#include <algorithm>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/memory/utils.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

REXCVAR_DEFINE_DOUBLE(bloom_strength, 0.0, "Graphics",
                      "Glow around bright areas with the game's unused bloom pass: 0 = off, as in "
                      "the game, up to 2");

namespace {

// +0: only the brightest parts glow (the one scene that sets it has 0.4, which makes the whole
// sunlit ground glow at any strength).
constexpr float kThreshold = 0.7f;
// +4 per step of the option: a quarter, so "Mittel" (1) is a clear glow and "Stark" still keeps
// the colours (1 itself washed the ground out).
constexpr float kStrengthPerStep = 0.25f;
constexpr float kLevel = 1.0f;            // +8
constexpr float kColour[3] = {0.85f, 0.86f, 0.86f};  // +12..+20

void StoreFloat(uint8_t* p, float value) { rex::memory::store_and_swap<float>(p, value); }

}  // namespace

// 0x8289A428, the callback's first instruction: r3 = the parameter block.
void BloomRenderHook(PPCRegister& r3) {
  const double strength = std::clamp(REXCVAR_GET(bloom_strength), 0.0, 2.0);
  if (strength <= 0.0 || !r3.u32) return;
  uint8_t* block = rex::system::kernel_state()->memory()->virtual_membase() + r3.u32;
  StoreFloat(block + 0, kThreshold);
  StoreFloat(block + 4, float(strength) * kStrengthPerStep);
  StoreFloat(block + 8, kLevel);
  for (int i = 0; i < 3; ++i) StoreFloat(block + 12 + 4 * i, kColour[i]);
}
