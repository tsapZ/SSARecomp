#pragma once

// Every Skylander's upgrades for the portal menu's details view, from the game's own text
// (see upgrades.cpp).

namespace ssarecomp {

// Registers the portal menu's upgrades provider. The texts come from game_text.h's table, so call
// it after InstallGameText.
void InstallUpgrades();

}  // namespace ssarecomp
