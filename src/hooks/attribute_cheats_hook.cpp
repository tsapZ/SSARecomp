// Always crit and elemental power everywhere, as cheats (SkylandersCoreOfLight #48; the other five
// games have them under the same setting names).
//
// Spyro's Adventure works out a hit in its damage rules (lux classes, vtables found by their RTTI,
// debug/ssa_rtti.py, 2026-11-06). The players' rule, SpyroDamageRule (vtable 820AF7E0, slot 0
// sub_82762AC0 -> sub_82761F80), rolls two numbers from 0 to 100 (sub_82145788 with the constants
// 0 and 100 at 0x820A5E40+52/+552): below the hit's critical hit chance (+248 of the hit's info) the
// hit is critical, below the target's block chance (+252) it is blocked; a critical hit adds half
// its damage (the constant 0.5 at +140). Then the element: on one of two ways, picked by a mode
// check (sub_828084C0), the attacker's element (r29; 9 = none, 8 = all) is compared with the area's
// (r27, through the table at 0x82AA5800 that maps a Skylander's element to an area's) or with the
// level's (r30); when it matches, damage * elemental power (+256) * 0.01 is added (0x827621BC). The
// enemies' rule, SpyroEnemyDamageRule (vtable 820AF800, slot 0 sub_82761AF0), has no crit roll and
// an element part of its own, so the enemies' hits stay as they are.
//
// Three midasm hooks (config/default.toml): at the crit compare the roll becomes -1 (below every
// chance, 0 too); at the start of each element way the area's / level's element becomes 8 ("all")
// when the attacker has one. Nothing is written to the game's memory.

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

REXCVAR_DEFINE_BOOL(cheat_always_crit, false, "Extras",
                    "Every hit of your Skylanders is a critical hit");
REXCVAR_DEFINE_BOOL(cheat_elemental_power, false, "Extras",
                    "Your Skylanders get their element's bonus in every area, as if each area were "
                    "theirs");

namespace {

constexpr uint32_t kNoElement = 9;
constexpr uint32_t kAllElements = 8;

void LogElementOnce(uint32_t element, uint32_t area) {
  static bool logged = false;
  if (logged) return;
  logged = true;
  REXLOG_INFO("Extras: elemental power everywhere (element {}, area {})", element, area);
}

}  // namespace

// 0x82762084 in sub_82761F80: fcmpu cr6,f1,f0, f1 the roll (0 to 100), f0 the hit's crit chance.
void CritRollHook(PPCRegister& f0, PPCRegister& f1) {
  if (!REXCVAR_GET(cheat_always_crit)) return;
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("Extras: always crit made a hit critical (chance {:.1f}, roll {:.1f})", f0.f64, f1.f64);
  }
  f1.f64 = -1.0;
}

// 0x82762164: cmpwi cr6,r29,9, the way with the area's element (r27).
void ElementAreaHook(PPCRegister& r27, PPCRegister& r29) {
  if (!REXCVAR_GET(cheat_elemental_power) || r29.u32 == kNoElement) return;
  LogElementOnce(r29.u32, r27.u32);
  r27.u64 = kAllElements;
}

// 0x8276219C: cmpwi cr6,r30,9, the way with the level's element (r30).
void ElementLevelHook(PPCRegister& r29, PPCRegister& r30) {
  if (!REXCVAR_GET(cheat_elemental_power) || r29.u32 == kNoElement) return;
  LogElementOnce(r29.u32, r30.u32);
  r30.u64 = kAllElements;
}
