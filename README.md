# SSARecomp

An unofficial native Windows port of **Skylanders: Spyro's Adventure** (Xbox 360), made with static
recompilation (ReXGlue). Work in progress: the project is being set up and does not boot yet.

You need your own copy of the game: nothing from the game is in this repository.

## Status

See [docs/STATUS.md](docs/STATUS.md) and [docs/roadmap.md](docs/roadmap.md).

| Area | State |
|---|---|
| Build | ✅ codegen and build work |
| Boot | ✅ title screen, main menu, save slots |
| Portal | ⬜ (the portal code is in, not tried with this game) |

## Building

The same as SwapForceRecomp: see [docs/development.md](docs/development.md).

## Credits

- **[SwapForceRecomp](https://github.com/tsapZ/SwapForceRecomp)** (Skylanders: Swap Force): this
  project is scaffolded from it; app, portal, menus and tooling come from there.
- **[GiantsRecomp](https://github.com/TheBiemGamer/GiantsRecomp)** by TheBiemGamer brought
  Skylanders Giants to PC and is the template SwapForceRecomp started from: build setup, tooling
  and workflow.
- **[ReXGlue](https://github.com/rexglue/rexglue-sdk)**, the static recompilation SDK, here in
  TheBiemGamer's fork.
- **[extract-xiso](https://github.com/XboxDev/extract-xiso)** is included (prebuilt, unmodified)
  for the installer (`installer/extract-xiso.LICENSE.txt`).

Skylanders is a trademark of Activision. This project is not affiliated with or endorsed by
Activision.
