// Gold and XP multipliers, as cheats (SkylandersCoreOfLight #48; the other five games have them under
// the same setting names, the rows from skylanders-portal), and the difficulty's reward bonus.
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
//
// The difficulty (game/difficulty.h) gives more of both on Hard (x1.25) and Nightmare (x1.5), as
// Imaginators does (its _xpScalar* and _moneyScalar*, the "+50% Rewards" of its Nightmare); outside
// Battle Mode, and on top of a cheat's multiplier. It is no cheat: achievements still unlock.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

#include "game/difficulty.h"
#include "hooks/where_hook.h"
#include "portal_rex/achievements_ui.h"

namespace {

// The difficulty's reward bonus, 1 in Battle Mode.
double Reward() {
  const double r = ssarecomp::RewardScale();
  if (r == 1.0 || !(r > 0.0)) return 1.0;
  if (ssarecomp::InBattleLevel(rex::system::kernel_state()->memory()->virtual_membase())) return 1.0;
  return r;
}

void Scale(PPCRegister& amount, int k, uint32_t cap, const char* what) {
  const uint64_t before = amount.u32;
  const double factor = double(std::max(k, 1)) * Reward();
  if (factor == 1.0 || before == 0) return;
  // The game caps the total itself; more than the cap can't make a difference. A bonus that rounds
  // away on a small award still gives at least one more.
  uint64_t after = uint64_t(std::llround(double(before) * factor));
  if (factor > 1.0 && after <= before) after = before + 1;
  amount.u64 = std::min<uint64_t>(after, cap);
  static bool gold_logged = false, xp_logged = false;
  bool& logged = what[0] == 'g' ? gold_logged : xp_logged;
  if (logged) return;
  logged = true;
  REXLOG_INFO("Extras: the {} multiplier scaled an award by x{:.2f} ({} -> {})", what, factor, before,
              amount.u64);
}

}  // namespace

// sub_8275A440 at 0x8275A454 (mr r31,r4): r4 the gold a pickup adds.
void GoldGainHook(PPCRegister& r4) { Scale(r4, skylanders::CheatGoldMultiplier(), 65000, "gold"); }

// sub_8275A550 at 0x8275A564 (mr r31,r4): r4 the XP an award adds.
void XpGainHook(PPCRegister& r4) { Scale(r4, skylanders::CheatXpMultiplier(), 33000, "XP"); }
