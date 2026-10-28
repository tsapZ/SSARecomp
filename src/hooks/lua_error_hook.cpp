// The game's Lua 5.1 raising an error: sub_8211A568 is luaD_throw(L, errcode). Most errors happen
// inside a pcall (L->errorJmp, +112, set), which longjmps back and goes on; without one Lua calls
// the panic function and then exit(1). This logs each distinct message once (Lua's text, the value
// on top of the Lua stack), so a problem report shows which script failed, then runs the original.
// Found for SSARecomp #42: loading Oilspill Island the game's lua\utilities.lua:96 errors inside a
// pcall; before setjmp/longjmp were set up in config/default.toml the longjmp didn't jump and the
// throw went on into exit(1).
//
// lua_State (Lua 5.1, 32-bit): +6 status, +8 top, +12 base, +16 l_G, +112 errorJmp.

#include <cstdint>
#include <mutex>
#include <set>
#include <string>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/context.h>

namespace {

uint32_t Load32(uint8_t* base, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(base + address);
}

// The string a TValue at `value` holds, for either TValue size (8 bytes with float numbers,
// 16 with doubles): tt 4 is LUA_TSTRING, the TString's text starts 16 bytes in, its length at +12.
std::string StackString(uint8_t* base, uint32_t value) {
  for (uint32_t tt_offset : {4u, 8u}) {
    if (Load32(base, value + tt_offset) != 4) continue;
    const uint32_t ts = Load32(base, value);
    if (ts < 0x40000000 || ts >= 0xA0000000) continue;
    const uint32_t len = Load32(base, ts + 12);
    if (len == 0 || len > 1024) continue;
    return std::string(reinterpret_cast<const char*>(base + ts + 16), len);
  }
  return {};
}

// A script that fails every frame would otherwise fill the log.
constexpr size_t kMaxMessages = 32;

void LogLuaError(uint8_t* base, uint32_t L, uint32_t errcode) {
  static std::mutex mutex;
  static std::set<std::string> seen;
  const bool protected_call = Load32(base, L + 112) != 0;
  const uint32_t top = Load32(base, L + 8);
  std::string message = StackString(base, top - 8);
  if (message.empty()) message = StackString(base, top - 16);
  if (message.empty()) message = "(no message)";
  {
    std::lock_guard lock(mutex);
    if (seen.size() >= kMaxMessages || !seen.insert(message).second) return;
  }
  if (protected_call) {
    REXLOG_INFO("Lua: error {} inside a pcall (the game goes on): {}", errcode, message);
  } else {
    REXLOG_ERROR("Lua: error {} with no pcall around it (the game exits): {}", errcode, message);
  }
}

}  // namespace

extern "C" void __imp__sub_8211A568(PPCContext& __restrict ctx, uint8_t* base);

REX_HOOK_RAW(sub_8211A568) {
  // Done before the original, which longjmps out past this frame.
  LogLuaError(base, ctx.r3.u32, ctx.r4.u32);
  __imp__sub_8211A568(ctx, base);
}
