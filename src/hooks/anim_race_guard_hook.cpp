// A guard against a rare crash at boot, 0.4-0.6 s after the FrontEnd level starts loading (2 crashes
// in about 130 boots on 2026-11-05, never seen in the 321 logged runs before): the loading screen
// thread and another thread update the same animations at once.
//
// The loading screen thread runs sub_827F9748: while its state (+20) is 0x3257EBBC it calls the pass
// sub_827F9298, then sub_827F8E30, then sleeps 29 ms (sub_822DCC58). The pass works on the animation
// manager singleton 0x82C590B0 (from sub_8220CB68) through sub_8267FC18, sub_82678120, sub_8267D870
// and, at its end, the update sub_82684AB0 -> sub_82682548 -> sub_826822D8 -> sub_826810A0 for each
// animation. The game has no lock around any of it (the helpers it calls first are getters of static
// singletons). The two crashes:
// - on the loading screen thread in sub_826810A0: an animation's +628 (the next one to play) was
//   non-null at 0x82681118 and null at 0x82681158 with no call in between. sub_826810A0 itself
//   clears +628 once it took it (stw r16 at the end), so another thread ran sub_826810A0 on the same
//   animation at the same time. Besides the pass, sub_826822D8 is reached through sub_82682548 (also
//   from sub_826B98F8), sub_826962A0, sub_826D4788 and sub_827BBEC0, and sub_82684AB0 through
//   sub_82684E78 (called through a pointer). A third crash of this kind (boot 175 of 200 with only
//   the sub_82684878 part of this guard) came while the main thread was loading FrontEnd's files.
// - on the main thread in sub_8289EA20, from its animation update sub_82684878 (seven callers),
//   reading a bit array whose count was 30.0f. A diagnostic build showed the main thread inside
//   sub_82684878 for 17-110 ms at a time during the FrontEnd load while the loading screen thread
//   kept running its passes on the same manager, 6-7 overlaps in every boot.
//
// The guard keeps the pass and every other thread's sub_82684878, sub_82684AB0, sub_82682548 and
// sub_826822D8 apart without a lock that could deadlock:
// - The loading thread says it wants to run (g_want), waits until no other thread is inside one of
//   those functions and runs its pass. If one stays inside for kWorkerWaitMs it skips this pass (the
//   loop then checks its state and sleeps as usual).
// - Another thread entering one of them while the loading thread wants to run or runs waits until
//   the pass is done, at most kMainWaitMs, then goes on anyway. Calls nested inside a guarded one
//   (or inside the pass) go straight through.
// So a pass waits for at most the one call that was already running, and the other threads for at
// most one pass. sub_826822D8 runs for every animation in every frame, so the other threads' side
// has no locked instruction: each thread marks itself in its own cache line with plain stores, and
// the loading thread, after saying it wants to run, makes every core drain its store buffer
// (FlushProcessWriteBuffers, once per pass and only while a loading screen runs) before it reads the
// marks: a thread either is marked by then, or its next look at g_want sees it set. (A first version
// with an atomic add and subtract per call cost about 0.6 % FPS in Level_036.)
// anim_race_guard = false turns it off. At the end of each loading screen (sub_827F9748 returning)
// one log line says how often the two met and which functions other threads entered meanwhile.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include <windows.h>  // Sleep, FlushProcessWriteBuffers (the build defines WIN32_LEAN_AND_MEAN and NOMINMAX)

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

REXCVAR_DEFINE_BOOL(anim_race_guard, true, "Game",
                    "Keep the loading screen's animation update apart from the game's (a rare crash "
                    "at boot otherwise).");

namespace {

constexpr int kWorkerWaitMs = 250;
constexpr int kMainWaitMs = 50;

std::atomic<bool> g_want{false};       // the loading thread wants to run its pass, or runs it
std::atomic<bool> g_loop_running{false};
thread_local int t_depth = 0;          // guarded functions nested on this thread
thread_local bool t_in_pass = false;   // this thread is the loading thread, inside its pass

// One mark per thread that ever entered a guarded function, each in its own cache line. Threads past
// kSlots (the game has about 20) share the last one with locked adds.
constexpr int kSlots = 64;
struct alignas(64) Slot {
  std::atomic<uint32_t> inside{0};
};
Slot g_slots[kSlots];
std::atomic<int> g_slot_count{0};
thread_local Slot* t_slot = nullptr;

Slot* MySlot() {
  if (!t_slot) {
    const int index = g_slot_count.fetch_add(1);
    t_slot = &g_slots[index < kSlots - 1 ? index : kSlots - 1];
  }
  return t_slot;
}

bool Shared(const Slot* slot) { return slot == &g_slots[kSlots - 1]; }

void Enter(Slot* slot) {
  if (Shared(slot)) {
    slot->inside.fetch_add(1);
  } else {
    slot->inside.store(1, std::memory_order_relaxed);
    std::atomic_signal_fence(std::memory_order_seq_cst);  // the compiler keeps the store before the load
  }
}

void Leave(Slot* slot) {
  if (Shared(slot)) {
    slot->inside.fetch_sub(1);
  } else {
    slot->inside.store(0, std::memory_order_release);
  }
}

// True when no other thread is marked (the loading thread calls it after FlushProcessWriteBuffers).
bool NoneInside() {
  const int used = std::min(g_slot_count.load(), kSlots);
  for (int i = 0; i < used; ++i) {
    if (g_slots[i].inside.load(std::memory_order_acquire) != 0) return false;
  }
  return g_slots[kSlots - 1].inside.load(std::memory_order_acquire) == 0;
}

enum Entry { k84878, k84AB0, k82548, k822D8, kEntries };
constexpr const char* kEntryNames[kEntries] = {"sub_82684878", "sub_82684AB0", "sub_82682548", "sub_826822D8"};

// Counted per loading screen, logged and reset when its loop ends.
std::atomic<uint32_t> g_passes{0};
std::atomic<uint32_t> g_pass_waits{0};     // a pass waited for another thread
std::atomic<uint32_t> g_pass_skips{0};     // a pass gave up after kWorkerWaitMs
std::atomic<uint32_t> g_other_waits{0};    // another thread waited for a pass
std::atomic<uint32_t> g_other_timeouts{0}; // another thread went on after kMainWaitMs
std::atomic<uint32_t> g_entries[kEntries] = {};  // outermost entries by other threads while the loop ran

bool Expired(std::chrono::steady_clock::time_point start, int ms) {
  return std::chrono::steady_clock::now() - start > std::chrono::milliseconds(ms);
}

struct Nested {
  Nested() { ++t_depth; }
  ~Nested() { --t_depth; }
};

// Runs one of the guarded functions on a thread other than the loading thread's pass.
template <typename Call>
void OtherSide(Entry which, Call&& call) {
  if (t_in_pass || t_depth > 0 || !REXCVAR_GET(anim_race_guard)) {
    Nested nested;
    call();
    return;
  }
  if (g_loop_running.load(std::memory_order_relaxed)) g_entries[which].fetch_add(1, std::memory_order_relaxed);
  Slot* const slot = MySlot();
  for (;;) {
    Enter(slot);
    if (!g_want.load(std::memory_order_relaxed)) break;
    Leave(slot);
    g_other_waits.fetch_add(1, std::memory_order_relaxed);
    const auto start = std::chrono::steady_clock::now();
    bool timed_out = false;
    while (g_want.load()) {
      if (Expired(start, kMainWaitMs)) {
        timed_out = true;
        break;
      }
      std::this_thread::yield();
    }
    if (timed_out) {
      g_other_timeouts.fetch_add(1, std::memory_order_relaxed);
      Enter(slot);
      break;
    }
  }
  struct Unmark {
    Slot* slot;
    ~Unmark() { Leave(slot); }
  } unmark{slot};
  Nested nested;
  call();
}

}  // namespace

extern "C" void __imp__sub_827F9748(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_827F9298(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82684878(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82684AB0(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82682548(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_826822D8(PPCContext& __restrict ctx, uint8_t* base);

// The loading screen thread's loop: runs until the screen is stopped.
REX_HOOK_RAW(sub_827F9748) {
  g_loop_running.store(true);
  __imp__sub_827F9748(ctx, base);
  g_loop_running.store(false);
  const uint32_t passes = g_passes.exchange(0);
  const uint32_t pass_waits = g_pass_waits.exchange(0);
  const uint32_t pass_skips = g_pass_skips.exchange(0);
  const uint32_t other_waits = g_other_waits.exchange(0);
  const uint32_t other_timeouts = g_other_timeouts.exchange(0);
  std::string entered;
  for (int e = 0; e < kEntries; ++e) {
    if (const uint32_t n = g_entries[e].exchange(0)) {
      entered += fmt::format("{}{} x {}", entered.empty() ? "" : ", ", kEntryNames[e], n);
    }
  }
  if (pass_waits + pass_skips + other_waits + other_timeouts == 0 && entered.empty()) return;
  if (entered.empty()) entered = "none";
  if (pass_skips + other_timeouts > 0) {
    REXLOG_WARN("Animation guard: loading screen with {} passes, {} waited and {} skipped for other threads, "
                "other threads waited {} times and went on {} times; entered meanwhile: {}",
                passes, pass_waits, pass_skips, other_waits, other_timeouts, entered);
  } else {
    REXLOG_INFO("Animation guard: loading screen with {} passes, {} waited for other threads, other threads "
                "waited {} times; entered meanwhile: {}",
                passes, pass_waits, other_waits, entered);
  }
}

// The loading screen thread's pass.
REX_HOOK_RAW(sub_827F9298) {
  if (!REXCVAR_GET(anim_race_guard)) {
    __imp__sub_827F9298(ctx, base);
    return;
  }
  g_want.store(true);
  FlushProcessWriteBuffers();
  if (!NoneInside()) {
    const auto start = std::chrono::steady_clock::now();
    int spins = 0;
    while (!NoneInside()) {
      if (Expired(start, kWorkerWaitMs)) {
        g_want.store(false);
        g_pass_skips.fetch_add(1, std::memory_order_relaxed);
        return;
      }
      if (++spins < 200) {
        std::this_thread::yield();
      } else {
        Sleep(1);
      }
    }
    g_pass_waits.fetch_add(1, std::memory_order_relaxed);
  }
  g_passes.fetch_add(1, std::memory_order_relaxed);
  t_in_pass = true;
  struct Done {
    ~Done() {
      t_in_pass = false;
      g_want.store(false);
    }
  } done;
  __imp__sub_827F9298(ctx, base);
}

// The main thread's animation update.
REX_HOOK_RAW(sub_82684878) {
  OtherSide(k84878, [&] { __imp__sub_82684878(ctx, base); });
}

// The manager update the pass ends with, also reached through sub_82684E78.
REX_HOOK_RAW(sub_82684AB0) {
  OtherSide(k84AB0, [&] { __imp__sub_82684AB0(ctx, base); });
}

// The manager's walk over its animations, also called by sub_826B98F8.
REX_HOOK_RAW(sub_82682548) {
  OtherSide(k82548, [&] { __imp__sub_82682548(ctx, base); });
}

// One animation's update (it runs sub_826810A0), also called by sub_826962A0, sub_826D4788 and
// sub_827BBEC0.
REX_HOOK_RAW(sub_826822D8) {
  OtherSide(k822D8, [&] { __imp__sub_826822D8(ctx, base); });
}
