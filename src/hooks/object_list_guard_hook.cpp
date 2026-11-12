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
// guard keeps it so on the first frames, before the game ever wrote it. sub_82289530, which goes
// on with the walk from 0x82BB4F50, gets the same guard (NullResumeNodeHook, 2026-11-12): with the
// list not made yet, the sentinel's own next is 0.
//
// The crash of #32 itself (a read of guest 0x368, seen under RenderDoc and again 2026-10-25 with
// an HD texture pack, both times when the game ran slowly): sub_82631098 runs a node of the same
// tree, the one in the global 0x82BB4F4C (lwz r31,20300(r27) at 0x826310C0), and calls
// sub_826326B8 first, which reads that node's +872 (0x368). With the global 0 there is no node to
// run, and the second hook returns at its start, before anything is changed (its counter at
// 0x82BB4FA8 is raised only after that).
//
// A third case of the same walk (Core of Light #76, a player's report of 2026-11-12, 0.5.0-beta.6,
// Level_021, a minute after the level loaded): sub_829B71D0, run for a node of the tree, calls the
// sixth virtual function of the node's member at +240. That member was already freed: its first
// word, the vtable, was a heap address (45249480) and the slot read from it 0x18C, so the call went
// to guest 0x18C ("Call to invalid or unregistered function"). DanglingMemberHook skips the call
// when the member or its vtable isn't in the game's image.

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
constexpr uint32_t kCurrentTreeNode = 0x82BB4F4C;
// The game's image: every vtable of a live guest object is in it.
constexpr uint32_t kImageStart = 0x82000000;
constexpr uint32_t kImageEnd = 0x83000000;

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

// At the start of sub_82289530 (return_on_true): true when there is no list to go on with.
// sub_82289530 goes on with the walk from the node in 0x82BB4F50: it reads that node's next (+4)
// and walks on until the sentinel. In the first frames the sentinel can be in 0x82BB4F50 (put
// there by NullObjectListHook) while the list isn't made yet and the sentinel's own next is still
// 0: the walk then reads guest 0x4 (2026-11-12, a dev kit run, 36 s in on the title screen, right
// after NullObjectListHook's warnings; call chain sub_82289530 <- sub_82631098). Nothing to go on
// with: the sentinel stays in 0x82BB4F50, so the caller's loop ends at once.
bool NullResumeNodeHook(PPCRegister& r3) {
  (void)r3;  // the node comes from the global, not an argument
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  const uint32_t node = rex::memory::load_and_swap<uint32_t>(base + kCurrentNode);
  if (node != 0 && rex::memory::load_and_swap<uint32_t>(base + node + 4) != 0) return false;
  rex::memory::store_and_swap<uint32_t>(base + kCurrentNode, kListSentinel);
  static std::atomic<int> logged{0};
  if (logged.fetch_add(1) < 3) {
    REXLOG_WARN("Object list: sub_82289530 found no list to go on with (node {:08X}); skipped this frame "
                "(SSARecomp #32)", node);
  }
  return true;
}

// At the start of sub_82631098 (return_on_true): true when the node to run (0x82BB4F4C) is 0.
bool NullTreeNodeHook(PPCRegister& r3) {
  (void)r3;  // the node comes from the global, not an argument
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  if (rex::memory::load_and_swap<uint32_t>(base + kCurrentTreeNode) != 0) return false;
  static std::atomic<int> logged{0};
  if (logged.fetch_add(1) < 3) {
    REXLOG_WARN("Object tree: sub_82631098 found no node at 0x82BB4F4C; skipped this frame (SSARecomp #32)");
  }
  return true;
}

// At the start of sub_829B71D0 (return_on_true): true when its object's member at +240 is gone. The
// function calls that member's sixth virtual function (lwz r3,240(r3); lwz r11,0(r3); lwz r10,24(r11);
// bctrl) and then hands the object on (sub_82269B90 or sub_8226A5D0 by the answer). A member whose
// vtable word isn't in the image was freed (or never made): skipping the function for this frame
// leaves the object alone instead of calling into garbage.
bool DanglingMemberHook(PPCRegister& r3) {
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  const uint32_t member = r3.u32 ? rex::memory::load_and_swap<uint32_t>(base + r3.u32 + 240) : 0;
  const uint32_t vtable = member ? rex::memory::load_and_swap<uint32_t>(base + member) : 0;
  if (vtable >= kImageStart && vtable < kImageEnd) return false;
  static std::atomic<int> logged{0};
  if (logged.fetch_add(1) < 3) {
    REXLOG_WARN("Object tree: sub_829B71D0's object {:08X} has a member {:08X} without a vtable ({:08X}); "
                "skipped (Core of Light #76, SSARecomp #32's family)", r3.u32, member, vtable);
  }
  return true;
}
