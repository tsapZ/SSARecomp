// Difficulty levels for Spyro's Adventure, which has none of its own (the later games ask for one on
// a new game). A new save asks for one of four, and the save keeps it. The values are the later
// games' own (docs/difficulty.md has where each comes from):
//
//                damage taken  enemy health  food heals  level-up heals  XP and gold
//   Easy         x0.4          x0.6          100 %       to full         x1
//   Adventurer   x1            x1            100 %       to full         x1     (the game as it is)
//   Hard         x2.1          x1.35         75 %        20 %            x1.25
//   Nightmare    x5            x2.5          60 %        10 %            x1.5
//
// Saves from before this are Adventurer. The hooks are in hooks/difficulty_hook.cpp (damage, food,
// level-up) and hooks/multiplier_cheats_hook.cpp (XP and gold), the picker in game/difficulty.cpp.
//
// Where the choice is kept: difficulty.txt in the user data folder, a line per slot save
// ("Skylanders - Slot 1=hard"). The game's own saves stay as they are.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace rex::ui {
class ImGuiDrawer;
class WindowedAppContext;
}  // namespace rex::ui

namespace skylanders {
struct Setting;
}  // namespace skylanders

namespace ssarecomp {

enum class Difficulty : int { kEasy = 0, kAdventurer = 1, kHard = 2, kNightmare = 3 };
constexpr int kDifficultyCount = 4;

// "easy", "adventurer", "hard", "nightmare" (difficulty.txt's words).
const char* DifficultyKey(Difficulty difficulty);

// Reads difficulty.txt and registers the picker. From ReXApp::OnCreateDialogs.
void InstallDifficulty(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext* app_context,
                       const std::filesystem::path& user_data_root);
// Closes the picker; from ReXApp::OnShutdown.
void ShutdownDifficulty();

// F4 > Extras: "Change this game's difficulty" (opens the picker for the save being played).
skylanders::Setting ChangeDifficultySetting();

// The game opens or writes a save package (`name` its folder, "Skylanders - Slot 1"; `how` the
// XamContentCreateEx flags' low four bits; `on_main_menu` while the FrontEnd level is loaded). A slot
// save written from the main menu, or one created that didn't exist, is a new game: the picker opens
// and the game waits for it. Called on the game's thread, before the package is opened
// (difficulty_hook.cpp).
void OnSavePackage(const std::string& name, uint32_t how, bool on_main_menu);

// For the save being played (Adventurer, all 1, before any slot save was opened). Any thread.
float DamageTakenScale();  // the damage a player's Skylander takes
float EnemyHealthScale();  // the health of everything else (enemies, bosses)
float RewardScale();       // XP and gold
float FoodHealScale();     // the part of a food pickup's heal that is given
float LevelUpHealPart();   // the part of the full health a level-up gives back

}  // namespace ssarecomp
