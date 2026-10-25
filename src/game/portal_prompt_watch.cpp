// Opens the portal menu when the game asks a player for a Skylander, and the portal check when it
// says there is too much on the portal.
//
// The prompt text is chosen every frame by sub_827E78F0 (called from sub_827E8CA8, the update of
// the object below; found 2026-10-20 by a read watch on the prompt's text, which the text drawing
// reads through the hash 0x049B83C4 for player 1 and 0x9D92D27E for player 2, and the code that
// loads those hashes). It works on one object, a function-local static of sub_82205D18 at
// 0x82C569B0 (guard word at 0x82C56AC0), so its address is fixed:
//   +56   mode: 2 while the portal screen is up, 4 while a figure comes in, 0 otherwise (measured:
//         2 at the "place a Skylander" screen, 4 three seconds after placing Spyro, then 0 in
//         play; one figure change runs 1, 2, 3, 4, 5, 0)
//
// Which screen it is, the watch reads from the text the game shows rather than from the
// selector's fields, because the selector has more cases than they tell apart: with the mode at
// 2, "too many toys" (an item and an adventure pack on the portal) leaves the screen field +52 at
// 0 as for the request, and player 2's figure bytes empty, so reading those fields took it for
// player 2's request (seen 2026-10-20). The selector shows its texts with sub_82695070 on the
// HUD's portal message, +3260 of the HUD (a function-local static of sub_82204058 at 0x82C56AF8),
// which hands the text's hash to sub_8267B808; that stores it at +432 of the text element that
// +376 of the message points to. The hashes are CRC-32s of global.lxb keys (game_text.cpp); the
// texts below are the German table's, and the selector loads each of them:
//   0x049B83C4  "Spieler 1: Bitte setze einen Skylander auf das Portal of Power."
//   0x9D92D27E  "Spieler 2: ..." (after player 2 pressed A to join)
//   0xCE3BDD24  "Es befinden sich zu viele Skylander auf dem Portal of Power."
//   0xC23393BE  "Zu viele Spielzeuge auf dem Portal of Power." (different kinds, e.g. an item and
//               an adventure pack), 0xA66A468E "magische Objekte", 0xF568FF72 "Haustiere",
//               0x4F826D46 "Ortselemente" (more than one of a kind)
// Measured with each of them on screen (2026-10-20); the hash is 0 in play.
//
// Polling instead of a mid-asm hook keeps the generated code untouched and costs a few guest reads
// every 30 ms. It only reads; both objects are in the executable's data section, which is always
// mapped, and the text element is a heap object, so its address is checked before it is read.

#include "portal_prompt_watch.h"

#include <atomic>
#include <chrono>
#include <cstdint>
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
constexpr uint32_t kModePrompt = 2;
constexpr uint32_t kHudPortalMessage = 0x82C56AF8 + 3260;
constexpr uint32_t kMessageTextElement = 376;
constexpr uint32_t kTextElementHash = 432;

constexpr uint32_t kTextAskPlayer1 = 0x049B83C4;
constexpr uint32_t kTextAskPlayer2 = 0x9D92D27E;
constexpr uint32_t kTextTooManySkylanders = 0xCE3BDD24;
// "304_PortalError_NeedsRemoval": "Dieser Skylander braucht eine Pause. Bitte entferne den
// Skylander vom Portal of Power." (global.lxb), the game's text when a Skylander is out of health.
// The portal menu opens then, to swap it for another (the owner, 2026-10-25: in every game, as in
// SuperChargers). The text doesn't name the player; the menu is player 1's.
constexpr uint32_t kTextNeedsBreak = 0x8F980361;
constexpr uint32_t kTextsTooManyToys[] = {0xC23393BE, 0xA66A468E, 0xF568FF72, 0x4F826D46};

// The portal check's reasons (Swap Force's EMagicMomentReason values, which the portal menu
// library uses).
constexpr uint32_t kCheckReasonTooManySkylanders = 1;
constexpr uint32_t kCheckReasonTooManyItems = 2;
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

uint32_t Load(rex::memory::Memory* memory, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual<const uint8_t*>(address));
}

// Whether the 4 bytes at `address` are committed, readable guest memory (a read anywhere else
// would end the game with an access violation).
bool Readable(rex::memory::Memory* memory, uint32_t address) {
  rex::memory::BaseHeap* heap = memory->LookupHeap(address);
  rex::memory::HeapAllocationInfo info{};
  return heap && heap->QueryRegionInfo(address, &info) &&
         (info.state & rex::memory::kMemoryAllocationCommit) &&
         (info.protect & rex::memory::kMemoryProtectRead) &&
         (address & 3) == 0;
}

// The hash of the text on the game's portal screen, 0 if there is none.
uint32_t ShownText(rex::memory::Memory* memory) {
  const uint32_t element = Load(memory, kHudPortalMessage + kMessageTextElement);
  if (!element || !Readable(memory, element + kTextElementHash)) return 0;
  return Load(memory, element + kTextElementHash);
}

struct Screen {
  std::optional<uint32_t> asks;          // the player (guest user index) asked for a Skylander
  std::optional<uint32_t> check_reason;  // a "too many" screen: the portal check's reason
};

Screen CurrentScreen(rex::memory::Memory* memory) {
  const uint32_t mode = Load(memory, kPromptObject + kModeOffset);
  g_active.store(mode != 0 && mode < 16);
  if (mode != g_last_mode) {
    // The mode sequence of one figure change.
    REXLOG_DEBUG("Portal prompt: mode {} -> {}", g_last_mode, mode);
    g_last_mode = mode;
  }
  Screen screen;
  if (mode != kModePrompt) return screen;
  const uint32_t text = ShownText(memory);
  if (text == kTextAskPlayer1) screen.asks = 0u;
  if (text == kTextAskPlayer2) screen.asks = 1u;
  if (text == kTextNeedsBreak) screen.asks = 0u;
  static uint32_t last_text = 0;
  if (text != last_text && text == kTextNeedsBreak) skylanders::PlayerSkylanderTired(0);
  last_text = text;
  if (text == kTextTooManySkylanders) screen.check_reason = kCheckReasonTooManySkylanders;
  for (uint32_t toys : kTextsTooManyToys) {
    if (text == toys) screen.check_reason = kCheckReasonTooManyItems;
  }
  return screen;
}

void Watch(rex::memory::Memory* memory) {
  std::optional<uint32_t> waiting_for;  // player whose prompt is up
  Clock::time_point since{};
  bool opened = false;
  std::optional<uint32_t> error;  // the check reason of the error screen that is up
  std::optional<Clock::time_point> error_since, check_shown;
  while (!g_stop.load()) {
    const Screen screen = CurrentScreen(memory);
    if (screen.check_reason != error) {
      if (check_shown) skylanders::ClosePortalCheck();
      error = screen.check_reason;
      error_since = Clock::now();
      check_shown.reset();
    }
    if (error) {
      const auto now = Clock::now();
      if (now - *error_since >= kErrorDelay && (!check_shown || now - *check_shown >= kErrorRepeat)) {
        REXLOG_INFO("Portal prompt: the game shows \"too many {} on the portal\"",
                    *error == kCheckReasonTooManySkylanders ? "Skylanders" : "toys");
        skylanders::ShowPortalCheck(*error);
        check_shown = now;
      }
    }
    if (screen.asks != waiting_for) {
      if (waiting_for && opened) skylanders::CloseAutoOpenedPortalMenu();
      waiting_for = screen.asks;
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
