// Where the player is, for problem reports (skylanders-portal dev/where.h): read at the start of
// the game's per-frame state update sub_8280A008, on its main thread, where no object is half
// changed.
//
// The level manager, a static object at 0x82CA3700, keeps the current level's hash at +12: the
// CRC-32 (zlib's) of its name as the level table on the disc (rom\gamedata.lxb) spells it,
// "Level_014" -> 9D0BA2B5; the main menu is "FrontEnd". The game's state at 0x82AA602C is 2 while
// it plays (1 once a level is asked for, 0 while one loads). Player 1 is the first node of the
// player list at 0x82AB9C94 (circular, node +4 the next; node +8 a weak pointer: the object, then
// its count block, whose +4 is the use count); the object's +0x22C is its position (x, y, z, z up),
// the one the game's own Lua player_obj(0).position() returns. Found 2026-10-28 with a search for
// the Lua position's three floats behind that list, before and after a teleport.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <unordered_map>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/context.h>

#include "dev/where.h"
#include "hooks/where_hook.h"

namespace {

constexpr uint32_t kCurrentLevel = 0x82CA3700 + 12;
constexpr uint32_t kGameState = 0x82AA602C;
constexpr uint32_t kPlayers = 0x82AB9C94;
constexpr uint32_t kPosition = 0x22C;

std::unordered_map<uint32_t, std::string> g_names;  // level hash -> name, set before the game runs

uint32_t Load32(uint8_t* base, uint32_t address) { return rex::memory::load_and_swap<uint32_t>(base + address); }
float LoadFloat(uint8_t* base, uint32_t address) {
  const uint32_t bits = Load32(base, address);
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}
bool HeapPointer(uint32_t v) { return v >= 0x40000000 && v < 0x80000000; }

uint32_t Crc32(const std::string& s) {
  uint32_t crc = 0xFFFFFFFFu;
  for (unsigned char c : s) {
    crc ^= c;
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

// Player 1's object, or 0 when the level has none (yet).
uint32_t PlayerOne(uint8_t* base) {
  const uint32_t node = Load32(base, kPlayers + 4);
  if (node == kPlayers || !HeapPointer(node)) return 0;
  const uint32_t object = Load32(base, node + 8), counts = Load32(base, node + 12);
  if (!HeapPointer(object) || !HeapPointer(counts) || Load32(base, counts + 4) == 0) return 0;
  return object;
}

}  // namespace

namespace ssarecomp {

void InstallWhere(const std::filesystem::path& game_data_root) {
  std::ifstream in(game_data_root / "gamedata.lxb", std::ios::binary);
  const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  static const std::regex name(R"((?:Level_(?:\d{3}b?|Hub)|Challenge_Level_\d{3}|PvP_Level_\d{3}))");
  for (auto it = std::sregex_iterator(data.begin(), data.end(), name); it != std::sregex_iterator(); ++it) {
    g_names.emplace(Crc32(it->str()), it->str());
  }
  g_names.emplace(Crc32("FrontEnd"), "FrontEnd");
  REXLOG_INFO("Level names for the Where lines: {}", g_names.size());
}

}  // namespace ssarecomp

extern "C" void __imp__sub_8280A008(PPCContext& __restrict ctx, uint8_t* base);

REX_HOOK_RAW(sub_8280A008) {
  if (Load32(base, kGameState) == 2) {
    const uint32_t hash = Load32(base, kCurrentLevel);
    static std::string unknown;
    const auto it = g_names.find(hash);
    if (it == g_names.end()) unknown = fmt::format("level {:08X}", hash);
    const std::string& level = it != g_names.end() ? it->second : unknown;
    float x = 0, y = 0, z = 0;
    if (const uint32_t player = PlayerOne(base)) {
      x = LoadFloat(base, player + kPosition);
      y = LoadFloat(base, player + kPosition + 4);
      z = LoadFloat(base, player + kPosition + 8);
    }
    // A Skylander that is not in the level yet stands at 0 0 0 for a few frames.
    skylanders::dev::ReportPlace(level, x != 0 || y != 0 || z != 0, x, y, z);
  }
#if defined(SKYLANDERS_DEVKIT)
  // The developer kit (a private test build only) does its per-frame work here as well.
  DevKitFrame(ctx, base);
#endif
  __imp__sub_8280A008(ctx, base);
}
