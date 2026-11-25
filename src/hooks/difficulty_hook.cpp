// Difficulty levels (game/difficulty.h): which save is played, and the damage, food and level-up heal.
//
// The save: the game opens and creates its saves through one function, the only call of
// XamContentCreateEx (0x822DD4C4, bl 0x829E5C34, in the content wrapper sub_822DD460). At that call
// r5 is the XCONTENT_DATA (its file name, the package's folder "Skylanders - Slot 1", at +264, 42
// characters) and r6 the flags, whose low four bits say how: 1 create new, 2 create always, 3 open
// existing, 4 open always, 5 truncate. A new game writes its slot save (2, over an old save in that
// slot too) while the main menu (FrontEnd) is loaded, before the opening movie; in play the game
// saves only inside the levels (seen 2026-11-24: Slot 4 written at the main menu, then again as
// Level_027 loaded). So a slot save written from the main menu is a new game, as is one created that
// didn't exist; any slot save opened or written is the one played from then on. Found 2026-11-24 from
// the import table (0x829E5C34 is XamContentCreateEx's thunk, called from this one place only).
//
// Health: every health component (the one extras.cpp reads: current health at +688, full health
// +484 * +476) has its health set by sub_82826C20 (clamps to 0 .. full, stores +688, works out the
// bar's part). Its 38 callers (2026-11-24) are full refills (spawns, respawns, checkpoints), heals,
// the loading of a figure's health, and one subtraction: sub_828279A0, the component's take-damage
// (r3 the component, f1 the damage, r5 the hit). Every hit of the damage rules (SpyroDamageRule,
// SpyroEnemyDamageRule) and every other hurt ends there; in play it reaches
//
//   0x82827D80  lfs f0,688(r29); fsubs f1,f0,f29; bl sub_82826C20   (r29 the component, f29 the damage)
//
// and below 1 health (+148 of the constants) the component is set to 0 and dies. (The way before it,
// through +865 / +868, is Battle Mode's arena health.) So the damage is scaled there, once for all:
// a player's Skylander takes it times DamageTakenScale, anything else (enemies, bosses, minions)
// divided by EnemyHealthScale, the same as its health times that, the bar included. The game's
// own direct settings (refills, scripted deaths at 0) are left alone.
//
//   0x8273B44C  bl sub_82826C20 in the pickup handler sub_8273B3D8: food. f0 the current health,
//               f1 that plus the food's value (+452 of the pickup); the gain is scaled by FoodHealScale.
//   0x8275DDD4  bl sub_82826C20 in sub_8275DBC8, which sets the Skylander's XP (total_exp, 0xAF511216)
//               and its level and then refills the health: the level-up. f1 the full health, r3 the
//               component; the refill becomes LevelUpHealPart of the full health on top of what is
//               left, only in play with the Skylander hurt (0 < health < full), so a Skylander coming
//               into a level still starts full.
//
// Battle Mode arenas (PvP_Level_*), where Skylanders fight each other, stay as they are, and so does
// Adventurer. Nothing is written to the game's memory; the registers are what the game goes on with.

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
#include "hooks/extras.h"
#include "hooks/where_hook.h"

namespace {

constexpr uint32_t kContentFileName = 264;  // XCONTENT_DATA: device id, type, display name (128 wchar)
constexpr uint32_t kContentFileNameSize = 42;
constexpr uint32_t kHealth = 688;

uint8_t* Base() { return rex::system::kernel_state()->memory()->virtual_membase(); }

float LoadFloat(uint8_t* base, uint32_t address) {
  const uint32_t bits = rex::memory::load_and_swap<uint32_t>(base + address);
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}

// False in Battle Mode: the difficulty stays out of the arenas.
bool Applies(uint8_t* base) { return !ssarecomp::InBattleLevel(base); }

void LogFirst(std::atomic<int>& logged, const char* what, double before, double after, float m) {
  if (logged.fetch_add(1) >= 3) return;
  REXLOG_INFO("Difficulty: x{:.1f} {} ({:.1f} -> {:.1f})", m, what, before, after);
}

}  // namespace

// 0x822DD4C4 (bl XamContentCreateEx): r5 the XCONTENT_DATA, r6 the flags.
void SavePackageHook(PPCRegister& r5, PPCRegister& r6) {
  if (r5.u32 == 0) return;
  uint8_t* base = Base();
  char name[kContentFileNameSize + 1] = {};
  std::memcpy(name, base + r5.u32 + kContentFileName, kContentFileNameSize);
  ssarecomp::OnSavePackage(name, r6.u32 & 0xF, ssarecomp::InMainMenu(base));
}

// 0x82827D80 in sub_828279A0 (take damage): r29 the health component, f29 the damage.
void TakeDamageHook(PPCRegister& r29, PPCRegister& f29) {
  const float damage = ssarecomp::DamageTakenScale(), health = ssarecomp::EnemyHealthScale();
  if ((damage == 1.0f && health == 1.0f) || !(f29.f64 > 0.0)) return;
  uint8_t* base = Base();
  if (!Applies(base)) return;
  const double before = f29.f64;
  if (ssarecomp::IsPlayerHealth(base, r29.u32)) {
    if (!(damage > 0.0f) || damage == 1.0f) return;
    f29.f64 = before * damage;
    static std::atomic<int> logged{0};
    LogFirst(logged, "damage to a Skylander", before, f29.f64, damage);
  } else {
    if (!(health > 0.0f) || health == 1.0f) return;
    f29.f64 = before / health;
    static std::atomic<int> logged{0};
    LogFirst(logged, "enemy health (damage to it)", before, f29.f64, health);
  }
}

// 0x8273B44C in the pickup handler: f0 the health now, f1 the health with the food.
void FoodHealHook(PPCRegister& f0, PPCRegister& f1) {
  const float scale = ssarecomp::FoodHealScale();
  if (scale == 1.0f || !(f1.f64 > f0.f64) || !Applies(Base())) return;
  const double before = f1.f64 - f0.f64;
  f1.f64 = f0.f64 + before * scale;
  static std::atomic<int> logged{0};
  LogFirst(logged, "food heal", before, before * scale, scale);
}

// 0x8275DDD4 in the level-up: r3 the health component, f1 its full health.
void LevelUpHealHook(PPCRegister& r3, PPCRegister& f1) {
  const float part = ssarecomp::LevelUpHealPart();
  if (part >= 1.0f || r3.u32 == 0) return;
  uint8_t* base = Base();
  if (!ssarecomp::Playing(base) || ssarecomp::InMainMenu(base) || !Applies(base)) return;
  const double full = f1.f64, now = LoadFloat(base, r3.u32 + kHealth);
  if (!(now > 0.0 && now < full && full < 100000.0)) return;
  f1.f64 = std::min(full, now + full * part);
  static std::atomic<int> logged{0};
  LogFirst(logged, "level-up heal", full - now, f1.f64 - now, part);
}
