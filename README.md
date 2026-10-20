# SSARecomp

An unofficial native Windows port of **Skylanders: Spyro's Adventure** (Xbox 360), made with static
recompilation through ReXGlue. Chapter 1 plays with its voices and cutscenes, the emulated Portal of
Power and a portal menu for the controller.

> **Unofficial fan project.** It is not affiliated with or endorsed by Activision, Toys for Bob
> or Microsoft. This repository contains **no game data**: you need your own copy of the game.

One of a set: SSARecomp, [GiantsRecomp](https://github.com/tsapZ/GiantsRecomp) and
[SwapForceRecomp](https://github.com/tsapZ/SwapForceRecomp) share one portal, one menu style and
one figure folder. Later the hub, [Skylanders: Core of Light](https://github.com/tsapZ/SkylandersCoreOfLight),
starts them all; each game stays playable on its own.

## Status

Chapter 1 playable. Where each part stands and what comes next: **[docs/roadmap.md](docs/roadmap.md)**.
Session details and known bugs: [docs/STATUS.md](docs/STATUS.md).

| Area | State |
|---|---|
| Boot, menus, saving | ✅ title screen after about 5 s (logo movies skipped), main menu, save slots |
| Chapter 1 | ✅ the opening cutscenes with voices, then gameplay: walking, camera, attacks, mission messages |
| Rest of the game | 🟡 not played through yet (#9) |
| Frame rate | ✅ 30 FPS like the console, or faster: 50-91 FPS in Chapter 1 (F4 > Bildrate), game speed unchanged |
| Audio | ✅ stereo, as the game mixes it |
| Languages | ✅ English, German, French, Spanish, Italian; `auto` follows Windows |
| Portal | ✅ emulated: figures come in with their portal card, swap during play and keep their progress; real USB portal still to test (#13) |
| Portal menu | ✅ opens by itself at "Bitte setze einen Skylander" for player 1 and 2; the portal check on "zu viele Skylander" and "zu viele Spielzeuge"; the game's own portraits, item names and hats; upgrades still to come (#20) |
| Settings menu | ✅ F4 or Back + RB: presets, resolution, frame rate, language, portal, controls |
| Ultrawide | ⬜ planned (#22) |
| Installer tested on a clean PC | ⬜ planned (#23) |

## Playing

Build it once (below), then from the **x64 Native Tools Command Prompt for VS 2022**:

    just play-release

The keys and menus are in [docs/menus.md](docs/menus.md), the portal in [docs/portal.md](docs/portal.md).
The emulated figures live in `Documents\Skylanders\Figures`, shared with the other games.

## Building

Needs Windows, Visual Studio 2022 (C++ workload and C++ Clang tools), CMake, Ninja, just, Git and
Python 3, and the game's `default.xex` in `rom/` (see [rom/README.md](rom/README.md)). The steps are
the same as for SwapForceRecomp: [docs/development.md](docs/development.md). All docs:
[docs/README.md](docs/README.md).

## Credits

- **[SwapForceRecomp](https://github.com/tsapZ/SwapForceRecomp)** (Skylanders: Swap Force): this
  project is scaffolded from it; app, portal, menus and tooling come from there.
- **[GiantsRecomp](https://github.com/TheBiemGamer/GiantsRecomp)** by TheBiemGamer brought
  Skylanders Giants to PC and is the template SwapForceRecomp started from: build setup, tooling
  and workflow.
- **[ReXGlue](https://github.com/rexglue/rexglue-sdk)**, the static recompilation SDK, here in
  TheBiemGamer's fork with our fixes on top.
- **[extract-xiso](https://github.com/XboxDev/extract-xiso)** is included (prebuilt, unmodified)
  for the installer (`installer/extract-xiso.LICENSE.txt`).

Skylanders is a trademark of Activision. This project is not affiliated with or endorsed by
Activision.
