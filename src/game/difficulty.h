// Difficulty levels for Spyro's Adventure, which has none of its own (the later games ask for one on
// a new game: Giants, Swap Force and Trap Team). A new save asks for one of four, and the save keeps
// it:
//
//   Easy        enemies x0.6
//   Adventurer  enemies x1.0 (the game as it is; saves from before this have it)
//   Hard        enemies x2.0
//   Nightmare   enemies x3.0
//
// The multiplier makes the enemies stronger and tougher alike: the damage an enemy's hit does is
// multiplied by it, the damage a Skylander's hit does to an enemy divided by it (so an enemy takes
// that many times the hits). The hooks are in hooks/difficulty_hook.cpp, the picker in
// game/difficulty.cpp.
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

// x0.6, x1, x2, x3.
float DifficultyMultiplier(Difficulty difficulty);
// "easy", "adventurer", "hard", "nightmare" (difficulty.txt's words).
const char* DifficultyKey(Difficulty difficulty);

// Reads difficulty.txt and registers the picker. From ReXApp::OnCreateDialogs.
void InstallDifficulty(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext* app_context,
                       const std::filesystem::path& user_data_root);
// Closes the picker; from ReXApp::OnShutdown.
void ShutdownDifficulty();

// F4 > Extras: "Change this game's difficulty" (opens the picker for the save being played).
skylanders::Setting ChangeDifficultySetting();

// The game opens or creates a save package (`name` its folder, "Skylanders - Slot 1"); `create`
// when it may make it. A slot save that didn't exist yet is a new game: the picker opens. Called on
// the game's thread, before the package is opened (difficulty_hook.cpp).
void OnSavePackage(const std::string& name, bool create);

// The enemies' multiplier for the save being played: 1 before any slot save was opened. Any thread.
float EnemyMultiplier();

}  // namespace ssarecomp
