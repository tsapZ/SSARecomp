// Scripted controller input for automated test runs (--input_script=<file>).
//
// The keyboard emulation only reads keys while the game window has the focus, so a test that
// drives the game from outside breaks as soon as anything else takes the foreground (a system
// dialog, a notification). This plays a fixed timeline instead, independent of the focus, as an
// extra input device that merges with the controller and keyboard like the keyboard does.
//
// Script: one command per line or ';'-separated, '#' starts a comment. Each command starts when
// the previous one ends; the timeline starts when the game starts.
//   wait <seconds>
//   tap <button>             press for 120 ms
//   hold <button> <ms>
//   stick <x> <y> <ms>       left stick, x and y in -1..1
//   log <text>               writes a log line, to find a point of the timeline in the log
//   fps <seconds>            measures the guest frame rate over that time and logs it
//   scan <hex> [<hex> ...]   logs where this big-endian u32 sequence is in guest memory
//   poke <hex addr> <hex>    writes a big-endian u32 to guest memory
//   dump <hex addr> <words>  logs that many big-endian u32 from guest memory, 8 per line
//   watch <hex addr> [<hex> [<hex snapshot addr> <hex words>]]
//                            logs which guest functions write this address, only writes of that
//                            big-endian value if given, and copies guest memory at the first hit
//                            (src/dev/write_watch.h)
//   watchread <hex addr>     the same for reads and writes
//   unwatch                  ends the watch and logs the writers
//   renderdoc [n]            captures the next n guest frames with an attached RenderDoc
//   player <n>               the scripted controller plays as player n (1-4) from now on
//   key <name>               presses a key for the app's key binds (F4, F6, F9; names as in the
//                            bind_* settings), e.g. to open an overlay
//   place <slot> <file>      puts a figure file straight onto the emulated portal's slot (0-15),
//                            past the portal menu, e.g. a second sidekick for the portal check
//   quit                     ends the game
// Buttons: a b x y start back guide up down left right lb rb ls rs lt rt, or several pressed
// together joined with + (tap back+y)

#pragma once

#include <cstdint>
#include <functional>

namespace rex::memory {
class Memory;
}
namespace rex::system {
class IInputSystem;
}

namespace ssarecomp {

// Installs the scripted input device if --input_script is set. Call once the runtime exists.
// guest_frame_count returns the number of guest frames presented so far (renderdoc command);
// run_in_ui runs a function on the UI thread (key command).
void InstallInputScript(rex::system::IInputSystem* input_system, rex::memory::Memory* memory,
                        std::function<uint64_t()> guest_frame_count,
                        std::function<void(std::function<void()>)> run_in_ui);

}  // namespace ssarecomp
