// A guard against a crash that came up about 40 s into a new game in one run of three
// (2026-10-25, resolution scale 1; SSARecomp #32 has the same caller under RenderDoc): a read of
// guest 0x00000004 in sub_822895E0, called from sub_82631098 with r3 = 0.
//
// sub_822895E0 walks a list of game objects from the node in r3: the list's sentinel is the object
// at 0x82B5FAAC, a per-frame budget sits at 0x82BA92F0 (+12 the limit, +20 the count so far), the
// node to resume from at 0x82BB4F54 and the node being run at 0x82BB4F50; it calls the node's
// fourth virtual function and leaves 0x82BB4F50 for its caller. sub_82631098 passes it a manager's
// member (lwz r3,840(r31) at 0x82631280, the same for +844 further on), which can still be 0, and
// the walk's first lwz r3,4(r3) then reads guest 0x4. Its callers then run every node from
// 0x82BB4F50 on until the sentinel (0x82631294-0x826312B0).
//
// With r3 = 0 the hook returns at once (nothing to walk) and puts the sentinel into 0x82BB4F50,
// so the caller's loop ends right away too: after a full pass it holds the sentinel anyway, and the
// guard keeps it so on the first frames, before the game ever wrote it.

#include <atomic>
#include <cstdint>

#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/memory/utils.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

namespace {

constexpr uint32_t kListSentinel = 0x82B5FAAC;
constexpr uint32_t kCurrentNode = 0x82BB4F50;

}  // namespace

// At the start of sub_822895E0 (return_on_true): true when there is no list to walk.
bool NullObjectListHook(PPCRegister& r3) {
  if (r3.u32 != 0) return false;
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  rex::memory::store_and_swap<uint32_t>(base + kCurrentNode, kListSentinel);
  static std::atomic<int> logged{0};
  if (logged.fetch_add(1) < 3) {
    REXLOG_WARN("Object list: sub_822895E0 was given no list (r3 = 0); skipped this frame (SSARecomp #32)");
  }
  return true;
}
