// Difficulty levels (game/difficulty.h): which save is played, and the enemies' hits and toughness.
//
// The save: the game opens and creates its saves through one function, the only call of
// XamContentCreateEx (0x822DD4C4, bl 0x829E5C34, in the content wrapper sub_822DD460). At that call
// r5 is the XCONTENT_DATA (its file name, the package's folder "Skylanders - Slot 1", at +264, 42
// characters) and r6 the flags, whose low four bits say how: 1 create new, 2 create always, 3 open
// existing, 4 open always, 5 truncate. A new game writes its slot save (2, over an old save in that
// slot too) while the main menu (FrontEnd) is loaded, before the opening movie; in play the game
// saves only inside the levels (seen 2026-11-24: Slot 4 written at the main menu, then again as
// Level_027 loaded). So a slot save written from the main menu is a new game, as is one created that
// didn't exist; any slot save opened or written is the one played from then on. Found 2026-11-24 from the
// import table (0x829E5C34 is XamContentCreateEx's thunk, called from this one place only).
//
// The hits: Spyro's Adventure works out every hit in a damage rule (attribute_cheats_hook.cpp has
// how they were found). Both end the same way: the hit's damage in f31 and its element bonus in f30
// (players) / f0 (enemies) are stored to the hit's result, and their sum is the damage done.
//
//   SpyroEnemyDamageRule, sub_82761AF0, an enemy hits: 0x82761CC8 (lwz r11,396(r1); then
//   stfs f31 / stfs f0 / stfs f13 = f0 + f31). A blocked hit arrives with f31 = f0 = 0.
//   SpyroDamageRule, sub_82761F80, a Skylander hits: 0x827621D4 (lwz r11,268(r31); then
//   fadds f0,f30,f31, stfs f31 / stfs f30 / stfs f0). A blocked hit arrives with f31 = f30 = 0.
//
// An enemy's hit is multiplied by the difficulty's multiplier, a Skylander's divided by it, both
// rounded to whole points as the game's own are (a hit that did damage does at least 1). Battle Mode
// arenas (PvP_Level_*), where Skylanders fight each other, stay as they are. Nothing is written to the
// game's memory; the registers are only stored to the hit's result after this.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/memory/utils.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

#include "game/difficulty.h"
#include "hooks/where_hook.h"

namespace {

constexpr uint32_t kContentFileName = 264;  // XCONTENT_DATA: device id, type, display name (128 wchar)
constexpr uint32_t kContentFileNameSize = 42;

uint8_t* Base() { return rex::system::kernel_state()->memory()->virtual_membase(); }

// The damage scaled, rounded to whole points; a hit that did damage keeps at least 1.
double Scaled(double damage, double factor) {
  if (!(damage > 0.0)) return damage;
  return std::max(1.0, std::round(damage * factor));
}

// 0 for Battle Mode and Adventurer, else the multiplier.
float ActiveMultiplier() {
  const float m = ssarecomp::EnemyMultiplier();
  if (m == 1.0f || !(m > 0.0f)) return 0.0f;
  if (ssarecomp::InBattleLevel(Base())) return 0.0f;
  return m;
}

void LogOnce(std::atomic<bool>& logged, const char* who, double before, double after, float m) {
  if (logged.exchange(true)) return;
  REXLOG_INFO("Difficulty: x{:.1f} scaled {} hit ({:.0f} -> {:.0f})", m, who, before, after);
}

}  // namespace

// 0x822DD4C4 (bl XamContentCreateEx): r5 the XCONTENT_DATA, r6 the flags.
void SavePackageHook(PPCRegister& r5, PPCRegister& r6) {
  if (r5.u32 == 0) return;
  char name[kContentFileNameSize + 1] = {};
  std::memcpy(name, Base() + r5.u32 + kContentFileName, kContentFileNameSize);
  ssarecomp::OnSavePackage(name, r6.u32 & 0xF, ssarecomp::InMainMenu(Base()));
}

// 0x82761CC8 in SpyroEnemyDamageRule: f31 an enemy's hit, f0 its element bonus.
void EnemyHitHook(PPCRegister& f0, PPCRegister& f31) {
  const float m = ActiveMultiplier();
  if (m == 0.0f) return;
  static std::atomic<bool> logged{false};
  const double before = f31.f64 + f0.f64;
  f31.f64 = Scaled(f31.f64, m);
  f0.f64 = Scaled(f0.f64, m);
  if (before > 0.0) LogOnce(logged, "an enemy's", before, f31.f64 + f0.f64, m);
}

// 0x827621D4 in SpyroDamageRule: f31 a Skylander's hit, f30 its element bonus.
void SkylanderHitHook(PPCRegister& f30, PPCRegister& f31) {
  const float m = ActiveMultiplier();
  if (m == 0.0f) return;
  static std::atomic<bool> logged{false};
  const double before = f31.f64 + f30.f64;
  f31.f64 = Scaled(f31.f64, 1.0 / m);
  f30.f64 = Scaled(f30.f64, 1.0 / m);
  if (before > 0.0) LogOnce(logged, "a Skylander's", before, f31.f64 + f30.f64, m);
}
