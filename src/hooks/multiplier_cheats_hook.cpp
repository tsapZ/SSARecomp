// Gold and XP multipliers, as cheats (SkylandersCoreOfLight #48; the other five games have them under
// the same setting names, the rows from skylanders-portal).
//
// Spyro's Adventure keeps a Skylander's gold and XP as properties of the player's data, looked up by
// key (sub_82954D28) and set through sub_829555B8. Found 2026-11-06 with the developer kit (runs
// ssa_gold1-7: full gold made 65000 easy to scan for, then a write watch on the level's own gold
// property, key 0x47A91D51, while Chop Chop picked up coins in Oilspill Island): every gain goes
// through one function each,
//
//   sub_8275A440 (gold, key 0x47A91D51): gold = min(gold + amount, 65000)
//   sub_8275A550 (XP, key 0xAF511216):   xp = min(xp + amount, 33000) (nothing while over 33000)
//
// with the amount in r4, unsigned (a pickup's value; neither function can take money away). Two
// midasm hooks at their `mr r31,r4` scale it by the multiplier (config/default.toml); the game's own
// caps stay. The player data's copy of the gold the HUD shows (+0x448 of 0x82C577B4) is refreshed from
// the property by the game. Nothing is written to the game's memory; the gold and the levels a
// Skylander gets are saved to its figure by the game as usual.

#include <algorithm>
#include <cstdint>

#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "portal_rex/achievements_ui.h"

namespace {

void Scale(PPCRegister& amount, int k, uint32_t cap, const char* what) {
  const uint64_t before = amount.u32;
  if (k <= 1 || before == 0) return;
  // The game caps the total itself; more than the cap can't make a difference.
  amount.u64 = std::min<uint64_t>(before * uint64_t(k), cap);
  static bool gold_logged = false, xp_logged = false;
  bool& logged = what[0] == 'g' ? gold_logged : xp_logged;
  if (logged) return;
  logged = true;
  REXLOG_INFO("Extras: the {} multiplier scaled an award ({} -> {})", what, before, amount.u64);
}

}  // namespace

// sub_8275A440 at 0x8275A454 (mr r31,r4): r4 the gold a pickup adds.
void GoldGainHook(PPCRegister& r4) { Scale(r4, skylanders::CheatGoldMultiplier(), 65000, "gold"); }

// sub_8275A550 at 0x8275A564 (mr r31,r4): r4 the XP an award adds.
void XpGainHook(PPCRegister& r4) { Scale(r4, skylanders::CheatXpMultiplier(), 33000, "XP"); }
