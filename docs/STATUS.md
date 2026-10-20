# Status

Updated at the end of every session. Only record what a build actually did.

## Current state (2026-10-20)

| Area | State |
|---|---|
| Repository | scaffolded from SwapForceRecomp: build, app, portal (emulated, USB, hybrid), portal / player / settings menus, input scripts and test tools; none of its game hooks or archive readers |
| Game data | disc extracted into `rom/` (1,142 files), `default.xex` hash pinned |
| Codegen | not run yet |
| Boot | not tried yet |

## 2026-10-20: session log

- **Scaffold:** copied from SwapForceRecomp (its tracked files at c4c9c45), renamed, its Swap
  Force-specific parts left out (hooks, the Alchemy archive readers, the magic moment watch, the
  game art), docs started new. Submodules: the same rexglue-sdk commit and the portal fork's
  `feature/portal-menu` branch.
- **Disc:** extracted with `extract-xiso` (1,142 files). Other file formats than Swap Force and
  ten languages: docs/game/spyros-adventure.md.
