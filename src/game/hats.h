#pragma once

// The hats for the portal menu's details view: the game's name, picture and bonuses of the hat a
// figure wears (see hats.cpp).

#include <cstdint>

namespace ssarecomp {

// The name of a hat's picture and text in the game's data ("army" for 1), by the hat number a
// figure stores; nullptr for a number this game doesn't have.
const char* HatBaseName(uint16_t hat);

// Registers the portal menu's hat provider. Call after InstallGameText.
void InstallHats();

}  // namespace ssarecomp
