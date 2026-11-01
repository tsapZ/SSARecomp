// The Extras page's cheats.
//
// No health loss: each player's Skylander gets its health back to full at the start of every frame.
// The fields are the ones the game's own Lua function player_health(player) reads (its native
// sub_822444E8 -> sub_82938A08, found 2026-11-01 through the script function list at 0x82AD6580:
// name, alias, function): the current health as a float at +688 of the player's health component,
// the full health as the product of the floats at +484 (the level's health) and +476 (a
// multiplier). The way there starts at the player object of the player list (where_hook.cpp:
// 0x82AB9C94, circular, node +4 the next, node +8 the object, node +12 its count block with the use
// count at +4): the player object's +12 points to its Skylander (sub_828210D8), whose component map
// (buckets at +156, their count at +160; a bucket is 8 bytes, its nodes chained at +4 back to the
// bucket, node +8 the type, +12 the entry) holds the health component under the type 0x118B3297,
// the entry's +4 (sub_822047F8 -> sub_828759B0). The player object's own +688 is something else:
// checked with the developer kit, player_health(0) said 466.7 of 466.7 where those fields of the
// player object gave 4 of 300.

#include "hooks/extras.h"

#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>

REXCVAR_DEFINE_BOOL(cheat_no_health_loss, false, "Extras",
                    "Skylanders don't lose health (achievements don't unlock while it's on)");

namespace {

constexpr uint32_t kPlayers = 0x82AB9C94;
constexpr uint32_t kPlayerSkylander = 12;
constexpr uint32_t kComponentBuckets = 156;
constexpr uint32_t kComponentBucketCount = 160;
constexpr uint32_t kHealthComponentType = 0x118B3297;
constexpr uint32_t kHealth = 688;
constexpr uint32_t kLevelHealth = 484;
constexpr uint32_t kHealthFactor = 476;

uint32_t Load32(uint8_t* base, uint32_t address) { return rex::memory::load_and_swap<uint32_t>(base + address); }
float LoadFloat(uint8_t* base, uint32_t address) {
  const uint32_t bits = Load32(base, address);
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}
void StoreFloat(uint8_t* base, uint32_t address, float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, sizeof(bits));
  rex::memory::store_and_swap<uint32_t>(base + address, bits);
}
bool HeapPointer(uint32_t v) { return v >= 0x40000000 && v < 0x80000000; }

// The health component of a player object's Skylander (see the top), or 0.
uint32_t HealthComponent(uint8_t* base, uint32_t object) {
  const uint32_t skylander = Load32(base, object + kPlayerSkylander);
  if (!HeapPointer(skylander)) return 0;
  const uint32_t buckets = Load32(base, skylander + kComponentBuckets);
  const uint32_t count = Load32(base, skylander + kComponentBucketCount);
  if (!HeapPointer(buckets) || count == 0 || count > 4096) return 0;
  const uint32_t bucket = buckets + (kHealthComponentType % count) * 8;
  uint32_t node = Load32(base, bucket + 4);
  for (int i = 0; i < 64 && node != bucket && HeapPointer(node); ++i, node = Load32(base, node + 4)) {
    if (Load32(base, node + 8) != kHealthComponentType) continue;
    const uint32_t entry = Load32(base, node + 12);
    if (!HeapPointer(entry)) return 0;
    const uint32_t component = Load32(base, entry + 4);
    return HeapPointer(component) ? component : 0;
  }
  return 0;
}

void KeepHealthFull(uint8_t* base) {
  static bool logged = false;
  uint32_t node = Load32(base, kPlayers + 4);
  for (int i = 0; i < 4 && node != kPlayers && HeapPointer(node); ++i, node = Load32(base, node + 4)) {
    const uint32_t object = Load32(base, node + 8), counts = Load32(base, node + 12);
    if (!HeapPointer(object) || !HeapPointer(counts) || Load32(base, counts + 4) == 0) continue;
    const uint32_t health = HealthComponent(base, object);
    if (!health) continue;
    const float full = LoadFloat(base, health + kLevelHealth) * LoadFloat(base, health + kHealthFactor);
    const float now = LoadFloat(base, health + kHealth);
    // Only plausible values: a Skylander that isn't set up yet has zeros there, and for a frame
    // while a level loads the current value was far above the full one (4865.7 of 300.0).
    if (!(full > 0.0f && full < 100000.0f) || !(now >= 0.0f && now < full)) continue;
    StoreFloat(base, health + kHealth, full);
    if (!logged) {
      REXLOG_INFO("Extras: no health loss put a Skylander's health back to full ({:.1f} -> {:.1f})", now, full);
      logged = true;
    }
  }
}

}  // namespace

namespace ssarecomp {

void ApplyExtras(uint8_t* base) {
  if (REXCVAR_GET(cheat_no_health_loss)) KeepHealthFull(base);
}

}  // namespace ssarecomp
