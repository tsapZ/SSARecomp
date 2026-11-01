// The Extras page's cheats (settings_pages.cpp), done each frame from where_hook.cpp's hook of the
// game's per-frame state update.

#pragma once

#include <cstdint>

namespace ssarecomp {

// Called at the start of the game's per-frame update while it plays (game state 2).
void ApplyExtras(uint8_t* base);

}  // namespace ssarecomp
