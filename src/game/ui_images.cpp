// The game's own pictures for the portal menu, from the disc's uigameimage.str.
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

#include "ui_images.h"

#include <atomic>
#include <cstdint>
#include <cstring>
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

#include "portal_rex/portal_menu.h"
#include "xenos_dxt.h"

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
namespace {

struct Entry {
  int width = 0;
  int height = 0;
  Dxt kind = Dxt::kDxt5;
  bool tiled = true;
  std::vector<uint8_t> texels;
};

std::mutex g_mutex;
std::map<std::string, Entry> g_entries;  // only the pictures the menu can use
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

bool Wanted(std::string_view name) {
  return (name.starts_with("toy_") && name.ends_with("_hud")) || name.starts_with("ico_element_");
}

// Indexes one unpacked archive; keeps the texels of the pictures the menu uses.
size_t Index(const uint8_t* d, size_t n) {
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
    if (!Wanted(name)) continue;
    const uint32_t dword0 = Be32(rec + *fetch + 8), dword1 = Be32(rec + *fetch + 12),
                   dword2 = Be32(rec + *fetch + 16);
    Entry e;
    e.tiled = (dword0 >> 31) != 0;
    switch (dword1 & 0x3F) {
      case 0x12: e.kind = Dxt::kDxt1; break;
      case 0x13: e.kind = Dxt::kDxt3; break;
      case 0x14: e.kind = Dxt::kDxt5; break;
      default: continue;
    }
    e.width = int(dword2 & 0x1FFF) + 1;
    e.height = int((dword2 >> 13) & 0x1FFF) + 1;
    const uint32_t texel_offset = Be32(rec + *phys + 8), texel_size = Be32(rec + *phys + 16);
    if (uint64_t(texel_offset) + texel_size > size) continue;
    e.texels.assign(rec + texel_offset, rec + texel_offset + texel_size);
    g_entries[name] = std::move(e);
    ++kept;
    at += size - 1;
  }
  return kept;
}

void Load(std::filesystem::path archive) {
  std::ifstream in(archive, std::ios::binary);
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (file.size() < 22 || std::memcmp(file.data(), "strm", 4) != 0) {
    REXLOG_WARN("UI images: {} is missing or not a strm archive", archive.string());
    return;
  }
  int out_size = 0;
  char* out = stbi_zlib_decode_malloc_guesssize_headerflag(
      reinterpret_cast<const char*>(file.data() + 20), int(file.size() - 20),
      int(file.size() * 4), &out_size, 1);
  if (!out) {
    REXLOG_WARN("UI images: could not unpack {}", archive.string());
    return;
  }
  size_t kept = 0;
  {
    std::lock_guard lock(g_mutex);
    kept = Index(reinterpret_cast<const uint8_t*>(out), size_t(out_size));
  }
  STBI_FREE(out);
  g_loaded.store(true);
  REXLOG_INFO("UI images: {} pictures from {} ({} bytes unpacked)", kept,
              archive.filename().string(), out_size);
}

// The decoded picture for `name`, decoded once; nullptr while loading or if there is none.
std::shared_ptr<const skylanders::FigureImage> Picture(const std::string& name) {
  if (!g_loaded.load()) return nullptr;
  std::lock_guard lock(g_mutex);
  if (auto it = g_decoded.find(name); it != g_decoded.end()) return it->second;
  std::shared_ptr<const skylanders::FigureImage> result;
  if (auto it = g_entries.find(name); it != g_entries.end()) {
    const Entry& e = it->second;
    if (auto rgba = DecodeXenosDxt(e.texels.data(), e.texels.size(), e.width, e.height, e.kind,
                                   e.tiled)) {
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

}  // namespace

void InstallUiImages(const std::filesystem::path& archive) {
  std::thread(Load, archive).detach();
  skylanders::SetFigureImageProvider([](uint16_t id, uint16_t /*variant*/) {
    const char* name = PortraitName(id);
    return name ? Picture(std::string("toy_") + name + "_hud") : nullptr;
  });
  skylanders::SetArtImageProvider([](const std::string& key) {
    // "element:magic" -> ico_element_magic_l
    constexpr std::string_view kElement = "element:";
    if (key.starts_with(kElement)) {
      return Picture("ico_element_" + key.substr(kElement.size()) + "_l");
    }
    return std::shared_ptr<const skylanders::FigureImage>();
  });
}

}  // namespace ssarecomp
