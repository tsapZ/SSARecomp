// The game's own pictures for the portal menu, from the disc's uigameimage.str: the HUD
// portraits, the element symbols and the pictures of some magic items.
//
// The file is "strm", its size, two words of 1 and the record count (737), then one zlib stream.
// Unpacked it holds one record per UI picture, each an "APKF" package with a TEX, an IMG and a
// PHYS chunk (tags stored reversed: "XET", "GMI", "SYHP"):
//   - the picture's name, NUL-padded, right before "GMI" (toy_eruptor_hud, ico_element_fire_l, ...)
//   - a Xenos texture fetch constant after eight bytes of ff ff 00 00 ff ff 00 00: tiled flag in
//     dword 0 bit 31, format in dword 1 bits 0-5 (0x14 = DXT5), size in dword 2
//   - in PHYS, the texel data's offset from "APKF" (+8, 0x1000) and its size (+16, 0x4000)
// Every record read so far is a 128x128 (element symbols 64x64) tiled DXT5 texture with 16-bit
// words big-endian. Worked out from the file itself (2026-10-20); the decoded pictures matched the
// game's HUD portraits for all 37 characters.
//
// The path upgrades' pictures are in each character's own archive,
// models/characters/minionsmonsters/<name>.str (the same format, about 10 MB packed, the model,
// its textures and two UI pictures): "<name>_upgradesa" and "<name>_upgradesb", 512x128 DXT5,
// each three pictures side by side (columns 0-170, 171-340, 341-511), the first path's three
// upgrades and the second's (Trigger Happy's: "Happiness is a Golden Gun", "Bouncing Bullets",
// "Golden Yamato Blast" / "Just Throwing Money Away", "Coinsplosion", "Heads or Tails"). Found
// 2026-10-21 after the owner saw them on the game's Skylander screen; kPaths has the names (some
// differ from the archive's: firearcher, crustybuckler, troll, lifedragon, ent; Stealth Elf's are
// "...upgradespook" / "...upgradesforrest" for "Pook Blade Saint" / "Forest Ninja"). An archive
// is unpacked the first time its character's details are shown, on a thread of its own.
// The same archive has every other upgrade's picture on its own, named with the strips' prefix
// ("eruptor_upgradesa" -> "eruptor_"): "_primary" and "_secondary" the starting attacks, "_up1" ...
// "_up4" the basic upgrades, "_up5a" ... "_up7b" the paths', "_up8" the Soul Gem's (listed
// 2026-10-25 in eruptor.str after the owner's SSARecomp #39: the basic four showed only arrows).
// "pose:<toy id>:<suffix>" is one of them; the archive is read with the strips.
//
// The controller picture for the settings menu's controls page: ui/eng/startmenu.str has
// "c:\spyro\assets\ui\textures\startmenu\360_controller.dds", 256x256 DXT5, a clean drawing of
// the white pad, upright (found 2026-10-22 by listing every strm archive's picture names; the
// pause menu's "controller_360line" is only the callout lines drawn over it). It is cut to the
// pad's box (kControllerCrop); the buttons' places were measured on the picture.

#include "ui_images.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <rex/logging.h>

#include "hats.h"
#include "portal_rex/portal_menu.h"
#include "alchemy/xenos_dxt.h"

// stb_image's zlib decoder (vendored with the SDK), compiled here with every function static, as
// SwapForceRecomp does for its archives.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include <stb_image.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace ssarecomp {

// The games' shared Alchemy readers (skylanders-portal src/alchemy).
namespace alchemy = skylanders::alchemy;
namespace {

struct Entry {
  int width = 0;
  int height = 0;
  alchemy::Dxt kind = alchemy::Dxt::kDxt5;
  bool tiled = true;
  std::vector<uint8_t> texels;
};

std::mutex g_mutex;
std::map<std::string, Entry> g_entries;  // only the pictures the menu can use
std::filesystem::path g_characters_dir;  // models/characters/minionsmonsters
std::map<uint16_t, bool> g_paths_requested;  // by toy id: its archive is being read or was
std::map<std::string, std::shared_ptr<const skylanders::FigureImage>> g_decoded;
std::atomic<bool> g_loaded{false};

uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// The game's internal character name for a figure's HUD portrait (toy_<name>_hud), by toy id.
// Checked by eye against every decoded portrait.
const char* PortraitName(uint16_t id) {
  static const char* const kBase[33] = {
      "airdragon",     // 0 Whirlwind
      "griffin",       // 1 Sonic Boom
      "skyturtle",     // 2 Warnado
      "stormgiant",    // 3 Lightning Rod
      "rockdragon",    // 4 Bash
      "landshark",     // 5 Terrafin
      "dinorang",      // 6 Dino-Rang
      "gemgolem",      // 7 Prism Break
      "phoenixdragon", // 8 Sunburn
      "eruptor",       // 9 Eruptor
      "flameknight",   // 10 Ignitor
      "fireelf",       // 11 Flameslinger
      "waterdragon",   // 12 Zap
      "crustbuckler",  // 13 Wham-Shell
      "gillgrunt",     // 14 Gill Grunt
      "yeti",          // 15 Slam Bam
      "spyrojr",       // 16 Spyro
      "skullorc",      // 17 Voodood
      "tikiwizard",    // 18 Double Trouble
      "goldencheat",   // 19 Trigger Happy
      "metaldragon",   // 20 Drobot
      "drillbot",      // 21 Drill Sergeant
      "bombtroll",     // 22 Boomer
      "forcefieldgrub",// 23 Wrecking Ball
      "plantdragon",   // 24 Camo
      "bambazooker",   // 25 Zook
      "stealthelf",    // 26 Stealth Elf
      "stumpsmashent", // 27 Stump Smash
      "darkspyro",     // 28 Dark Spyro
      "shadowmaid",    // 29 Hex
      "pandoranguard", // 30 Chop Chop
      "ghosteater",    // 31 Ghost Roaster
      "cynder",        // 32 Cynder
  };
  switch (id) {
    case 404: return "legendaryrockdragon";     // Legendary Bash
    case 416: return "legendaryspyro";          // Legendary Spyro
    case 419: return "legendarygoldencheat";    // Legendary Trigger Happy
    case 430: return "legendarypandoranguard";  // Legendary Chop Chop
    default: return id < 33 ? kBase[id] : nullptr;
  }
}

// Magic items with a picture of their own in the archive (its Battle Mode power-up pictures),
// by toy id; checked by eye. The game shows the others as 3D models (its collection screen; the
// UI archives have no picture of an hourglass or Sparx, 2026-10-21), so Hidden Treasure gets
// the treasure chest of the archive's "treasure_hunter" (a chest with a gem, as the toy), Time
// Twister and Sparx the game's own symbol for a magic item, and the adventure packs (location
// pieces) the game's symbol for the location element.
const char* ItemPictureName(uint16_t id) {
  switch (id) {
    case 200: return "anvil_lg";              // Anvil Rain: the Anvil Hat's picture, the same anvil
    case 201: return "treasure_hunter";       // Hidden Treasure
    case 202: return "potion";                // Healing Elixir
    case 203: return "crossed_swords";        // Ghost Swords
    case 204: return "elementiconmagicitem";  // Time Twister
    case 205: return "shield";                // Sky-Iron Shield
    case 206: return "speed_boots";           // Winged Boots
    case 207: return "elementiconmagicitem";  // Sparx Dragonfly
    case 300: case 301: case 302: case 303: case 304:
      return "elementiconlocation";           // the location pieces
    default: return nullptr;
  }
}

// The items above whose picture is a stand-in: the chest, the magic item and location symbols.
bool IsStandIn(uint16_t id) { return id == 201 || id == 204 || id == 207 || (id >= 300 && id <= 304); }

// The HUD's coin (the menu's gold symbol), in the HUD's own archive ui/eng/hudmenu.str (64x64).
constexpr char kGoldPicture[] = "c:\\spyro\\assets\\ui\\textures\\hud\\gold.dds";

// The controls page's pad (see the top), in ui/eng/startmenu.str, and the part of it with the pad.
constexpr char kControllerPicture[] = "c:\\spyro\\assets\\ui\\textures\\startmenu\\360_controller.dds";
constexpr char kControllerArtKey[] = "controller:xbox360";
constexpr int kControllerCropX = 14, kControllerCropY = 46, kControllerCropW = 230, kControllerCropH = 164;

// A character's path pictures: its archive and the two strips (first path, second path), by toy
// id (Dark Spyro and the legendary figures use their character's, upgrades.cpp).
struct Paths {
  const char* archive;
  const char* first;
  const char* second;
};
const Paths* PathsOf(uint16_t id) {
  static const Paths kPaths[33] = {
      {"airdragon", "airdragon_upgradesa", "airdragon_upgradesb"},
      {"griffin", "griffin_upgradesa", "griffin_upgradesb"},
      {"skyturtle", "skyturtle_upgradesa", "skyturtle_upgradesb"},
      {"stormgiant", "stormgiant_upgradesa", "stormgiant_upgradesb"},
      {"rockdragon", "rockdragon_upgradesa", "rockdragon_upgradesb"},
      {"landshark", "landshark_upgradesa", "landshark_upgradesb"},
      {"dinorang", "dinorang_upgradesa", "dinorang_upgradesb"},
      {"gemgolem", "gemgolem_upgradesa", "gemgolem_upgradesb"},
      {"phoenixdragon", "phoenixdragon_upgradesa", "phoenixdragon_upgradesb"},
      {"eruptor", "eruptor_upgradesa", "eruptor_upgradesb"},
      {"flameknight", "flameknight_upgradesa", "flameknight_upgradesb"},
      {"fireelf", "firearcher_upgradesa", "firearcher_upgradesb"},
      {"waterdragon2", "waterdragon2_upgradesa", "waterdragon2_upgradesb"},
      {"crustbuckler", "crustybuckler_upgradesa", "crustybuckler_upgradesb"},
      {"gillgrunt", "gillgrunt_upgradesa", "gillgrunt_upgradesb"},
      {"yeti", "yeti_upgradesa", "yeti_upgradesb"},
      {"spyrojr", "spyro_upgradesa", "spyro_upgradesb"},
      {"skullorc", "skullorc_upgradesa", "skullorc_upgradesb"},
      {"tikiwizard", "tikiwizard_upgradesa", "tikiwizard_upgradesb"},
      {"goldencheat", "goldencheat_upgradesa", "goldencheat_upgradesb"},
      {"metaldragon", "metaldragon_upgradesa", "metaldragon_upgradesb"},
      {"drillbot", "drillbot_upgradesa", "drillbot_upgradesb"},
      {"bombtroll", "troll_upgradesa", "troll_upgradesb"},
      {"forcefieldgrub", "forcefieldgrub_upgradesa", "forcefieldgrub_upgradesb"},
      {"plantdragon", "lifedragon_upgradesa", "lifedragon_upgradesb"},
      {"bambazooker", "bambazooker_upgradesa", "bambazooker_upgradesb"},
      {"stealthelf", "stealthelf_upgradespook", "stealthelf_upgradesforrest"},
      {"stumpsmashent", "ent_upgradesa", "ent_upgradesb"},
      {"darkspyro", "spyro_upgradesa", "spyro_upgradesb"},
      {"shadowmaid", "shadowmaid_upgradesa", "shadowmaid_upgradesb"},
      {"pandoranguard", "pandoranguard_upgradesa", "pandoranguard_upgradesb"},
      {"ghosteater", "ghosteater_upgradesa", "ghosteater_upgradesb"},
      {"cynder", "cynder_upgradesa", "cynder_upgradesb"},
  };
  return id < std::size(kPaths) ? &kPaths[id] : nullptr;
}

// The pictures of the hat bonus symbols (hats.cpp).
constexpr const char* kBonusPictures[] = {"elementalstrength", "armor", "criticalhit", "speed"};

bool Wanted(std::string_view name) {
  if (name.starts_with("toy_") && name.ends_with("_hud")) return true;
  if (name.starts_with("ico_element_")) return true;
  // The Soul Gems' pictures, "upgrade<toy id>" (upgrades.cpp).
  if (name.size() == 10 && name.starts_with("upgrade")) return true;
  // The hats' pictures, "<hat>_lg" (hats.cpp).
  for (uint16_t hat = 1; const char* base = HatBaseName(hat); ++hat) {
    if (name == std::string(base) + "_lg") return true;
  }
  for (const char* bonus : kBonusPictures) {
    if (name == bonus) return true;
  }
  for (uint16_t id = 200; id < 305; ++id) {
    if (const char* item = ItemPictureName(id); item && name == item) return true;
  }
  return false;
}

// Indexes one unpacked archive into `entries`; keeps the texels of the pictures `wanted` names.
template <typename WantedFn>
size_t Index(const uint8_t* d, size_t n, std::map<std::string, Entry>& entries,
             const WantedFn& wanted) {
  static const uint8_t kFetchMarker[8] = {0xFF, 0xFF, 0, 0, 0xFF, 0xFF, 0, 0};
  size_t kept = 0;
  for (size_t at = 4; at + 4 <= n; ++at) {
    if (std::memcmp(d + at, "APKF", 4) != 0) continue;
    const uint32_t size = Be32(d + at - 4);
    if (size < 0x100 || at + size > n) continue;
    const uint8_t* rec = d + at;
    const auto find = [&](const void* what, size_t len) -> std::optional<size_t> {
      for (size_t i = 0; i + len <= size; ++i) {
        if (std::memcmp(rec + i, what, len) == 0) return i;
      }
      return std::nullopt;
    };
    const auto img = find("GMI", 3);
    const auto phys = find("SYHP", 4);
    const auto fetch = find(kFetchMarker, sizeof(kFetchMarker));
    if (!img || !phys || !fetch || *fetch + 8 + 24 > size || *phys + 20 > size) continue;
    // The name: the last NUL-separated run before "GMI".
    size_t end = *img;
    while (end > 0 && rec[end - 1] == 0) --end;
    size_t begin = end;
    while (begin > 0 && rec[begin - 1] != 0) --begin;
    const std::string name(reinterpret_cast<const char*>(rec + begin), end - begin);
    if (!wanted(name)) continue;
    const uint32_t dword0 = Be32(rec + *fetch + 8), dword1 = Be32(rec + *fetch + 12),
                   dword2 = Be32(rec + *fetch + 16);
    Entry e;
    e.tiled = (dword0 >> 31) != 0;
    switch (dword1 & 0x3F) {
      case 0x12: e.kind = alchemy::Dxt::kDxt1; break;
      case 0x13: e.kind = alchemy::Dxt::kDxt3; break;
      case 0x14: e.kind = alchemy::Dxt::kDxt5; break;
      default: continue;
    }
    e.width = int(dword2 & 0x1FFF) + 1;
    e.height = int((dword2 >> 13) & 0x1FFF) + 1;
    const uint32_t texel_offset = Be32(rec + *phys + 8), texel_size = Be32(rec + *phys + 16);
    if (uint64_t(texel_offset) + texel_size > size) continue;
    e.texels.assign(rec + texel_offset, rec + texel_offset + texel_size);
    entries[name] = std::move(e);
    ++kept;
    at += size - 1;
  }
  return kept;
}

// Unpacks a strm archive and indexes the pictures `wanted` names into g_entries; the number kept,
// or nothing if the archive can't be read.
template <typename WantedFn>
std::optional<size_t> LoadArchive(const std::filesystem::path& archive, const WantedFn& wanted) {
  // In one read: istreambuf_iterator went byte by byte, 85 ms for global.str (16 MB) and 36 ms for
  // the biggest character archive against 18 and 10 ms (measured 2026-10-26), as the HD textures'
  // reads were (rexglue-sdk ce9dbb6).
  std::vector<uint8_t> file;
  if (std::ifstream in(archive, std::ios::binary | std::ios::ate); in) {
    const std::streamoff size = in.tellg();
    file.resize(size > 0 ? size_t(size) : 0);
    in.seekg(0);
    if (!file.empty() && !in.read(reinterpret_cast<char*>(file.data()), std::streamsize(file.size()))) file.clear();
  }
  if (file.size() < 22 || std::memcmp(file.data(), "strm", 4) != 0) {
    REXLOG_WARN("UI images: {} is missing or not a strm archive", archive.string());
    return std::nullopt;
  }
  int out_size = 0;
  char* out = stbi_zlib_decode_malloc_guesssize_headerflag(
      reinterpret_cast<const char*>(file.data() + 20), int(file.size() - 20),
      int(file.size() * 4), &out_size, 1);
  if (!out) {
    REXLOG_WARN("UI images: could not unpack {}", archive.string());
    return std::nullopt;
  }
  // Indexed outside the lock, so the menu isn't held up while a big archive is searched.
  std::map<std::string, Entry> found;
  const size_t kept = Index(reinterpret_cast<const uint8_t*>(out), size_t(out_size), found, wanted);
  STBI_FREE(out);
  std::lock_guard lock(g_mutex);
  for (auto& [name, entry] : found) g_entries[name] = std::move(entry);
  return kept;
}

void Load(std::filesystem::path archive) {
  const auto kept = LoadArchive(archive, Wanted);
  // The coin from the HUD's archive (the language-neutral menus are in ui/eng).
  const auto hud = archive.parent_path() / "ui" / "eng" / "hudmenu.str";
  const auto coin = LoadArchive(hud, [](std::string_view name) { return name == kGoldPicture; });
  const auto start = archive.parent_path() / "ui" / "eng" / "startmenu.str";
  const auto pad = LoadArchive(start, [](std::string_view name) { return name == kControllerPicture; });
  g_loaded.store(true);
  if (kept) REXLOG_INFO("UI images: {} pictures from {}", *kept, archive.filename().string());
  if (coin && *coin == 0) REXLOG_WARN("UI images: no coin in {}", hud.string());
  if (pad && *pad == 0) REXLOG_WARN("UI images: no controller picture in {}", start.string());
}

// The single pictures' prefix, the strips' name up to "upgrades" ("eruptor_upgradesa" -> "eruptor_",
// "stealthelf_upgradespook" -> "stealthelf_").
std::string PosePrefix(const Paths& paths) {
  const std::string_view first = paths.first;
  const size_t at = first.find("upgrades");
  return at == std::string_view::npos ? std::string() : std::string(first.substr(0, at));
}

// Reads a character's archive for its two path pictures, once (on a thread of its own).
void RequestPaths(uint16_t id) {
  const Paths* paths = PathsOf(id);
  if (!paths) return;
  {
    std::lock_guard lock(g_mutex);
    if (g_paths_requested[id]) return;
    g_paths_requested[id] = true;
  }
  std::thread([paths] {
    const std::string first = paths->first, second = paths->second;
    const std::string prefix = PosePrefix(*paths);
    const auto archive = g_characters_dir / (std::string(paths->archive) + ".str");
    const auto kept = LoadArchive(archive, [&](std::string_view name) {
      return name == first || name == second || (!prefix.empty() && name.starts_with(prefix));
    });
    if (kept) REXLOG_INFO("UI images: {} path pictures from {}", *kept, archive.filename().string());
  }).detach();
}

// The decoded picture for `name`, decoded once; nullptr while loading or if there is none.
std::shared_ptr<const skylanders::FigureImage> Picture(const std::string& name) {
  if (!g_loaded.load()) return nullptr;
  std::lock_guard lock(g_mutex);
  if (auto it = g_decoded.find(name); it != g_decoded.end()) return it->second;
  std::shared_ptr<const skylanders::FigureImage> result;
  if (auto it = g_entries.find(name); it != g_entries.end()) {
    const Entry& e = it->second;
    if (auto rgba = alchemy::DecodeXenosDxt(e.texels.data(), e.texels.size(), e.width, e.height,
                                            e.kind, e.tiled)) {
      auto image = std::make_shared<skylanders::FigureImage>();
      image->width = rgba->width;
      image->height = rgba->height;
      image->rgba = std::move(rgba->pixels);
      result = std::move(image);
    }
  }
  g_decoded[name] = result;  // also remembers "none", so a missing one isn't searched again
  return result;
}

// The controls page's pad, cut to kControllerCrop once; nullptr while loading or if there is none.
std::shared_ptr<const skylanders::FigureImage> ControllerPicture() {
  static std::mutex mutex;
  static std::shared_ptr<const skylanders::FigureImage> cropped;
  const auto full = Picture(kControllerPicture);
  if (!full || full->width < kControllerCropX + kControllerCropW ||
      full->height < kControllerCropY + kControllerCropH) {
    return nullptr;
  }
  std::lock_guard lock(mutex);
  if (!cropped) {
    auto image = std::make_shared<skylanders::FigureImage>();
    image->width = kControllerCropW;
    image->height = kControllerCropH;
    image->rgba.resize(size_t(kControllerCropW) * kControllerCropH * 4);
    for (int y = 0; y < kControllerCropH; ++y) {
      std::memcpy(&image->rgba[size_t(y) * kControllerCropW * 4],
                  &full->rgba[(size_t(kControllerCropY + y) * full->width + kControllerCropX) * 4],
                  size_t(kControllerCropW) * 4);
    }
    cropped = std::move(image);
  }
  return cropped;
}

// "path:<toy id>:<path 0/1>:<upgrade 0-2>": that picture of the character's strip, padded to a
// square (the menu's tiles are square); nullptr while the archive is still being read.
std::shared_ptr<const skylanders::FigureImage> PathPicture(const std::string& key) {
  unsigned id = 0, path = 0, upgrade = 0;
  if (std::sscanf(key.c_str(), "path:%u:%u:%u", &id, &path, &upgrade) != 3 || path > 1 ||
      upgrade > 2) {
    return nullptr;
  }
  const Paths* paths = PathsOf(uint16_t(id));
  if (!paths) return nullptr;
  {
    std::lock_guard lock(g_mutex);
    if (auto it = g_decoded.find(key); it != g_decoded.end()) return it->second;
  }
  const std::string strip_name = path == 0 ? paths->first : paths->second;
  {
    std::lock_guard lock(g_mutex);
    if (g_entries.find(strip_name) == g_entries.end()) {
      const bool requested = g_paths_requested[uint16_t(id)];
      if (requested) return nullptr;  // still being read (or not in the archive)
    }
  }
  RequestPaths(uint16_t(id));
  auto strip = Picture(strip_name);
  if (!strip) {
    // Not there yet: Picture() remembered "none", forget it so the next frame asks again.
    std::lock_guard lock(g_mutex);
    if (g_entries.find(strip_name) == g_entries.end()) g_decoded.erase(strip_name);
    return nullptr;
  }
  // Three pictures side by side, 512 / 3 wide each (0-170, 171-340, 341-511).
  const int x0 = int(upgrade) * strip->width / 3, x1 = (int(upgrade) + 1) * strip->width / 3;
  const int w = x1 - x0, h = strip->height, side = std::max(w, h);
  auto image = std::make_shared<skylanders::FigureImage>();
  image->width = side;
  image->height = side;
  image->rgba.assign(size_t(side) * side * 4, 0);
  const int dx = (side - w) / 2, dy = (side - h) / 2;
  for (int y = 0; y < h; ++y) {
    std::memcpy(&image->rgba[(size_t(y + dy) * side + dx) * 4],
                &strip->rgba[(size_t(y) * strip->width + x0) * 4], size_t(w) * 4);
  }
  std::lock_guard lock(g_mutex);
  g_decoded[key] = image;
  return image;
}

// "pose:<toy id>:<suffix>": one of the character's single upgrade pictures (see the top); nullptr
// while its archive is still being read or if it has none.
std::shared_ptr<const skylanders::FigureImage> PosePicture(const std::string& key) {
  unsigned id = 0;
  char suffix[16] = {};
  if (std::sscanf(key.c_str(), "pose:%u:%15s", &id, suffix) != 2) return nullptr;
  const Paths* paths = PathsOf(uint16_t(id));
  if (!paths) return nullptr;
  const std::string name = PosePrefix(*paths) + suffix;
  {
    std::lock_guard lock(g_mutex);
    if (g_entries.find(name) == g_entries.end() && g_paths_requested[uint16_t(id)]) return nullptr;
  }
  RequestPaths(uint16_t(id));
  auto picture = Picture(name);
  if (!picture) {
    // Not read yet: forget the "none" Picture() remembered, so a later frame asks again.
    std::lock_guard lock(g_mutex);
    if (g_entries.find(name) == g_entries.end()) g_decoded.erase(name);
  }
  return picture;
}

}  // namespace

skylanders::ControllerPicture ControllerPictureLayout() {
  // In the full picture's pixels (see the top).
  static const skylanders::ControllerSpot kSpots[] = {
      {"guide", 129, 94, 14}, {"back", 105, 94, 6},   {"start", 152, 94, 6},  {"y", 193, 75, 8},
      {"x", 176, 92, 8},      {"b", 210, 92, 8},      {"a", 193, 109, 8},     {"ls", 63, 92, 17},
      {"rs", 159, 131, 17},   {"up", 94, 119, 6},     {"down", 94, 143, 6},   {"left", 83, 131, 6},
      {"right", 106, 131, 6}, {"lb", 63, 60, 9},      {"rb", 193, 57, 9},
  };
  skylanders::ControllerPicture picture;
  picture.art_key = kControllerArtKey;
  picture.width = kControllerCropW;
  picture.height = kControllerCropH;
  for (skylanders::ControllerSpot spot : kSpots) {
    spot.x -= kControllerCropX;
    spot.y -= kControllerCropY;
    picture.spots.push_back(spot);
  }
  return picture;
}

void InstallUiImages(const std::filesystem::path& archive) {
  g_characters_dir = archive.parent_path() / "models" / "characters" / "minionsmonsters";
  std::thread(Load, archive).detach();
  skylanders::SetFigureImageProvider([](uint16_t id, uint16_t variant) {
    // The magic items and adventure packs this game has only a stand-in for (#31): a later
    // game's picture of them, if one on this PC has left it (Swap Force's collection icons have
    // them all); else the stand-in.
    if (IsStandIn(id)) {
      if (auto other = skylanders::OtherGamePicture(id, variant)) return other;
    }
    if (const char* item = ItemPictureName(id)) return Picture(item);
    const char* name = PortraitName(id);
    return name ? Picture(std::string("toy_") + name + "_hud") : nullptr;
  });
  skylanders::SetArtImageProvider([](const std::string& key) {
    // "element:magic" -> ico_element_magic_l
    constexpr std::string_view kElement = "element:";
    if (key.starts_with(kElement)) {
      return Picture("ico_element_" + key.substr(kElement.size()) + "_l");
    }
    // "hat:army" -> army_lg; "stat:armor" -> armor (hats.cpp).
    constexpr std::string_view kHat = "hat:", kStat = "stat:";
    if (key.starts_with(kHat)) return Picture(key.substr(kHat.size()) + "_lg");
    if (key.starts_with(kStat)) return Picture(key.substr(kStat.size()));
    // "upgrade:016" -> upgrade016, a Skylander's Soul Gem picture (upgrades.cpp).
    constexpr std::string_view kUpgrade = "upgrade:";
    if (key.starts_with(kUpgrade)) return Picture("upgrade" + key.substr(kUpgrade.size()));
    // "path:19:0:1" -> the second picture of goldencheat_upgradesa (upgrades.cpp).
    if (key.starts_with("path:")) return PathPicture(key);
    // "pose:19:up1" -> goldencheat_up1, a single upgrade picture (upgrades.cpp).
    if (key.starts_with("pose:")) return PosePicture(key);
    // The menu's gold symbol: the HUD's coin.
    if (key == "ui:gold") return Picture(kGoldPicture);
    if (key == kControllerArtKey) return ControllerPicture();
    return std::shared_ptr<const skylanders::FigureImage>();
  });
}

}  // namespace ssarecomp
