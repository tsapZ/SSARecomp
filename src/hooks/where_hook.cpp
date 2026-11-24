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
#include <span>
#include <vector>
#include <algorithm>
#include <string>
#include <unordered_map>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/context.h>

#include "dev/where.h"
#include "hooks/extras.h"
#include "hooks/free_camera_hook.h"
#include "hooks/where_hook.h"
#include "portal/save_medals.h"
#include "portal_rex/soul_gems.h"

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

// The Soul Gem movies (skylanders-portal soul_gems.h): movies\<name>.bik, one per Skylander
// (chopchop.bik, legendarybash.bik). Only the catalog's names count, so the logos, the legal
// notes and the story movies (sl_sc*, sky_800_moment_*) are left out.
std::string SoulGemOf(std::string_view path) {
  if (!skylanders::InFolder(path, "movies")) return {};
  const std::string_view stem = skylanders::FileStem(path, ".bik");
  return stem.empty() ? std::string() : skylanders::SkylanderNamed(stem);
}

// Which Soul Gems lie in which story level (toy ids; read from the levels' mission.str with the dev
// kit's gem_ids.py, 2026-11-16): 28 gems in 25 levels, three levels with two.
struct GemLevel {
  const char* level;
  int toys[2];
};
constexpr GemLevel kGemLevels[] = {
    {"Level_000", {11, 32}}, {"Level_001", {7, -1}},  {"Level_002", {19, 29}}, {"Level_003", {15, -1}},
    {"Level_008", {5, -1}},  {"Level_009", {25, -1}}, {"Level_010", {18, -1}}, {"Level_014", {13, -1}},
    {"Level_017", {26, -1}}, {"Level_018", {1, -1}},  {"Level_019", {3, -1}},  {"Level_021", {27, -1}},
    {"Level_023", {16, -1}}, {"Level_024", {9, -1}},  {"Level_025", {17, -1}}, {"Level_026", {2, -1}},
    {"Level_027", {30, -1}}, {"Level_032", {0, -1}},  {"Level_034", {21, -1}}, {"Level_036", {31, -1}},
    {"Level_037", {6, 10}},  {"Level_038", {14, -1}}, {"Level_039", {23, -1}}, {"Level_040", {24, -1}},
    {"Level_046", {8, -1}},
};

std::filesystem::path g_user_data;  // set before the game runs

// The slot saves: <user data>\<profile>\415608AB\00000001\<slot>\SLOT.DAT.
std::vector<std::filesystem::path> SlotFiles() {
  std::vector<std::filesystem::path> out;
  std::error_code ec;
  for (const auto& profile : std::filesystem::directory_iterator(g_user_data, ec)) {
    const auto slots = profile.path() / "415608AB" / "00000001";
    std::error_code ec2;
    for (const auto& slot : std::filesystem::directory_iterator(slots, ec2)) {
      const auto file = slot.path() / "SLOT.DAT";
      if (std::filesystem::is_regular_file(file, ec2)) out.push_back(file);
    }
  }
  return out;
}

// The Skylanders whose Soul Gem the newer save has and the older hasn't (portal/save_medals.h): a level's
// "champion upgrade medal" going up. A level with two gems and one new medal: the first of its two that
// wasn't found yet in this session (the save doesn't say which; the run counts one unlock either way).
std::vector<std::string> GemFinds(std::span<const uint8_t> before, std::span<const uint8_t> after) {
  using namespace skylanders::portal;
  std::vector<std::string> out;
  const std::vector<std::string> seen = skylanders::SoulGemsSeen();
  for (const GemLevel& l : kGemLevels) {
    const uint32_t key = NameCrc32(l.level);
    const uint32_t was = GroupItem(before, key, kSsaSoulGemItem), now = GroupItem(after, key, kSsaSoulGemItem);
    if (now <= was) continue;
    uint32_t left = now - was;
    for (const int toy : l.toys) {
      if (toy < 0 || left == 0) continue;
      std::string name = skylanders::SkylanderWithId(static_cast<uint16_t>(toy));
      if (name.empty()) continue;
      if (l.toys[1] >= 0 && now - was < 2 && std::find(seen.begin(), seen.end(), name) != seen.end()) continue;
      REXLOG_INFO("Soul Gem in the save: {} ({})", name, l.level);
      out.push_back(std::move(name));
      --left;
    }
  }
  return out;
}

}  // namespace

namespace ssarecomp {

void InstallWhere(const std::filesystem::path& game_data_root, const std::filesystem::path& user_data_root) {
  std::ifstream in(game_data_root / "gamedata.lxb", std::ios::binary);
  const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  static const std::regex name(R"((?:Level_(?:\d{3}b?|Hub)|Challenge_Level_\d{3}|PvP_Level_\d{3}))");
  for (auto it = std::sregex_iterator(data.begin(), data.end(), name); it != std::sregex_iterator(); ++it) {
    g_names.emplace(Crc32(it->str()), it->str());
  }
  g_names.emplace(Crc32("FrontEnd"), "FrontEnd");
  REXLOG_INFO("Level names for the Where lines: {}", g_names.size());
  skylanders::WatchSoulGems(&SoulGemOf);
  g_user_data = user_data_root;
  skylanders::WatchSoulGemSaves(&SlotFiles, &GemFinds);
}

bool InBattleLevel(uint8_t* base) {
  const auto it = g_names.find(Load32(base, kCurrentLevel));
  return it != g_names.end() && it->second.starts_with("PvP_");
}

bool InMainMenu(uint8_t* base) {
  static const uint32_t kFrontEnd = Crc32("FrontEnd");
  return Load32(base, kCurrentLevel) == kFrontEnd;
}

}  // namespace ssarecomp

extern "C" void __imp__sub_8280A008(PPCContext& __restrict ctx, uint8_t* base);

REX_HOOK_RAW(sub_8280A008) {
  // The free camera: the game camera's own transform back before its camera logic runs
  // (free_camera_hook.cpp).
  ssarecomp::FreeCameraFrameStart(base);
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
    // The Extras page's cheats, at the same safe point of the frame (extras.cpp).
    ssarecomp::ApplyExtras(base);
  }
#if defined(SKYLANDERS_DEVKIT)
  // The developer kit (a private test build only) does its per-frame work here as well.
  DevKitFrame(ctx, base);
#endif
  __imp__sub_8280A008(ctx, base);
}
