#include "write_watch.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <optional>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#endif

#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/xmemory.h>

namespace ssarecomp {
namespace {

#ifdef _WIN32

// The call chain of each hit (the instruction after the write first), written by the exception
// handler (no locks or logging there: it runs inside whatever game code did the write).
constexpr size_t kMaxHits = 2048;
constexpr int kFrames = 6;
struct Hit {
  uintptr_t frames[kFrames];
  uint64_t regs[16];  // rax rcx rdx rbx rsp rbp rsi rdi r8-r15 at the hit
  // The guest registers, if a host register points at the recompiled code's PPCContext (it
  // starts with r3, r0, r1, r2, r4 ... r31, 8 bytes each); the low words, else 0.
  uint32_t guest[32];
  int guest_from = -1;  // which host register held the context
  uint32_t stack[256];  // big-endian words from the guest stack pointer (r1) up
};

// Copies `size` bytes from `from`, or returns false if that memory can't be read.
bool SafeCopy(void* to, uint64_t from, size_t size) {
  __try {
    memcpy(to, reinterpret_cast<const void*>(from), size);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
std::array<Hit, kMaxHits> g_hits{};
std::atomic<size_t> g_hit_count{0};
std::atomic<size_t> g_write_count{0};
std::atomic<uintptr_t> g_watched{0};
std::atomic<bool> g_filter{false};
std::atomic<uint32_t> g_value{0};  // as the bytes read little-endian
PVOID g_handler = nullptr;
uint32_t g_guest_address = 0;
uint64_t g_membase = 0;  // host address of guest virtual 0
uint32_t g_snapshot = 0, g_snapshot_words = 0;
std::vector<uint8_t> g_snapshot_data;  // sized at start, filled once in the handler
std::atomic<bool> g_snapshot_taken{false};
std::thread g_refresher;  // puts the breakpoint on threads started after StartWriteWatch
bool g_reads = false;      // debug register mode 11 (read or write) instead of 01 (write)

// Walks the stack from the exception context with the binary's own unwind data (no symbols needed).
void Unwind(CONTEXT ctx, uintptr_t* frames) {
  for (int i = 0; i < kFrames; ++i) {
    frames[i] = uintptr_t(ctx.Rip);
    DWORD64 image_base = 0;
    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &image_base, nullptr);
    if (!fn) {
      // A leaf function: the return address is on top of the stack.
      ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
      ctx.Rsp += 8;
    } else {
      PVOID handler_data = nullptr;
      DWORD64 establisher = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, ctx.Rip, fn, &ctx, &handler_data,
                       &establisher, nullptr);
    }
    if (!ctx.Rip) {
      for (int k = i + 1; k < kFrames; ++k) frames[k] = 0;
      return;
    }
  }
}

// Unwind may walk into frames without unwind data and read garbage; a fault ends the chain.
void UnwindSafe(const CONTEXT& ctx, uintptr_t* frames) {
  __try {
    Unwind(ctx, frames);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

LONG CALLBACK OnException(PEXCEPTION_POINTERS info) {
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
  CONTEXT* ctx = info->ContextRecord;
  if (!(ctx->Dr6 & 1)) return EXCEPTION_CONTINUE_SEARCH;
  // Ours even while the watch is being taken down: a trap left unhandled would end the process.
  ctx->Dr6 = 0;
  const uintptr_t watched = g_watched.load();
  if (!watched) return EXCEPTION_CONTINUE_EXECUTION;
  g_write_count.fetch_add(1);
  if (g_filter.load() && *reinterpret_cast<const uint32_t*>(watched) != g_value.load()) {
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  const size_t i = g_hit_count.fetch_add(1);
  if (i == 0 && !g_snapshot_data.empty() &&
      SafeCopy(g_snapshot_data.data(), g_membase + g_snapshot, g_snapshot_data.size())) {
    g_snapshot_taken.store(true);
  }
  if (i < kMaxHits) {
    UnwindSafe(*ctx, g_hits[i].frames);
    const DWORD64 regs[16] = {ctx->Rax, ctx->Rcx, ctx->Rdx, ctx->Rbx, ctx->Rsp, ctx->Rbp,
                              ctx->Rsi, ctx->Rdi, ctx->R8,  ctx->R9,  ctx->R10, ctx->R11,
                              ctx->R12, ctx->R13, ctx->R14, ctx->R15};
    for (int r = 0; r < 16; ++r) g_hits[i].regs[r] = regs[r];
    g_hits[i].guest_from = -1;
    // A context candidate: a host (not guest) pointer, 64-byte aligned like PPCContext. The
    // stack pointer of a guest thread (r1) lies in guest memory, which tells it apart.
    for (int r : {7, 6, 3, 5, 12, 13, 14, 15}) {  // rdi rsi rbx rbp r12-r15
      const uint64_t p = regs[r];
      if (!p || (p & 0x3F) || (p >= g_membase && p < g_membase + 0x100000000ull)) continue;
      uint64_t raw[32];
      if (!SafeCopy(raw, p, sizeof(raw))) continue;
      const uint32_t r1 = uint32_t(raw[2]);
      if (r1 < 0x40000000u || r1 >= 0x80000000u) continue;  // guest stacks are at 0x7xxxxxxx
      static const int kOrder[32] = {3, 0, 1, 2, 4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                                     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
      for (int k = 0; k < 32; ++k) g_hits[i].guest[kOrder[k]] = uint32_t(raw[k]);
      g_hits[i].guest_from = r;
      if (!SafeCopy(g_hits[i].stack, g_membase + r1, sizeof(g_hits[i].stack))) {
        memset(g_hits[i].stack, 0, sizeof(g_hits[i].stack));
      }
      break;
    }
  }
  return EXCEPTION_CONTINUE_EXECUTION;
}

// Sets (address != 0) or clears debug register 0 on every other thread of the process.
void SetBreakpointOnAllThreads(uintptr_t address, bool quiet = false) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return;
  const DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  int threads = 0;
  for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self) continue;
    HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                               FALSE, entry.th32ThreadID);
    if (!thread) continue;
    if (SuspendThread(thread) != DWORD(-1)) {
      CONTEXT ctx{};
      ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
      if (GetThreadContext(thread, &ctx)) {
        if (address) {
          ctx.Dr0 = address;
          // L0 on, RW0 = 01 (write) or 11 (read or write), LEN0 = 11 (4 bytes).
          ctx.Dr7 = (ctx.Dr7 & ~DWORD64(0xF0003)) | 1 | ((g_reads ? 3u : 1u) << 16) | (3u << 18);
        } else {
          ctx.Dr0 = 0;
          ctx.Dr7 &= ~DWORD64(0xF0003);
        }
        if (SetThreadContext(thread, &ctx)) ++threads;
      }
      ResumeThread(thread);
    }
    CloseHandle(thread);
  }
  CloseHandle(snapshot);
  if (!quiet) {
    REXLOG_INFO("Write watch: debug register {} on {} threads", address ? "set" : "cleared",
                threads);
  }
}

// The guest function whose host code contains `rip`: the mapping with the highest host address
// not above it.
std::pair<uint32_t, uintptr_t> GuestFunctionAt(uintptr_t rip) {
  static std::vector<std::pair<uintptr_t, uint32_t>> table;
  if (table.empty()) {
    for (const PPCFuncMapping* m = PPCFuncMappings; m->host; ++m) {
      table.emplace_back(reinterpret_cast<uintptr_t>(m->host), uint32_t(m->guest));
    }
    std::sort(table.begin(), table.end());
  }
  auto it = std::upper_bound(table.begin(), table.end(), std::make_pair(rip, UINT32_MAX));
  if (it == table.begin()) return {0, 0};
  --it;
  return {it->second, rip - it->first};
}

#endif  // _WIN32

}  // namespace

void StartWriteWatch(rex::memory::Memory* memory, uint32_t guest_address,
                     std::optional<uint32_t> value, bool reads, uint32_t snapshot,
                     uint32_t snapshot_words) {
#ifdef _WIN32
  if (!memory || g_watched.load()) return;
  const auto host = reinterpret_cast<uintptr_t>(memory->TranslateVirtual<uint8_t*>(guest_address));
  g_guest_address = guest_address;
  g_membase = reinterpret_cast<uint64_t>(memory->virtual_membase());
  g_hit_count.store(0);
  g_write_count.store(0);
  g_filter.store(value.has_value());
  g_reads = reads;
  g_snapshot = snapshot;
  g_snapshot_words = snapshot_words;
  g_snapshot_taken.store(false);
  g_snapshot_data.assign(size_t(snapshot_words) * 4, 0);
  // Guest memory is big-endian; the handler compares the raw bytes.
  if (value) g_value.store(_byteswap_ulong(*value));
  if (!g_handler) g_handler = AddVectoredExceptionHandler(1, OnException);
  g_watched.store(host);
  SetBreakpointOnAllThreads(host);
  // Threads the game starts later (loading, rendering) get the breakpoint too.
  g_refresher = std::thread([host] {
    while (g_watched.load() == host) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      if (g_watched.load() == host) SetBreakpointOnAllThreads(host, true);
    }
  });
  REXLOG_INFO("Write watch: watching {} guest {:08X} (host {:X})",
              reads ? "reads and writes of" : "writes to", guest_address, host);
#else
  (void)memory;
  (void)guest_address;
  (void)value;
  (void)reads;
  (void)snapshot;
  (void)snapshot_words;
  REXLOG_WARN("Write watch: only on Windows");
#endif
}

void StopWriteWatch() {
#ifdef _WIN32
  if (!g_watched.load()) return;
  g_watched.store(0);
  if (g_refresher.joinable()) g_refresher.join();
  SetBreakpointOnAllThreads(0);
  const size_t count = std::min(g_hit_count.load(), kMaxHits);
  // Each distinct call chain once, as guest functions.
  std::map<std::string, size_t> chains;
  for (size_t i = 0; i < count; ++i) {
    std::string chain;
    for (uintptr_t frame : g_hits[i].frames) {
      if (!frame) break;
      const auto [guest, offset] = GuestFunctionAt(frame);
      chain += fmt::format("{}sub_{:08X}+0x{:X}", chain.empty() ? "" : " <- ", guest, offset);
    }
    ++chains[chain];
  }
  REXLOG_INFO("Write watch: {} writes to {:08X}, {} counted, {} call chains", g_write_count.load(),
              g_guest_address, g_hit_count.load(), chains.size());
  for (const auto& [chain, n] : chains) REXLOG_INFO("Write watch:   {} x {}", n, chain);
  if (g_snapshot_taken.load()) {
    // As text (other bytes shown as '.'), 96 characters per line.
    for (size_t o = 0; o < g_snapshot_data.size(); o += 96) {
      std::string text;
      for (size_t k = o; k < g_snapshot_data.size() && k < o + 96; ++k) {
        const uint8_t c = g_snapshot_data[k];
        text += c >= 32 && c < 127 ? char(c) : '.';
      }
      REXLOG_INFO("Write watch:   text {:08X}: {}", g_snapshot + uint32_t(o), text);
    }
  }
  // The registers of the first few hits, as guest addresses where they point into guest memory
  // (e.g. the source of a copy).
  static const char* const kNames[16] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                                         "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"};
  for (size_t i = 0; i < std::min<size_t>(count, 4); ++i) {
    std::string line;
    for (int r = 0; r < 16; ++r) {
      const uint64_t v = g_hits[i].regs[r];
      const bool guest = v >= g_membase && v < g_membase + 0x100000000ull;
      line += guest ? fmt::format(" {}=g:{:08X}", kNames[r], v - g_membase)
                    : fmt::format(" {}={:X}", kNames[r], v);
    }
    REXLOG_INFO("Write watch:   hit {}:{}", i, line);
    if (g_hits[i].guest_from >= 0) {
      std::string guest;
      for (int r = 0; r < 32; ++r) guest += fmt::format(" r{}={:08X}", r, g_hits[i].guest[r]);
      REXLOG_INFO("Write watch:   hit {} guest (context in {}):{}", i, kNames[g_hits[i].guest_from],
                  guest);
      if (i == 0) {
        for (int w = 0; w < 256; w += 8) {
          std::string line;
          for (int k = w; k < w + 8; ++k) line += fmt::format(" {:08X}", _byteswap_ulong(g_hits[i].stack[k]));
          REXLOG_INFO("Write watch:   stack {:08X}:{}", g_hits[i].guest[1] + 4 * w, line);
        }
      }
    }
  }
#endif
}

}  // namespace ssarecomp
