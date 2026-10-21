// Skips the startup logo movies and, optionally, the opening movie of a new game.
//
// BinkOpen (sub_829EBEF8, the Bink 1 SDK's; it names "Not a Bink file." at 0x8207B08C) reads the
// 44-byte file header into r1+112 through its read callback and then checks the signature (the
// BIKf..BIKi compares at 0x829EC1CC). The midasm hook at 0x829EC1CC (config/default.toml) runs in
// between. When the header belongs to one of the movies below, recognised by the file-size and
// frame-count fields that are unique to them, the frame count at +8 is set to 1. Bink then plays
// one frame and reports the movie finished, so the game moves on as if it had played out. Same
// approach as GiantsRecomp's and SwapForceRecomp's intro skip.

#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/memory/utils.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

#include "ultrawide_hook.h"

REXCVAR_DEFINE_BOOL(skip_intro, true, "Game",
                    "Skip the Activision, XPEC and Toys for Bob logo movies and the legal notice "
                    "at startup.");
REXCVAR_DEFINE_BOOL(skip_opening_movie, false, "Game",
                    "Skip the opening movie when starting a new game.");

namespace {

struct Movie {
  uint32_t size_field;  // header +4: file size minus 8
  uint32_t frames;      // header +8
  bool opening;         // true: controlled by skip_opening_movie instead of skip_intro
  const char* name;
};

// Header values read from the disc's files (all "BIKi"). The legal notice has one file per
// language, each with its own size.
constexpr Movie kMovies[] = {
    {0x00552788, 311, false, "movies/atvilogo.bik"},   // Activision logo
    {0x0025A2C0, 255, false, "movies/xpeclogo.bik"},   // XPEC logo (the Xbox 360 port's studio)
    {0x00B6B850, 240, false, "movies/tfb.bik"},        // Toys for Bob logo
    {0x000618B0, 66, false, "movies/eng/legal.bik"},
    {0x0006A094, 66, false, "movies/ger/legal.bik"},
    {0x0006565C, 66, false, "movies/fre/legal.bik"},
    {0x00066AC8, 66, false, "movies/spa/legal.bik"},
    {0x0005C2D4, 66, false, "movies/ita/legal.bik"},
    {0x0006DFA0, 66, false, "movies/dut/legal.bik"},
    {0x000655E0, 66, false, "movies/dan/legal.bik"},
    {0x0006680C, 66, false, "movies/fin/legal.bik"},
    {0x000537F4, 66, false, "movies/nor/legal.bik"},
    {0x00064E20, 67, false, "movies/swe/legal.bik"},
    {0x053BF980, 1869, true, "movies/sl_sc01.bik"},    // new-game opening movie (Eon, 62 s)
};

}  // namespace

void BinkHeaderLoaded(PPCRegister& r1) {
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  uint8_t* header = base + r1.u32 + 112;
  // Every movie, skipped or not (ultrawide: bars at the sides while it plays).
  ssarecomp::MovieOpened(rex::memory::load_and_swap<uint32_t>(header + 0x14),
                         rex::memory::load_and_swap<uint32_t>(header + 0x18));
  const bool skip_logos = REXCVAR_GET(skip_intro);
  const bool skip_opening = REXCVAR_GET(skip_opening_movie);
  if (!skip_logos && !skip_opening) {
    return;
  }
  // The file stores these fields little-endian, but Bink's reader has already swapped the header
  // into native (big-endian) words by the time it reaches this point (the signature compares
  // against 0x694B4942 for "BIKi").
  const uint32_t size_field = rex::memory::load_and_swap<uint32_t>(header + 4);
  const uint32_t frames = rex::memory::load_and_swap<uint32_t>(header + 8);
  for (const Movie& movie : kMovies) {
    if (size_field != movie.size_field || frames != movie.frames) {
      continue;
    }
    if (movie.opening ? skip_opening : skip_logos) {
      rex::memory::store_and_swap<uint32_t>(header + 8, 1);
      REXLOG_INFO("Skipping movie {} ({} frames)", movie.name, frames);
    }
    return;
  }
}
