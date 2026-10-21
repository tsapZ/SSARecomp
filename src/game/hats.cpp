// The hats for the portal menu's details view, in the game's language.
//
// A figure stores its hat as the game's kTfbSpyroTag_Hat_* number (portal FigureStats::hat, at
// +0x14 of the figure's save area). Swap Force's executable has that enum (1 Army, 2 French, ...,
// 45 BirthdayCake, the last of 2011), and this game's data names its hats after it: the picture
// "<name>_lg" in uigameimage.str (all 45 there, 128x128, see ui_images.cpp) and the text
// "<name>_hat" in global.lxb ("tophat" without the suffix), e.g. army_hat = "&COLOR(255,255,0)
// Kampfhut &COLOR(255,255,255)\n +15 $ICON_HAT_BUFF_ELEMENT$". The text is the hat's name, then
// one line per bonus with the game's symbol for it instead of a word: the symbols are the
// pictures "elementalstrength", "armor", "criticalhit" and "speed" (art keys "stat:<picture>").
// The menu shows the symbol, the number and the stat's name, which the hat text leaves out: the
// labels of the game's stats screen, "StatsElemetal" (sic), "StatsArmor", "StatsCT" and
// "StatsSpeed" = "Elementar: ", "R\xC3\xBCstung: ", "Kritischer Treffer: ", "Tempo: " (found by
// hashing the table's key strings against the label texts, 2026-10-21). Found by hashing the
// enum's names (2026-10-20): 35 of the 45 hats have their text as "<name>_hat" or "<name>"; the
// other ten under keys of their own, found in the table's key strings (2026-10-21, after the owner
// saw "Hut Nr. 43" for Chop Chop's flower hat): "hat_flower_pot" = "Blumenhut", +2 critical hit,
// +2 armor, and kOtherKeys below.

#include "hats.h"

#include <cstdlib>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "game/game_text.h"
#include "portal_rex/portal_menu.h"

namespace ssarecomp {
namespace {

using skylanders::HatInfo;
using skylanders::HatStat;

// By hat number - 1, the picture names in uigameimage.str.
constexpr const char* kHats[] = {
    "army",        "french",  "goggles",     "mining",  "officer",        "pirate",
    "propeller",   "raccoon", "straw",       "sunday",  "tophat",         "viking",
    "wwi",         "anvil",   "beret",       "birthday", "bonehead",      "bowler",
    "bunny",       "carmen",  "chef",        "cowboy",  "crazy_hair",     "crown",
    "devil_horns", "eyeball", "fez",         "halo",    "jester",         "mercury",
    "moose",       "plunger", "pot",         "rocket",  "santa",          "tiki",
    "trojan",      "unicorn", "wizard",      "pumpkin", "pirate_bandana", "cossack",
    "flower_pot",  "balloon", "birthday_cake",
};

// The hats whose text has a key of its own (the others: "<name>_hat", else "<name>").
struct OtherKey {
  std::string_view hat;
  const char* key;
};
constexpr OtherKey kOtherKeys[] = {
    {"bonehead", "_boneHead_hat"},
    {"crazy_hair", "crazyHair_hat"},
    {"devil_horns", "devilHorns_hat"},
    {"eyeball", "eyeBall_hat"},
    {"pumpkin", "hat_Pumpkin"},
    {"pirate_bandana", "hat_minion_pirate_bandana"},
    {"cossack", "hat_minion_cossack"},
    {"flower_pot", "hat_flower_pot"},
    {"balloon", "minion_balloon"},
    {"birthday_cake", "minion_birthday_cake"},
};

// The bonus symbols in the hat texts, the pictures that show them and the stats screen's label.
struct Bonus {
  std::string_view symbol;
  const char* picture;
  const char* label;
};
constexpr Bonus kBonuses[] = {
    {"$ICON_HAT_BUFF_ELEMENT$", "elementalstrength", "StatsElemetal"},
    {"$ICON_HAT_BUFF_ARMOR$", "armor", "StatsArmor"},
    {"$ICON_HAT_BUFF_CRITICAL$", "criticalhit", "StatsCT"},
    {"$ICON_HAT_BUFF_SPEED$", "speed", "StatsSpeed"},
};

// "Kritischer Treffer: " -> "Kritischer Treffer".
std::string LabelName(const char* key) {
  auto text = GameText(key);
  if (!text) return {};
  std::string name = *text;
  while (!name.empty() && (name.back() == ' ' || name.back() == ':')) name.pop_back();
  return name;
}

std::string Trim(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\r')) s.pop_back();
  size_t start = 0;
  while (start < s.size() && s[start] == ' ') ++start;
  return s.substr(start);
}

// Removes the "&COLOR(r,g,b)" tags.
std::string WithoutColors(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s.compare(i, 7, "&COLOR(") == 0) {
      const size_t close = s.find(')', i);
      if (close != std::string::npos) {
        i = close;
        continue;
      }
    }
    out.push_back(s[i]);
  }
  return out;
}

std::shared_ptr<const HatInfo> Build(uint16_t hat) {
  const char* base = HatBaseName(hat);
  if (!base) return nullptr;
  auto info = std::make_shared<HatInfo>();
  info->image = std::string("hat:") + base;
  std::optional<std::string> text;
  for (const OtherKey& other : kOtherKeys) {
    if (other.hat == base) text = GameText(other.key);
  }
  if (!text) text = GameText(std::string(base) + "_hat");
  if (!text) text = GameText(base);
  if (!text) return info;
  const std::string plain = WithoutColors(*text);
  size_t start = 0;
  bool first = true;
  while (start <= plain.size()) {
    size_t end = plain.find('\n', start);
    if (end == std::string::npos) end = plain.size();
    const std::string line = Trim(plain.substr(start, end - start));
    start = end + 1;
    if (line.empty()) continue;
    if (first) {
      info->name = line;
      first = false;
      continue;
    }
    for (const Bonus& bonus : kBonuses) {
      const size_t at = line.find(bonus.symbol);
      if (at == std::string::npos) continue;
      HatStat stat;
      stat.value = float(std::atoi(line.substr(0, at).c_str()));
      stat.name = LabelName(bonus.label);
      stat.image = std::string("stat:") + bonus.picture;
      info->stats.push_back(std::move(stat));
    }
  }
  return info;
}

}  // namespace

const char* HatBaseName(uint16_t hat) {
  return hat >= 1 && hat <= std::size(kHats) ? kHats[hat - 1] : nullptr;
}

void InstallHats() {
  // Called every frame while a figure with a hat is shown: each hat is built once.
  skylanders::SetHatInfoProvider([](uint16_t hat) {
    static std::mutex mutex;
    static std::unordered_map<uint16_t, std::shared_ptr<const HatInfo>> built;
    std::lock_guard<std::mutex> lock(mutex);
    auto it = built.find(hat);
    if (it == built.end()) it = built.emplace(hat, Build(hat)).first;
    return it->second;
  });
}

}  // namespace ssarecomp
