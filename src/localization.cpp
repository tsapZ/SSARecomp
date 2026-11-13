#include "localization.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>
#include <unordered_map>

#include <rex/cvar.h>
#include <rex/logging.h>

#ifdef _WIN32
#include <windows.h>
#endif

// Defined in the SDK (xam_user.cpp); both XGetLanguage and ExGetXConfigSetting read this.
REXCVAR_DECLARE(uint32_t, user_language);
// Defined in the SDK (xam_locale.cpp): the console's country (XC_COUNTRY ids), which XGetLocale reads.
REXCVAR_DECLARE(uint32_t, user_country);

REXCVAR_DEFINE_STRING(language, "auto", "Game",
                      "Game text/voice language: auto (the Windows display language if it is one "
                      "of the following, else English), english, french, german, spanish, "
                      "italian, and dutch, swedish, norwegian, danish, finnish when the disc has "
                      "them. A raw Xbox 360 language ID (a number) also works, for experiments.");

namespace ssarecomp {

namespace {

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// Dutch and the Nordic four aren't Xbox 360 language ids: the game picks their text and movies
// (text\dut, movies\dut, ...) when the language is English and the console's country is theirs
// (Dutch: CoL #68, checked 2026-11-09 on the European disc, the menus came up in Dutch; Swedish:
// SSA #47, checked 2026-11-12 with the country Sweden, title, menus and chapter texts in Swedish).
// The countries are XC_COUNTRY ids (the SDK's xam_locale.cpp table).
struct CountryLanguage {
  uint32_t country;
  const char* folder;  // under text\ (and movies\, audio\)
};

const std::unordered_map<std::string, CountryLanguage>& CountryLanguages() {
  static const std::unordered_map<std::string, CountryLanguage> kLanguages = {
      {"dutch", {74u, "dut"}},      // XC_COUNTRY_NL
      {"swedish", {90u, "swe"}},    // XC_COUNTRY_SE
      {"norwegian", {75u, "nor"}},  // XC_COUNTRY_NO
      {"danish", {25u, "dan"}},     // XC_COUNTRY_DK
      {"finnish", {32u, "fin"}},    // XC_COUNTRY_FI
  };
  return kLanguages;
}

std::filesystem::path g_disc_root;

// The Windows display language as one of the names below; English for any other.
std::string SystemLanguage() {
#ifdef _WIN32
  switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_GERMAN:
      return "german";
    case LANG_FRENCH:
      return "french";
    case LANG_SPANISH:
      return "spanish";
    case LANG_ITALIAN:
      return "italian";
    case LANG_DUTCH:
      return "dutch";
    case LANG_SWEDISH:
      return "swedish";
    case LANG_NORWEGIAN:
      return "norwegian";
    case LANG_DANISH:
      return "danish";
    case LANG_FINNISH:
      return "finnish";
    default:
      break;
  }
#endif
  return "english";
}

}  // namespace

void SetDiscRoot(const std::filesystem::path& root) { g_disc_root = root; }

bool DiscHasCountryLanguage(const std::string& name) {
  const auto it = CountryLanguages().find(name);
  if (it == CountryLanguages().end()) return false;
  if (g_disc_root.empty()) return true;
  std::error_code ec;
  return std::filesystem::is_directory(g_disc_root / "text" / it->second.folder, ec);
}

void ApplyLanguageSetting() {
  // A direct --user_language / user_language=<n> override (raw, for experimentation) always
  // wins: don't clobber it with whatever `language` defaults to (a real bug this had until now --
  // it unconditionally overwrote user_language, silently discarding a direct override).
  if (rex::cvar::HasNonDefaultValue("user_language")) {
    REXLOG_INFO("Language: user_language already set directly ({}); leaving it alone",
               REXCVAR_GET(user_language));
    return;
  }

  // Xbox 360 XLanguage IDs (include/rex/system/xcontent.h); only these five are confirmed to
  // match a real console. Dutch and the Nordic four aren't in that enum: they come from the
  // console's country (CountryLanguages above).
  static const std::unordered_map<std::string, uint32_t> kNames = {
      {"english", 1}, {"german", 3}, {"french", 4}, {"spanish", 5}, {"italian", 6},
  };

  std::string setting = Lower(REXCVAR_GET(language));
  if (setting == "auto") {
    setting = SystemLanguage();
    REXLOG_INFO("Language: auto, from the Windows display language");
  }
  // Dutch and the Nordic four: English with their country. Only when the disc has the language: a
  // pressing without its text folder stays English, as before these were added (Automatic on a
  // Finnish Windows, or a settings file from another disc). A country set directly
  // (--user_country) stays.
  if (auto it = CountryLanguages().find(setting); it != CountryLanguages().end()) {
    REXCVAR_SET(user_language, 1u);
    if (!DiscHasCountryLanguage(setting)) {
      REXLOG_WARN("Language: the disc has no {} (no text/{} folder); using English", setting,
                  it->second.folder);
      return;
    }
    if (!rex::cvar::HasNonDefaultValue("user_country")) {
      REXCVAR_SET(user_country, it->second.country);
    }
    REXLOG_INFO("Language: {} (English with the country {})", setting, REXCVAR_GET(user_country));
    return;
  }
  if (auto it = kNames.find(setting); it != kNames.end()) {
    REXCVAR_SET(user_language, it->second);
    REXLOG_INFO("Language: {} (id {})", setting, it->second);
    return;
  }

  uint32_t numeric_id = 0;
  auto [ptr, ec] = std::from_chars(setting.data(), setting.data() + setting.size(), numeric_id);
  if (ec == std::errc() && ptr == setting.data() + setting.size()) {
    REXCVAR_SET(user_language, numeric_id);
    REXLOG_INFO("Language: raw id {}", numeric_id);
    return;
  }

  REXLOG_WARN("Language: '{}' is not a known name or a number; using English", setting);
  REXCVAR_SET(user_language, 1u);
}

}  // namespace ssarecomp
