// Where the player is, for problem reports: the level and player 1's position (where_hook.cpp).

#pragma once

#include <cstdint>
#include <filesystem>

#include <rex/ppc/context.h>

namespace ssarecomp {

// Reads the level table (gamedata.lxb in the game's data folder), so the log names the level; and
// watches the slot saves under `user_data_root` for Soul Gems (Core of Light #71).
void InstallWhere(const std::filesystem::path& game_data_root, const std::filesystem::path& user_data_root);

// True while a Battle Mode arena (PvP_Level_*) is loaded: the difficulty leaves those alone.
bool InBattleLevel(uint8_t* base);

}  // namespace ssarecomp

#if defined(SKYLANDERS_DEVKIT)
// The developer kit's per-frame work (a private test build only), from where_hook.cpp's hook.
void DevKitFrame(PPCContext& ctx, uint8_t* base);
#endif
