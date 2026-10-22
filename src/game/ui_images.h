#pragma once

// The game's own pictures for the portal menu: the HUD portrait of every Skylander and the element
// symbols, read from the disc's uigameimage.str (see ui_images.cpp).

#include <filesystem>

#include "portal_rex/settings_menu.h"

namespace ssarecomp {

// Loads `archive` (rom/uigameimage.str) on a background thread and registers the portal menu's
// figure and art picture providers. Until it is loaded (or if it can't be read) the menu draws its
// own symbols. Call once.
void InstallUiImages(const std::filesystem::path& archive);

// The controls page's controller picture (the start menu's pad, served as "controller:xbox360")
// and the buttons on it, for skylanders::SetControllerPicture.
skylanders::ControllerPicture ControllerPictureLayout();

}  // namespace ssarecomp
