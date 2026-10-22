// Every Skylander's upgrades for the portal menu's details view.
//
// The game's text table (global.lxb, game_text.cpp) names them by the character's internal name,
// the one its HUD portrait has (ui_images.cpp): "<Name>Pri" / "<Name>Sec" the two starting
// attacks, "<Name>Up00" ... "Up10" the upgrades, "<Name>Path1" / "Path2" the two paths, each with
// "_Des" for its description. Counted over the table (2026-10-21): 32 characters with all eleven
// upgrades. The layout is the one Giants and Swap Force use: 00-03 the four basic upgrades, 04-06
// the first path, 07-09 the second, 10 the Soul Gem (Spyro: Up04 "Feuerschild" and Up05
// "Explodierende Feuersbrunst" under "Schafbrenner-Spyro", Up08 "Kometensprint" under
// "Blitz-Spyro", Up10 "Spyros Erdklopfer").
// The descriptions name the attack buttons with icons: ICON_ENTER is attack 1, ICON_HATK attack 2,
// ICON_UNDO attack 3 (Trigger Happy's "Pot of Gold" and Chop Chop's shield are HATK, their second
// attacks; Whirlwind's Rainbow of Doom ENTER, her first). They are shown as "Angriff 1" ... so
// they hold whatever buttons the player uses.
// The upgrade bits of a figure (FigureStats::upgrades) are the same in every game: bit 0 a path is
// chosen, bit 1 the second path, from bit 2 the basic four, the chosen path's three and the Soul
// Gem, from bit 11 the other path's (GiantsRecomp's toy_upgrades.cpp, which shows this game's
// figures' upgrades right).
// The prices are Giants' (its Minion_UpgradeCosts: 500, 700, 900, 1200, 1700, 2200, 3000, 4000).
// They are this game's too: the Skylanders wiki's list of Dark Spyro's Spyro's Adventure upgrades
// (checked 2026-10-22) has Long Range Raze 500, Spyro's Flight 700, Sprint Charge 900, Triple
// Flameballs 1200, each path 1700 / 2200 / 3000 and the Soul Gem's Dive Bomb 4000. This game's own
// table isn't found in its files (not in gamedata.lxb or tuning.lxb as plain numbers).
// Pictures: the Soul Gem's is "upgrade<toy id>" in uigameimage.str (ui_images.cpp): 32 pictures,
// one per Skylander by toy id (upgrade016 is Spyro's ground pound, "Spyros Erdklopfer";
// upgrade014 Gill Grunt's anchor, upgrade019 Trigger Happy's golden cannon, "Infinite Ammo").
// The two paths' six are in the character's own archive, three per path ("path:<id>:<path>:<n>",
// ui_images.cpp). Dark Spyro (28) shows Spyro's, the legendary figures their character's. The
// starting attacks and the four basic upgrades have no picture in the game's files.

#include "upgrades.h"

#include <cstdio>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "game/game_text.h"
#include "portal_rex/menu_draw.h"
#include "portal_rex/portal_menu.h"

namespace ssarecomp {
namespace {

using skylanders::FigureUpgrade;
using skylanders::FigureUpgradePath;
using skylanders::FigureUpgrades;

constexpr uint32_t kCosts[] = {500, 700, 900, 1200, 1700, 2200, 3000, 4000};
constexpr int kBasicCount = 4;
constexpr int kPathLength = 3;
constexpr int kFirstPath = 4;
constexpr int kSoulGem = 10;
constexpr int kFirstUpgradeBit = 2;
constexpr int kFirstPathBit = kFirstUpgradeBit + kBasicCount;
constexpr int kSoulGemBit = kFirstPathBit + kPathLength;
constexpr int kFirstAlternateBit = kSoulGemBit + 2;  // bit 10 is Giants' Wow Pow

// The text keys' character names by toy id (the HUD portraits' names in ui_images.cpp, in the
// table's spelling). Dark Spyro (28) is Spyro.
constexpr const char* kNames[33] = {
    "AirDragon",   "Griffin",     "SkyTurtle",   "StormGiant",     "RockDragon",  "LandShark",
    "DinoRang",    "GemGolem",    "PhoenixDragon", "Eruptor",      "FlameKnight", "FireElf",
    "WaterDragon", "CrustBuckler", "GillGrunt",  "Yeti",           "SpyroJr",     "SkullOrc",
    "TikiWizard",  "GoldenCheat", "MetalDragon", "DrillBot",       "BombTroll",   "ForceFieldGrub",
    "PlantDragon", "Bambazooker", "StealthElf",  "StumpSmashEnt",  "SpyroJr",     "ShadowMaid",
    "PandoranGuard", "GhostEater", "Cynder",
};
constexpr uint16_t kLegendaryIds = 400;

const char* NameOf(uint16_t id) {
  if (id >= kLegendaryIds && id < kLegendaryIds + 100) id = id - kLegendaryIds;
  return id < std::size(kNames) ? kNames[id] : nullptr;
}

// The text without its colour tags and with the attack icons as words.
std::string Clean(const std::string& s) {
  const bool german = skylanders::menu_draw::GermanText();
  struct Icon {
    const char* tag;
    const char* en;
    const char* de;
  };
  static const Icon kIcons[] = {
      {"&ICON(0,ICON_ENTER)", "Attack 1", "Angriff 1"},
      {"&ICON(0,ICON_HATK)", "Attack 2", "Angriff 2"},
      {"&ICON(0,ICON_UNDO)", "Attack 3", "Angriff 3"},
  };
  std::string out;
  for (size_t i = 0; i < s.size();) {
    bool matched = false;
    for (const Icon& icon : kIcons) {
      const std::string_view tag(icon.tag);
      if (s.compare(i, tag.size(), tag) == 0) {
        out += german ? icon.de : icon.en;
        i += tag.size();
        matched = true;
        break;
      }
    }
    if (matched) continue;
    if (s.compare(i, 7, "&COLOR(") == 0 || s.compare(i, 6, "&ICON(") == 0) {
      const size_t close = s.find(')', i);
      if (close != std::string::npos) {
        i = close + 1;
        continue;
      }
    }
    out.push_back(s[i++]);
  }
  return out;
}

std::string Text(const std::string& key) {
  auto t = GameText(key);
  return t ? Clean(*t) : std::string();
}

std::shared_ptr<const FigureUpgrades> Build(const std::string& name, uint16_t id) {
  // Legendary figures (400 + the toy id) and Dark Spyro show their character's pictures.
  if (id >= kLegendaryIds && id < kLegendaryIds + 100) id = uint16_t(id - kLegendaryIds);
  if (id == 28) id = 16;
  char key[64];
  auto up = [&](int i, int bit, uint32_t cost) {
    FigureUpgrade u;
    std::snprintf(key, sizeof(key), "%sUp%02d", name.c_str(), i);
    u.name = Text(key);
    u.description = Text(std::string(key) + "_Des");
    u.bit = bit;
    u.cost = cost;
    return u;
  };
  auto result = std::make_shared<FigureUpgrades>();
  FigureUpgradePath start;
  start.kind = FigureUpgradePath::Kind::kStart;
  for (const char* which : {"Pri", "Sec"}) {
    FigureUpgrade u;
    u.name = Text(name + which);
    u.description = Text(name + which + "_Des");
    if (!u.name.empty()) start.upgrades.push_back(std::move(u));
  }
  if (!start.upgrades.empty()) result->paths.push_back(std::move(start));
  FigureUpgradePath basic;
  basic.kind = FigureUpgradePath::Kind::kBasic;
  for (int i = 0; i < kBasicCount; ++i) basic.upgrades.push_back(up(i, kFirstUpgradeBit + i, kCosts[i]));
  result->paths.push_back(std::move(basic));
  for (int p = 0; p < 2; ++p) {
    FigureUpgradePath path;
    path.kind = FigureUpgradePath::Kind::kPath;
    path.name = Text(name + (p == 0 ? "Path1" : "Path2"));
    path.description = Text(name + (p == 0 ? "Path1_Des" : "Path2_Des"));
    for (int j = 0; j < kPathLength; ++j) {
      FigureUpgrade u = up(kFirstPath + kPathLength * p + j, kFirstPathBit + j, kCosts[kBasicCount + j]);
      u.alternate_bit = kFirstAlternateBit + j;
      std::snprintf(key, sizeof(key), "path:%u:%d:%d", unsigned(id), p, j);
      u.image = key;
      path.upgrades.push_back(std::move(u));
    }
    result->paths.push_back(std::move(path));
  }
  FigureUpgradePath soul_gem;
  soul_gem.kind = FigureUpgradePath::Kind::kSoulGem;
  soul_gem.upgrades.push_back(up(kSoulGem, kSoulGemBit, kCosts[kBasicCount + kPathLength]));
  std::snprintf(key, sizeof(key), "upgrade:%03u", unsigned(id));
  soul_gem.upgrades.back().image = key;
  result->paths.push_back(std::move(soul_gem));
  return result;
}

}  // namespace

void InstallUpgrades() {
  // Called every frame while a figure's details are shown: each character is built once its text
  // is there (the table loads at start-up), in the language the menu speaks then.
  skylanders::SetFigureUpgradesProvider(
      [](uint16_t id, uint16_t) -> std::shared_ptr<const FigureUpgrades> {
        const char* name = NameOf(id);
        if (!name || !GameText(std::string(name) + "Up00")) return nullptr;
        static std::mutex mutex;
        static std::unordered_map<std::string, std::shared_ptr<const FigureUpgrades>> built;
        const std::string key =
            std::string(name) + (skylanders::menu_draw::GermanText() ? "|de" : "|en");
        std::lock_guard<std::mutex> lock(mutex);
        auto it = built.find(key);
        if (it == built.end()) it = built.emplace(key, Build(name, id)).first;
        return it->second;
      });
}

}  // namespace ssarecomp
