// The mark-and-flag part of the animation race guard (anim_race_guard_hook.cpp), kept apart from
// the rex hooks so a test can run it without the game.
//
// A guarded function on another thread stores its thread's mark (Enter) and then looks at the
// loading thread's flag; the loading thread sets the flag, makes every core drain its store buffer
// (FlushStoreBuffers) and looks at the marks (NoneInside). Draining every core is what lets the hot
// path keep plain stores and loads: a thread is either marked by then, or its next look at the flag
// sees it set.
//
// A kernel may refuse both membarrier commands, or a sandbox may block them. A fence on one thread
// cannot reach the others, so with plain marks on the other side the two can miss each other (a
// mark not published yet, a flag not published yet). Every side then fences between its own store
// and its look at the other side, which orders that pair on every architecture. The mode is picked
// once at startup, before the threads exist, so both sides agree on it.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

#if defined(_WIN32)
#include <windows.h>  // FlushProcessWriteBuffers (the build defines WIN32_LEAN_AND_MEAN and NOMINMAX)
#elif defined(__linux__)
#include <linux/membarrier.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace anim_guard {

enum class FlushMode {
  kPrivateExpedited,  // a process-wide membarrier, or FlushProcessWriteBuffers on Windows
  kGlobal,            // the system-wide membarrier, when the process-wide one is not usable
  kFence              // neither is usable: every side fences its own store and load
};

inline FlushMode ChooseFlushMode() {
#if defined(_WIN32)
  return FlushMode::kPrivateExpedited;  // Windows flushes with FlushProcessWriteBuffers
#elif defined(__linux__)
  const int supported = static_cast<int>(syscall(SYS_membarrier, MEMBARRIER_CMD_QUERY, 0));
  if (supported > 0) {
    if ((supported & MEMBARRIER_CMD_PRIVATE_EXPEDITED) != 0 &&
        syscall(SYS_membarrier, MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED, 0) == 0) {
      return FlushMode::kPrivateExpedited;
    }
    if ((supported & MEMBARRIER_CMD_GLOBAL) != 0) return FlushMode::kGlobal;
  }
  return FlushMode::kFence;
#else
  return FlushMode::kFence;
#endif
}

// Fixed before the threads start, so every side reads the same value without a transition to race
// with.
inline const FlushMode g_flush_mode = ChooseFlushMode();

// True while FlushStoreBuffers can make the other cores drain their store buffers.
inline bool ProcessWideFlush() { return g_flush_mode != FlushMode::kFence; }

// Orders the caller's store before its following loads for the other side to see. Returns false
// only when a command that was reported as usable failed, in which case the other cores were not
// drained and the caller must not run the guarded pass this time.
inline bool FlushStoreBuffers() {
#if defined(_WIN32)
  if (g_flush_mode == FlushMode::kPrivateExpedited) {
    FlushProcessWriteBuffers();
    return true;
  }
#elif defined(__linux__)
  if (g_flush_mode == FlushMode::kPrivateExpedited &&
      syscall(SYS_membarrier, MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0) == 0) {
    return true;
  }
  if (g_flush_mode == FlushMode::kGlobal && syscall(SYS_membarrier, MEMBARRIER_CMD_GLOBAL, 0) == 0) {
    return true;
  }
  if (g_flush_mode != FlushMode::kFence) return false;
#endif
  std::atomic_thread_fence(std::memory_order_seq_cst);  // the fence fallback, or this thread's share of it
  return true;
}

// One mark per thread that ever entered a guarded function, each in its own cache line. Threads past
// kSlots (the game has about 20) share the last one with locked adds.
constexpr int kSlots = 64;
struct alignas(64) Slot {
  std::atomic<uint32_t> inside{0};
};

inline Slot g_slots[kSlots];
inline std::atomic<int> g_slot_count{0};
inline thread_local Slot* t_slot = nullptr;

inline Slot* MySlot() {
  if (!t_slot) {
    const int index = g_slot_count.fetch_add(1);
    t_slot = &g_slots[index < kSlots - 1 ? index : kSlots - 1];
  }
  return t_slot;
}

inline bool Shared(const Slot* slot) { return slot == &g_slots[kSlots - 1]; }

inline void Enter(Slot* slot) {
  if (Shared(slot)) {
    slot->inside.fetch_add(1);
  } else {
    slot->inside.store(1, std::memory_order_relaxed);
    if (ProcessWideFlush()) {
      std::atomic_signal_fence(std::memory_order_seq_cst);  // the compiler keeps the store before the load
    } else {
      // Nothing drains the other cores' store buffers, so this mark and the caller's look at the
      // flag must be ordered here; the loading thread fences the same way before reading the marks.
      std::atomic_thread_fence(std::memory_order_seq_cst);
    }
  }
}

inline void Leave(Slot* slot) {
  if (Shared(slot)) {
    slot->inside.fetch_sub(1);
  } else {
    slot->inside.store(0, std::memory_order_release);
  }
}

// True when no other thread is marked (the loading thread calls it after FlushStoreBuffers).
inline bool NoneInside() {
  const int used = std::min(g_slot_count.load(), kSlots);
  for (int i = 0; i < used; ++i) {
    if (g_slots[i].inside.load(std::memory_order_acquire) != 0) return false;
  }
  return g_slots[kSlots - 1].inside.load(std::memory_order_acquire) == 0;
}

}  // namespace anim_guard
