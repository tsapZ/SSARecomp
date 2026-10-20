// Opens the portal menu when the game asks a player for a Skylander.
//
// The prompt text is chosen every frame by sub_827E78F0 (called from sub_827E8CA8, the update of
// the object below; found 2026-10-20 by a read watch on the prompt's text, which the text drawing
// reads through the hash 0x049B83C4 for player 1 and 0x9D92D27E for player 2, and the code that
// loads those hashes). It works on one object, a function-local static of sub_82205D18 at
// 0x82C569B0 (guard word at 0x82C56AC0), so its address is fixed:
//   +56   mode: 2 while the prompt is up, 4 while a figure comes in, 0 otherwise (measured: 2 at
//         the "place a Skylander" screen, 4 three seconds after placing Spyro, then 0 in play;
//         one figure change runs 1, 2, 3, 4, 5, 0)
//   +52   what the screen is about while the mode is 2: 0 = asking for a Skylander, 1 = "Es
//         befinden sich zu viele Skylander auf dem Portal of Power" (measured with a second
//         Skylander for a single player; the selector's switch on it ends at 3)
//   +32   byte, nonzero once player 1 has a Skylander; +195 another player 1 flag
//   +36   the same for player 2; +194 its other flag
// sub_827E78F0 shows player 1's prompt when the mode is 2 and both player 1 bytes are 0, else
// player 2's when both of theirs are 0; this watch picks the player the same way. On the "too many
// Skylanders" screen it opens the portal check instead, so the player can take one off. Whether
// "too many magic items" also uses +52 = 1 is not checked yet: that screen would open the check
// for Skylanders as well.
//
// Polling instead of a mid-asm hook keeps the generated code untouched and costs a few guest reads
// every 30 ms. It only reads, and the object is in the executable's data section, which is always
// mapped.

#include "portal_prompt_watch.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <thread>

#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/xmemory.h>

#include "portal_rex/portal_menu.h"

namespace ssarecomp {
namespace {

using Clock = std::chrono::steady_clock;

constexpr uint32_t kPromptObject = 0x82C569B0;
constexpr uint32_t kModeOffset = 56;
constexpr uint32_t kScreenOffset = 52;
constexpr uint32_t kScreenAsk = 0;
constexpr uint32_t kScreenTooManySkylanders = 1;
// The portal check's reason for too many Skylanders (Swap Force's EMagicMomentReason value, which
// the portal menu library uses).
constexpr uint32_t kCheckReasonTooManySkylanders = 1;
constexpr uint32_t kPlayer1Offsets[] = {32, 195};
constexpr uint32_t kPlayer2Offsets[] = {36, 194};
constexpr uint32_t kModePrompt = 2;
// How long the game must keep waiting before the menu opens, so it comes up with the prompt
// rather than during a one-frame flicker.
constexpr auto kOpenDelay = std::chrono::milliseconds(100);
// The error screen must stay this long before the portal check opens, and if it was closed with
// the error still up, it comes back after a while.
constexpr auto kErrorDelay = std::chrono::milliseconds(400);
constexpr auto kErrorRepeat = std::chrono::seconds(8);

std::thread g_thread;
std::atomic<bool> g_stop{false};
std::atomic<bool> g_active{false};  // mode != 0 at the last poll
uint32_t g_last_mode = 0;            // watcher thread only; for the debug log

uint32_t ReadField(rex::memory::Memory* memory, uint32_t offset) {
  return rex::memory::load_and_swap<uint32_t>(
      memory->TranslateVirtual<const uint8_t*>(kPromptObject + offset));
}

bool PlayerHasFigure(rex::memory::Memory* memory, const uint32_t (&offsets)[2]) {
  for (uint32_t offset : offsets) {
    if (*memory->TranslateVirtual<const uint8_t*>(kPromptObject + offset) != 0) return true;
  }
  return false;
}

// The player (guest user index) the game is asking for a Skylander, if its prompt is up.
std::optional<uint32_t> CurrentPrompt(rex::memory::Memory* memory) {
  const uint32_t mode = ReadField(memory, kModeOffset);
  g_active.store(mode != 0 && mode < 16);
  if (mode != g_last_mode) {
    // The mode sequence of one figure change, e.g. to find the "too many" screen's value.
    REXLOG_DEBUG("Portal prompt: mode {} -> {}", g_last_mode, mode);
    g_last_mode = mode;
  }
  if (mode != kModePrompt || ReadField(memory, kScreenOffset) != kScreenAsk) return std::nullopt;
  if (!PlayerHasFigure(memory, kPlayer1Offsets)) return 0u;
  if (!PlayerHasFigure(memory, kPlayer2Offsets)) return 1u;
  return std::nullopt;
}

// True while the game shows its "too many Skylanders on the portal" screen.
bool TooManySkylanders(rex::memory::Memory* memory) {
  return ReadField(memory, kModeOffset) == kModePrompt &&
         ReadField(memory, kScreenOffset) == kScreenTooManySkylanders;
}

void Watch(rex::memory::Memory* memory) {
  std::optional<uint32_t> waiting_for;  // player whose prompt is up
  Clock::time_point since{};
  bool opened = false;
  std::optional<Clock::time_point> error_since, check_shown;
  while (!g_stop.load()) {
    if (TooManySkylanders(memory)) {
      const auto now = Clock::now();
      if (!error_since) error_since = now;
      if (now - *error_since >= kErrorDelay && (!check_shown || now - *check_shown >= kErrorRepeat)) {
        REXLOG_INFO("Portal prompt: the game shows \"too many Skylanders on the portal\"");
        skylanders::ShowPortalCheck(kCheckReasonTooManySkylanders);
        check_shown = now;
      }
    } else if (error_since) {
      if (check_shown) skylanders::ClosePortalCheck();
      error_since.reset();
      check_shown.reset();
    }
    const std::optional<uint32_t> player = CurrentPrompt(memory);
    if (player != waiting_for) {
      if (waiting_for && opened) skylanders::CloseAutoOpenedPortalMenu();
      waiting_for = player;
      since = Clock::now();
      opened = false;
    }
    if (waiting_for && !opened && Clock::now() - since >= kOpenDelay) {
      REXLOG_INFO("Portal prompt: the game asks player {} for a Skylander", *waiting_for + 1);
      skylanders::AutoOpenPortalMenu(*waiting_for);
      opened = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
}

}  // namespace

void StartPortalPromptWatch(rex::memory::Memory* memory) {
  if (!memory || g_thread.joinable()) return;
  g_stop.store(false);
  g_thread = std::thread(Watch, memory);
}

bool PortalPromptActive() { return g_active.load(); }

void StopPortalPromptWatch() {
  g_stop.store(true);
  if (g_thread.joinable()) g_thread.join();
}

}  // namespace ssarecomp
