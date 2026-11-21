# The Russian disc (0.0.0.4)

A second Xbox 360 build of Spyro's Adventure, released in Russia and Poland. It is not a re-pressing
of the supported disc: its executable is a different version with different code, and its data
carries Russian and Polish text and voices. This page records what it is, what works here, and what
is still missing.

## Target dump

| Property | Value | Source |
|---|---|---|
| Disc | "Skylanders - Spyro's Adventure (Russia)" | measured |
| Title ID | `415608AB` | XEX execution info |
| Media ID | `0D31DE7E` | XEX execution info |
| Version | `0.0.0.4` (the supported disc is `0.0.0.2`) | XEX execution info |
| Region flags | `00FF0000`: Europe and Australia | XEX security info |
| `default.xex` SHA-256 | `0598a9638d92ec0070f4fb701323c132427dd76af3cd5a0ebc1d61f211b84041` | measured |
| Code pin | `82110000:3d6a86a8777cd8bd093d15c19cad2394bc825d4795eb80c9ce9e8fb2e3e5ce3e` | measured |
| Files | 1606 (1574 without `$SystemUpdate`), 7,166,335,459 bytes without it | measured |
| Languages | Russian and Polish text and voices (`text/rus`, `text/pol`, `audio/rus`, `audio/pol`); English menus (`ui/eng`) | disc folders |

The supported disc's pin is `82110000:0a6b070c...`. 142 of this disc's 143 code-page runs hash
differently, and its file set is another edition (1098 files the supported disc doesn't have, 634
the other way, 248 with the same name and another size). It is a different build, not another
pressing, so the code pin alone cannot make it run: the code has to be recompiled from this disc.

## What works

The build detects this disc by its hash at configure time, recompiles its `default.xex`, and uses
`config/russian.toml` (no codegen hooks) instead of `config/default.toml`. On 2026-11-21, on Linux
with clang 23 and an Intel Iris Xe (Vulkan, 1x), this build:

- boots with `default.xex verified` and loads its title music, UI images and achievements;
- shows the title screen, autosave notices, main menu and save slots in Russian;
- opens the portal menu by itself at "Player 1: place a Skylander on the Portal of Power" — the
  portal prompt's object is at the same address as in 0.0.0.2, so the shared watcher works;
- plays a new game into Level_027 (its intro cutscene) at 60 FPS, 23 FPS while its shaders compile
  the first time; and
- creates and reads a figure file (`--portal_figure`) and reports "Playing as Spyro".

No crash, no script error and no missing required file were seen in those runs. The missing
`update:\tuning.lxb`, `game:\UserOptions.lxb`, `audio\rus\init.bnk`, `text\rus\FrontEnd.lxb`,
`levels\frontend\camera.lxb` and `movies\rus\sl_sc01.bik` lookups are the same optional ones the
supported disc does; the game falls back and carries on.

## What is off

`config/default.toml`'s midasm hooks are keyed to 0.0.0.2 addresses and are not applied to this
build. The features they implement are therefore off:

- intro and opening movie skip (`skip_intro`, `skip_opening_movie`);
- ultrawide FOV;
- scene size (560x352 / 280x176) and the sun-grid split;
- bloom;
- gold/XP multipliers and attribute cheats (the Extras page);
- skip talks;
- the object-list and animation-race crash guards.

`src/hooks/skip_talks_hook.cpp` and `src/hooks/anim_race_guard_hook.cpp` call 0.0.0.2 functions
they replace, so they aren't compiled into the Russian build; `where_hook.cpp`'s per-frame hook is
compiled out, so the log's `Where:` lines and the Extras cheats don't run. The level-name setup in
`InstallWhere` still runs.

The CRT `setjmp`/`longjmp` addresses are also 0.0.0.2-specific and aren't set. A Lua error that
needs a `longjmp` out of a script would end the game instead of being caught (SSARecomp #42). None
was hit in the runs above.

## How to build it

1. Put the extracted Russian disc in `rom/` (`rom/default.xex` must be the 0.0.0.4 file).
2. Configure and build as usual (`cmake --preset linux-amd64-release`, then
   `cmake --build ... --target ssarecomp_codegen`, configure again and build). CMake prints
   "the Russian 0.0.0.4 disc; the 0.0.0.2 hooks are off" and copies `config/russian.toml` to
   `config/active.toml`.
3. Start it with `--language=russian` (or `polish`); `auto` follows a Russian Windows/Linux
   locale after the language names were added, but an English system needs the flag.

The disc's hash and code pin are in `docs/game/russian/` (selected by the same CMake gate), so the
game and `ssarecomp_xexcheck` accept it; a 0.0.0.2 build still checks only the supported hash. Core of Light doesn't know the disc yet; its launcher pin and its "another build" message
still treat it as unsupported until this build ships.

## What is still to do

- Re-find the 0.0.0.2 hook addresses in this build: the portal prompt and data globals happen to
  be unchanged, the code hooks are not. Start with the setjmp/longjmp pair, then intro skip and the
  scene-size/texture hooks.
- Re-find the per-frame function the `Where:` line used (`sub_8280A008` in 0.0.0.2) and
  `sub_827C1BD0` / `sub_8278F7C0` for skip talks.
- Run the level and figure sweeps of the test suite against this disc; the routes and expected
  strings are the supported disc's.
- Teach Core of Light to accept the disc once this build is released.
