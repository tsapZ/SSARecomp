# Status

Updated at the end of every session. Only record what a build actually did.

## Current state (2026-10-20)

| Area | State |
|---|---|
| Repository | scaffolded from SwapForceRecomp: build, app, portal (emulated, USB, hybrid), portal / player / settings menus, input scripts and test tools; none of its game hooks or archive readers |
| Game data | disc extracted into `rom/` (1,142 files), `default.xex` hash pinned |
| Codegen | works: 323 files, no analysis errors, no `config/default.toml` entries needed so far |
| Boot | ✅ title screen, main menu, save slots, new game, intro movies |
| In-game | ✅ Chapter 1 (Shattered Island): the opening cutscenes play through with their voices, then the player has control |
| Portal | ✅ emulated portal: the game resets, activates and polls it, reads a whole figure and shows its portal card ("L-Spyro") |
| SDK | our fork `tsapZ/rexglue-sdk`, branch `ssarecomp`: the rexglue-sdk commit SwapForceRecomp uses plus two fixes, each also on its own branch (`fix/ring-buffer-reinit`, `fix/xma-stream-end`) |

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
- **Portal works unchanged.** The game resolves `XamInputNonControllerGetRaw/SetRaw` at run time
  (`XexGetProcedureAddress`, ordinals 0x4A1/0x4A2) instead of importing them, and talks to the
  portal the way Swap Force does: reset, activate, status polls, `A 01` + `S` every 10 seconds, and
  `Q` reads of every block of a figure on it (the query's slot byte is `0x10`/`0x20` + slot). With
  a figure on the portal (`--portal_figure`, a copy) it shows the figure's portal card.
- **Second hang: every voiced line stalled its cutscene.** The intro stood on its first subtitle
  with the speaker walking on the spot; START skipped it, and the next line stalled the same way.
  Audio itself worked (output level logged per second: movies, menu music, ambience). Logging the
  reads of `audio\streaming.pck` showed each line's Wwise file being read; its header (XMA2,
  mono, 44.1 kHz) promises e.g. 49,280 samples. Counting the decoder's output per XMA context:
  every stream came out exactly one frame short (384 of 385 subframes, 376 of 377, 1,328 of
  1,329), and those contexts were never released: Wwise waits for the rest. Cause in the SDK: the
  start-padding realignment (rexglue-sdk f24299f) writes each frame's block once the next frame
  is decoded, so the last frame of a stream never came out.
- **Fix in the SDK** (fork branch `fix/xma-stream-end`): when the input has run out, the carried
  tail becomes one final block. After it the opening plays through by itself: chapter card,
  Hugo's and Eon's lines, then gameplay with the HUD.
- **Language: `auto` is the new default**, the Windows display language when it is one of the
  five (else English). German checked in game: menus ("SPIEL WÄHLEN", "Skylands-Abenteuer"),
  subtitles and voices, and the voiced lines play through. The installer offers all five.
