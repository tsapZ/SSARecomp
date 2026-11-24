// The Extras page's cheats (settings_pages.cpp), done each frame from where_hook.cpp's hook of the
// game's per-frame state update.

#pragma once

#include <cstdint>

namespace ssarecomp {

// Called at the start of the game's per-frame update while it plays (game state 2).
void ApplyExtras(uint8_t* base);

// True when `component` is the health component of a player's Skylander (the way above, through the
// player list). For the difficulty (difficulty_hook.cpp); on the game's thread.
bool IsPlayerHealth(uint8_t* base, uint32_t component);

}  // namespace ssarecomp
