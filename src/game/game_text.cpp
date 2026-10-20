// The game's own text, from rom/text/<language>/global.lxb.
//
// The file is a table of 2335 entries: a big-endian count at 0x7C, then that many pairs of
// (CRC-32 of the key, offset), where the offset counts from the offset field itself to a
// NUL-terminated string, UTF-8 or cp1252 (the file mixes both: "D\xC3\xBCstergruft" next to
// "Spionageausr\xFCstung"). The keys are the game's own names ("Anvil" for Anvil Rain,
// "GriffinUp09" for an upgrade, "propeller_hat" for a hat); the file also lists them as plain
// strings, which is how they were found. Worked out from the file (2026-10-20): the German and
// English tables give the same keys, e.g. "Anvil" = "Ambossregen" / "Anvil Rain".

#include "game_text.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "portal_rex/portal_menu.h"

// Defined in the SDK (xam_user.cpp); the language the game runs in.
REXCVAR_DECLARE(uint32_t, user_language);

namespace ssarecomp {
namespace {

std::unordered_map<uint32_t, std::string> g_text;  // CRC-32 of the key -> UTF-8 text

uint32_t Crc32(std::string_view s) {
  uint32_t crc = 0xFFFFFFFFu;
  for (unsigned char c : s) {
    crc ^= c;
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

uint32_t Be32(const std::vector<uint8_t>& d, size_t at) {
  return (uint32_t(d[at]) << 24) | (uint32_t(d[at + 1]) << 16) | (uint32_t(d[at + 2]) << 8) |
         d[at + 3];
}

// True if `s` is valid UTF-8 (plain ASCII included).
bool IsUtf8(const uint8_t* s) {
  while (*s) {
    int follow = *s < 0x80 ? 0 : (*s >> 5) == 6 ? 1 : (*s >> 4) == 14 ? 2 : (*s >> 3) == 30 ? 3 : -1;
    if (follow < 0) return false;
    ++s;
    for (; follow > 0; --follow, ++s) {
      if ((*s >> 6) != 2) return false;
    }
  }
  return true;
}

// cp1252 to UTF-8 (the table's texts are Western European; 0x80-0x9F as cp1252 punctuation).
std::string Utf8FromCp1252(const char* s) {
  static const char16_t kHigh[32] = {
      0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
      0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
      0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
  std::string out;
  for (; *s; ++s) {
    const uint8_t c = uint8_t(*s);
    const uint32_t cp = c >= 0x80 && c < 0xA0 ? kHigh[c - 0x80] : c;
    if (cp < 0x80) {
      out += char(cp);
    } else if (cp < 0x800) {
      out += char(0xC0 | (cp >> 6));
      out += char(0x80 | (cp & 0x3F));
    } else {
      out += char(0xE0 | (cp >> 12));
      out += char(0x80 | ((cp >> 6) & 0x3F));
      out += char(0x80 | (cp & 0x3F));
    }
  }
  return out;
}

// The text folder for the game's language (XGetLanguage ids; the disc's other five languages
// have no Xbox 360 id the SDK knows, see localization.cpp).
const char* LanguageFolder(uint32_t language) {
  switch (language) {
    case 3: return "ger";
    case 4: return "fre";
    case 5: return "spa";
    case 6: return "ita";
    default: return "eng";
  }
}

// The table key of a magic item's or adventure pack's name, by toy id (looked up through their
// English names in the English table). Skylanders keep the catalog's names: the game's are the
// same in every language, apart from short forms like "L-Spyro".
const char* NameKey(uint16_t id) {
  switch (id) {
    case 200: return "Anvil";          // Anvil Rain
    case 201: return "SecretStash";    // Hidden Treasure
    case 202: return "Regeneration";   // Healing Elixir
    case 203: return "Pirates";        // Ghost Swords
    case 204: return "Hourglass";      // Time Twister
    case 205: return "Shield";         // Sky-Iron Shield
    case 206: return "Potion";         // Winged Boots
    case 207: return "Zapper";         // Sparx Dragonfly
    case 300: return "Level_046";      // Dragon's Peak
    case 301: return "Level_003";      // Empire of Ice
    case 302: return "Level_008";      // Pirate Seas
    case 303: return "Level_036";      // Darklight Crypt
    case 304: return "PvP_Level_009";  // Volcanic Vault
    default: return nullptr;
  }
}

}  // namespace

std::optional<std::string> GameText(std::string_view key) {
  if (auto it = g_text.find(Crc32(key)); it != g_text.end()) return it->second;
  return std::nullopt;
}

void InstallGameText(const std::filesystem::path& text_root) {
  const auto path = text_root / LanguageFolder(REXCVAR_GET(user_language)) / "global.lxb";
  std::ifstream in(path, std::ios::binary);
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (d.size() < 0x80) {
    REXLOG_WARN("Game text: cannot read {}", path.string());
    return;
  }
  const uint32_t count = Be32(d, 0x7C);
  if (0x80 + uint64_t(count) * 8 > d.size()) {
    REXLOG_WARN("Game text: {} has an unexpected layout", path.string());
    return;
  }
  for (uint32_t i = 0; i < count; ++i) {
    const size_t at = 0x80 + size_t(i) * 8;
    const size_t text = at + 4 + Be32(d, at + 4);
    if (text >= d.size()) continue;
    // NUL-terminated within the file (the file ends with one).
    size_t end = text;
    while (end < d.size() && d[end]) ++end;
    if (end >= d.size()) continue;
    const char* str = reinterpret_cast<const char*>(d.data() + text);
    g_text[Be32(d, at)] = IsUtf8(d.data() + text) ? std::string(str) : Utf8FromCp1252(str);
  }
  REXLOG_INFO("Game text: {} entries from {}", g_text.size(), path.string());
  skylanders::SetFigureNameProvider([](uint16_t id, uint16_t /*variant*/) -> std::optional<std::string> {
    const char* key = NameKey(id);
    return key ? GameText(key) : std::nullopt;
  });
}

}  // namespace ssarecomp
