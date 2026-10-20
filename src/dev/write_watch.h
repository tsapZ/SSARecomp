// Finds which game function writes a guest address, without a debugger (input script `watch` /
// `unwatch`, src/dev/input_script.h). A hardware write breakpoint (debug register 0) goes on every
// thread of the process; a vectored exception handler notes where each write came from, and
// StopWriteWatch names those places by guest function through PPCFuncMappings (host function
// containing the instruction -> its guest address). Developer tool for reverse engineering;
// Windows only.

#pragma once

#include <cstdint>
#include <optional>

namespace rex::memory {
class Memory;
}

namespace ssarecomp {

// Watches 4 bytes at a virtual guest address; with `value`, only writes that leave that
// big-endian word there count. Each hit keeps the call chain (host frames, named by guest function
// in the log). Threads started later are not covered.
// `reads`: reads count too (debug register mode read/write instead of write).
// `snapshot` / `snapshot_words`: at the first counted hit, copy that much guest memory (e.g. the
// text a parser is reading), logged as text and words by StopWriteWatch.
void StartWriteWatch(rex::memory::Memory* memory, uint32_t guest_address,
                     std::optional<uint32_t> value = std::nullopt, bool reads = false,
                     uint32_t snapshot = 0, uint32_t snapshot_words = 0);

// Removes the breakpoint and logs every writer seen since StartWriteWatch.
void StopWriteWatch();

}  // namespace ssarecomp
