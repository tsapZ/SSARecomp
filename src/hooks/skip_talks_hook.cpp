// Skip talks (F4 > Game, off by default; the owner, 2026-11-16/17: "die labern zu viel", a general dialog
// skip, some of Hugo's lines can't be skipped).
//
// Whether the player may skip is level data, per object: the loaders read the flags by name (CRC-32
// keys of the levels' mission.str strings, matched to the lis/ori constants in the generated code):
//   sub_8278F7C0  lux::LockCtrl, a talk that holds the player: lockAI, lockPlayer (+69), canSkip (+70),
//                 ForceIdleWhenUnlock
//   sub_827C1BD0  a cinematic: ..., neverSkip (+497), alwaysSkip (+498), ...; sub_827C3338, which decides
//                 whether B skips it, reads both first
// With skip_talks on, each loader runs as it is and then its object is made skippable: canSkip = 1, and
// neverSkip = 0 / alwaysSkip = 1. Both functions take the object in r3. In Level_027 the option made three
// talks skippable that weren't (2026-11-16, skip_test.ps1); the opening talk with Hugo already ends on one B.
// Safe: only flags the game sets per object itself change, to values other objects of the same kind
// have; read when a level loads, so the setting takes effect from the next level.

#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

REXCVAR_DEFINE_BOOL(skip_talks, false, "Game",
                    "Talks and cutscenes in the levels can always be skipped (the game lets you skip "
                    "only some); from the next level on")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

extern "C" void __imp__sub_8278F7C0(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_827C1BD0(PPCContext& __restrict ctx, uint8_t* base);

REX_HOOK_RAW(sub_8278F7C0) {
  const uint32_t talk = ctx.r3.u32;
  __imp__sub_8278F7C0(ctx, base);
  if (REXCVAR_GET(skip_talks) && talk) {
    if (base[talk + 70] == 0) REXLOG_INFO("Skip talks: talk {:08X} made skippable", talk);
    base[talk + 70] = 1;
  }
}

REX_HOOK_RAW(sub_827C1BD0) {
  const uint32_t scene = ctx.r3.u32;
  __imp__sub_827C1BD0(ctx, base);
  if (REXCVAR_GET(skip_talks) && scene) {
    if (base[scene + 497] != 0) REXLOG_INFO("Skip talks: cutscene {:08X} made skippable", scene);
    base[scene + 497] = 0;
    base[scene + 498] = 1;
  }
}
