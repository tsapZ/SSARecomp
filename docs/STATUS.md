# Status

Updated at the end of every session. Only record what a build actually did.

## Current state (2026-10-20)

| Area | State |
|---|---|
| Repository | scaffolded from SwapForceRecomp: build, app, portal (emulated, USB, hybrid), portal / player / settings menus, input scripts and test tools; none of its game hooks or archive readers |
| Game data | disc extracted into `rom/` (1,142 files), `default.xex` hash pinned |
| Codegen | works: 323 files, no analysis errors, no `config/default.toml` entries needed so far |
| Boot | not tried yet |

## 2026-10-20: session log

- **Scaffold:** copied from SwapForceRecomp (its tracked files at c4c9c45), renamed, its Swap
  Force-specific parts left out (hooks, the Alchemy archive readers, the magic moment watch, the
  game art), docs started new. Submodules: the same rexglue-sdk commit and the portal fork's
  `feature/portal-menu` branch.
- **Disc:** extracted with `extract-xiso` (1,142 files). Other file formats than Swap Force and
  ten languages: docs/game/spyros-adventure.md.
- **Codegen and build: work at the first try.** `just regen-release` (SDK, codegen, game) took
  6 minutes; codegen reported no analysis errors (unlike Swap Force, which needed one
  `[functions]` entry) and wrote 323 files. Title `415608AB`, version `0.0.0.2`, built
  2011-08-22. Unit tests pass (15).
