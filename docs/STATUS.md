# Status

Updated at the end of every session. Only record what a build actually did.

## Current state (2026-10-20)

| Area | State |
|---|---|
| Repository | scaffolded from SwapForceRecomp: build, app, portal (emulated, USB, hybrid), portal / player / settings menus, input scripts and test tools; none of its game hooks or archive readers |
| Game data | disc extracted into `rom/` (1,142 files), `default.xex` hash pinned |
| Codegen | works: 323 files, no analysis errors, no `config/default.toml` entries needed so far |
| Boot | ✅ to the title screen, the main menu (Skyland Adventures, Battle Mode, Options) and the save slots; needs the SDK ring buffer fix (below) |
| SDK | our fork `tsapZ/rexglue-sdk`, branch `fix/ring-buffer-reinit` (one commit on the rexglue-sdk commit SwapForceRecomp uses) |

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
- **First boot: black screen, the game waited for the GPU forever.** Without a debugger on the
  machine, a new input script command `stacks` (src/dev/write_watch.cpp: suspends each thread,
  unwinds it with the binary's own unwind data and names recompiled frames by guest function)
  showed the main thread ("Game Master") in `RtlEnterCriticalSection` and the job worker spinning
  in `sub_822A05F0`, the D3D fence wait with its 5-second watchdog. The GPU had logged
  "ExecutePacketType0 overflow" twice. Logging the ring buffer showed why: the game calls
  `VdInitializeRingBuffer` three times (it recreates its device), and the SDK reset only the read
  pointer. The old ring's write pointer ran the new buffer's leftovers, and the game's first real
  write pointer, below it, read as a wrap through the whole ring into a `0x0BADF00D` guard the
  game keeps 8 KB before its end: that batch and its fence were dropped. Swap Force sets the ring
  up once, so it never hit this.
- **Fix in the SDK** (fork branch `fix/ring-buffer-reinit`): a new ring starts with the "no write
  pointer yet" sentinel, and the command processor's worker restarts at 0 when it sees a new ring.
  After it: logos, intro, title screen, autosave notice, main menu and the save slot list, with no
  GPU errors in the log. Missing optional files (`update:\tuning.lxb`, `game:\UserOptions.lxb`,
  some per-level audio banks) are only logged, as on the console.
