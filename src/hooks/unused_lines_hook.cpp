// Unused voice lines (F4 > Extras, off by default; Core of Light #138, the owner's yes 2026-11-25): Gill Grunt's hose
// starts with the animation his files have for it, with the valve sound and his effort line, which the game never
// plays (not on the console either: no trace of them in a 47-minute console playthrough, 2026-11-24).
//
// How it was found (dev kit, docs/ssa.md, 2026-11-24): an attack state is a lux::StateAttack (vtable 820B3D84); its
// loader sub_828BB358 (r3 the state, r4 its data) reads the animation name into +264 (CRC-32 key 33E6ABBE, "animName",
// next to animSelect +284 and animChannel +276), and the state's update asks the animation set at +272 for that name
// (sub_8291D2C8 from 828BD80C). The state's own name is the CRC-32 at +8. Holding X, Gill runs two hose states: first
// statePartialAttackWaterHoseInIdle (9C3295CA, 0.4 s = the 24 frames of the start clip), then the loop
// statePartialAttackWaterHoseIdle (0AC36E84). Both name the loop's animation AttackHoseIdle (23008D1A) in the data, so
// the start clip AttackHoseInIdle (4AF3A85F, clip 47273718, with the events valve_open B3AC6870 and effort_heavy
// E55DF07F) is never asked for. The moving start, statePartialAttackWaterHoseIn (491BC1E0), is handled the same way:
// AttackHoseIn (B9C9C5F4) where it names the loop's AttackHose (7E05368F). Gill's animation set (C697C917) has a ref
// for each of the four (In, InIdle, Hose, HoseIdle, in that order next to each other).
//
// With the option on, the loader runs as it is and then a start state that names the loop's animation gets its own
// start animation instead; the loop states keep theirs. The dev kit's first try swapped the name wherever it was asked
// for, so both hose states played the start clip: it played, both sounds came (once per state), the spray looked the
// same (runs\kit\ssa138_remap). Safe: only these two states of Gill Grunt change, to an animation of his own set; a
// state whose data names anything else is left alone. Read when a Skylander's states load, so the option takes
// effect from the next level on.

#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/context.h>

REXCVAR_DEFINE_BOOL(unused_voice_lines, false, "Extras",
                    "Gill Grunt's hose starts with its unused animation, valve sound and voice line (the game's "
                    "files have them, the game never plays them); from the next level on")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace {

constexpr uint32_t kStateName = 8;
constexpr uint32_t kAnimName = 264;

struct StartState {
  uint32_t state;      // CRC-32 of the state's name
  uint32_t loop_anim;  // what the data names
  uint32_t start_anim; // the start clip of Gill's own set
  const char* what;
};
constexpr StartState kStartStates[] = {
    {0x9C3295CA, 0x23008D1A, 0x4AF3A85F, "standing (AttackHoseInIdle)"},
    {0x491BC1E0, 0x7E05368F, 0xB9C9C5F4, "moving (AttackHoseIn)"},
};

}  // namespace

extern "C" void __imp__sub_828BB358(PPCContext& __restrict ctx, uint8_t* base);

REX_HOOK_RAW(sub_828BB358) {
  const uint32_t state = ctx.r3.u32;
  __imp__sub_828BB358(ctx, base);
  if (!REXCVAR_GET(unused_voice_lines) || state < 0x40000000 || state >= 0x80000000) return;
  const uint32_t name = rex::memory::load_and_swap<uint32_t>(base + state + kStateName);
  for (const StartState& s : kStartStates) {
    if (name != s.state) continue;
    if (rex::memory::load_and_swap<uint32_t>(base + state + kAnimName) != s.loop_anim) return;
    rex::memory::store_and_swap<uint32_t>(base + state + kAnimName, s.start_anim);
    REXLOG_INFO("Unused voice lines: Gill Grunt's hose start, {}", s.what);
    return;
  }
}
