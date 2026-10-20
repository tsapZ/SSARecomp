#pragma once

// The game's own text (rom/text/<language>/global.lxb), for the portal menu's names of magic items
// and adventure packs in the game's language (see game_text.cpp).

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ssarecomp {

// Loads the text table for the language the game runs in (user_language) from `text_root`
// (rom/text) and registers the portal menu's figure name provider. Call once, after the language
// is applied.
void InstallGameText(const std::filesystem::path& text_root);

// One entry of the loaded table by its key ("Anvil"), as UTF-8; nullopt if there is none.
std::optional<std::string> GameText(std::string_view key);

}  // namespace ssarecomp
