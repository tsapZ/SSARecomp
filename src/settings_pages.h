#pragma once

// The pages of the settings menu (F4 / Back + RB, skylanders-portal's settings_menu): graphics with
// presets, frame rate, game, portal, controls, and the full developer list.

#include <functional>
#include <vector>

#include "portal_rex/settings_menu.h"

namespace ssarecomp {

// `open_all_settings` opens the SDK's list of every setting (the former F4 overlay).
std::vector<skylanders::SettingsPage> BuildSettingsPages(std::function<void()> open_all_settings);

}  // namespace ssarecomp
