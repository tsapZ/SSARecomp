# Status

Updated at the end of every session. Only record what a build actually did.

## Current state (2026-10-21)

| Area | State |
|---|---|
| Repository | scaffolded from SwapForceRecomp: build, app, portal (emulated, USB, hybrid), portal / player / settings menus, input scripts and test tools; none of its game hooks or archive readers |
| Game data | disc extracted into `rom/` (1,142 files), `default.xex` hash pinned |
| Codegen | works: 323 files, no analysis errors, no `config/default.toml` entries needed so far |
| Boot | ✅ title screen, main menu, save slots, new game, intro movies |
| In-game | ✅ Chapter 1 (Shattered Island): the opening cutscenes play through with their voices, then the player has control: walking, camera, attacks, mission messages; 30 FPS like the console |
| Frame rate | 30 FPS by default as on the console; `frame_rate_limit` (F4 > Bildrate) lets it run as fast as the PC allows (50-91 FPS in Chapter 1 at 1080p, scale 2); cutscenes and walking keep their speed |
| Audio | works; the game mixes stereo only: center and LFE silent, the rear channels below 1 % in the title, intro and Chapter 1 (measured 2026-10-20 with a temporary per-channel peak log of the guest's 6-channel render frames), so a 5.1 device gets the front pair, as from the console |
| Portal | ✅ emulated portal: figures are read, shown with their portal card and swapped during play (Spyro for Eruptor); the game writes their progress back to the figure file |
| Portal menu | ✅ opens by itself on the game's "place a Skylander" screen for player 1 and 2 (or with Guide), lists this game's figures by element with the game's portraits, item names and hats; the portal check on "zu viele Skylander" and "zu viele Spielzeuge"; German with the game |
| SDK | our fork `tsapZ/rexglue-sdk`, branch `skylanders` (shared with SwapForceRecomp and later games): the rexglue-sdk commit SwapForceRecomp uses plus two fixes, each also on its own branch (`fix/ring-buffer-reinit`, `fix/xma-stream-end`) |

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
- **Portal menu** (portal fork): opens with Guide on the game's "Bitte setze einen Skylander auf
  das Portal of Power" screen, shows the "Spyro's Adventure" figures by element, and placing
  Spyro through it gets the game's portal card and Hugo's line for Spyro by name. The menus took
  German only from `language = "german"`; they now follow the language the game runs in
  (`user_language`), so "auto" works (portal fork 5a7188c, also in SwapForceRecomp).
- **Playing:** after skipping the opening, Spyro walks, the camera follows, fire breath and the
  mission message ("Rette die Dorfbewohner") work, 30 guest FPS. Swapping Spyro for Eruptor on
  the emulated portal mid-level brings Eruptor in with his card and HUD.
- **Intro skip** (`skip_intro`, on; `skip_opening_movie`, off; both on the F4 game page): a
  midasm hook in BinkOpen (0x829EC1CC, found from "Not a Bink file.") sets the frame count of
  the legal notice, the Activision, XPEC and Toys for Bob logos and optionally the new-game movie
  (`sl_sc01.bik`, Eon, 62 s) to 1, as in Giants and Swap Force. The title screen comes after
  about 5 s instead of 40; with the opening movie skipped, a new game goes straight to the
  in-engine intro. Saves work: slot 1 lists its progress and three Skylanders.
- **The portal menu opens by itself** at the game's "place a Skylander" prompt
  (src/game/portal_prompt_watch.cpp). Found from the prompt text: its string table entry
  (`0x42F96B38`, hash `0x049B83C4` for player 1, `0x9D92D27E` for player 2), a read watch on it,
  and the code loading those hashes, `sub_827E78F0`, which picks the prompt from one object: a
  function-local static at `0x82C569B0` (mode at +56: 2 = prompt up, 4 = a figure coming in,
  0 = playing; player 1's and player 2's "has a Skylander" bytes at +32/+195 and +36/+194). The
  watch polls it every 30 ms like SwapForceRecomp's magic moment watch. Verified: the menu came up
  with the prompt, A placed Spyro from "Zuletzt", the game showed his card and Hugo's line.
- **Portal check on "Es befinden sich zu viele Skylander auf dem Portal of Power":** that screen
  also has mode 2; +52 tells it apart (0 = asking, 1 = too many Skylanders). Before this the
  watch took it for "player 2, place a Skylander". Now it opens the portal check, which lists
  the figures; marking Eruptor and "Fortfahren" took him off and the game went on with Spyro.
- **"Zu viele Spielzeuge auf dem Portal of Power" is no longer taken for player 2's prompt.**
  With Anvil Rain and Darklight Crypt on the portal the game showed that screen with mode 2 and
  +52 = 0, as for its request, and with player 2's bytes empty, so the watch logged "the game asks
  player 2" and would have opened the menu for player 2. The selector has more cases than its
  fields show (it checks the extra toy at +228 against the portal's slots first), so the watch
  now reads the text the game shows instead: the HUD (static at `0x82C56AF8`, `sub_82204058`)
  keeps its portal message at +3260, and the text element at +376 of it holds the text's hash at
  +432 (`sub_82695070` -> `sub_8267B808`). The selector loads seven such texts: the request for
  player 1 and for player 2, "zu viele Skylander", and four "too many" texts for toys (different
  kinds), magic items, pets and location elements. Measured with each case on screen: the
  request for player 1 opens the menu for player 1, the toys screen opens the portal check for
  items (Spyro, Ambossregen, Düstergruft listed), player 2's request (pad 2 pressed A to join)
  opens it for player 2, and "zu viele Skylander" still opens the check for Skylanders.
- **The game's own pictures in the portal menu** (src/game/ui_images.cpp): `uigameimage.str` is a
  zlib-packed archive of 737 UI pictures ("APKF" packages, each a Xenos texture: fetch constant,
  128x128 tiled DXT5). The menu now shows each Skylander's HUD portrait (`toy_<name>_hud`, the
  game's internal names, e.g. `airdragon` for Whirlwind; all 37 checked by eye) on its card and in
  the details panel, and the game's element symbols (`ico_element_<element>_l`). The archive also
  has the round character icons, the hats (`<hat>_lg`), larger character pictures (`<name>_vs`),
  upgrade symbols (`upgradeNNN`), level pictures and the collection frames, not used yet. Magic
  items: Ghost Swords, Healing Elixir, Sky-Iron Shield and Winged Boots have pictures of their own
  (its Battle Mode power-ups), Anvil Rain uses the Anvil Hat's anvil; Hidden Treasure, Time
  Twister, Sparx and the adventure packs keep the menu's symbol.
- **The game's own names for magic items and adventure packs** (src/game/game_text.cpp):
  `text\<lang>\global.lxb` is a table of 2335 texts keyed by the CRC-32 of the game's key
  ("Anvil" = "Ambossregen", "Hourglass" = "Zeitwender", "Level_036" = "Düstergruft"), strings
  UTF-8 or cp1252 mixed. The menu shows them in the game's language. The same table has every
  upgrade's name and description (`GriffinUp09` "Mehr Bumm!") and the hats' names and bonuses
  (`propeller_hat`); the hats' numbers in the figure data are not mapped yet, so neither is used.
- **Frame rate above 30.** The earlier finding "the swap word already has interval 1, and
  `frame_rate_limit=60` still gives 30" held for 60: a frame takes about 19 ms here, so with a
  60 Hz vblank each frame waits for the second one. With a higher limit the vblank comes more
  often and the game runs as fast as it can: 72 FPS on the title screen with 144, 52 at the
  portal prompt, 50-91 in Chapter 1 with 240 (1080p, scale 2). Game speed: the opening cutscenes
  run through in 72.6 s at 30 FPS and 71.5 s uncapped (the second is loading), and 2.5 s of walking
  ends at the same spot. The setting is `frame_rate_limit` on F4 > Bildrate, 0 (30 FPS) by
  default, as in GiantsRecomp.
- **Hats in the details view** (src/game/hats.cpp): the hat number a figure stores is the game's
  kTfbSpyroTag_Hat_* (Swap Force's executable has the enum, 1 Army ... 45 BirthdayCake), and this
  game names its hat data after it: the picture `<name>_lg` (all 45) and the text `<name>_hat`
  (35 of them; "Kampfhut", then one line per bonus with the game's symbol, shown as the symbol's
  picture and the number, as the game does). Found by hashing the enum's names; GiantsRecomp
  uses the same numbering for its own hat data. Checked with a test copy of Chop Chop given hat
  11: "Zylinder", the top hat, +5 critical hit, +5 armor.
- **One figure folder for every game** (the owner's call: like the real figures next to the
  portal): `Documents\Skylanders\Figures` (portal fork `SharedFiguresDir`), shared with
  SwapForceRecomp and later games. The first start copies the old `Documents\ssarecomp\figures`
  into it and leaves a note there. Verified on a test folder (10 files copied, the recent list
  with them).
- **Repository brought to SwapForceRecomp's level:** GitHub milestones per phase and an issue
  for every step, done ones closed with their result, the open ones as the plan; a play test
  issue for the owner; docs/roadmap.md with issue numbers, docs/README.md, docs/portal.md and
  docs/menus.md; the README's status table.
- **Saves survive a crash in the middle of saving** (SDK fork `fix/save-commit-on-close`, found in
  SwapForceRecomp, where a crash while saving left an empty save): the game saves by recreating its
  package ("Skylanders - Slot 1": create, write, close within 0.1 s), and the old one now waits in
  `<user data>/save-backup/pending/` until the new one is closed, then stays in
  `save-backup/previous/`; a start after a crash in between puts it back. Checked on a test copy:
  both saves log "saving ... anew" and "saved".
- **No crash when B closes the portal menu's "real portal is active" message** (portal fork
  1566231, found in SwapForceRecomp with the USB portal).
- **Crash report in the log** (`src/dev/write_watch.cpp`, `InstallCrashReport`, from
  SwapForceRecomp): a real crash logs the guest call chain and registers ("Crash: ...").
- **SDK: vertex data the CPU rewrites in place is uploaded again** (fork `fix/vertex-buffer-cpu-writes`,
  found in SwapForceRecomp, where the second logo movie drew with the first one's texture
  coordinates), and **a rebuilt GPU plugin is copied next to the exe** without a relink (fork
  `fix/stage-gpu-plugin`). Checked: boot with the logo movies, no errors. With the logos not skipped the XPEC logo shows as a small box in the middle of the screen; whether that is the movie's own framing is not checked yet. New RenderDoc script
  `tools/renderdoc/vsout.py` (vertex shader outputs per vertex).

## 2026-10-21: session log (night)

- **Player menu with two players** (portal library, #56 in SwapForceRecomp): Player 1, Player 2
  and "Not playing" (guest users 2 and 3, e.g. a third controller) instead of four player columns;
  left / right moves a controller through the three. Checked by script.
- **Controls page for every device** (#55 / #29): the game's own Xbox 360 controller picture stays
  for Xbox pads; any other pad (PlayStation, Nintendo, a generic one like the Logitech Dual Action),
  and a game without a picture of its own, gets a drawn controller in its layout and with its
  button names. Used with the keyboard (F4, or a key pressed in the menu) the page shows a keyboard
  (QWERTZ in German): a button setting marks its key (F6, F5, F9, F4), the overview every menu key
  and the keys of the keyboard's controller emulation (`keybind_*`) with the button each one
  stands for ("Laufen" on WASD, "A" on Space). Checked by script on both views.
- **Achievements like on the Xbox 360** (#59): an unlocked one pops up in the bottom middle (the
  round icon opens into a dark bar, "Erfolg freigeschaltet", "25G - name", no sound), and the list
  opens with F7 or from F4 > Spiel > Erfolge with a controller (unlocked first with the date,
  totals; up / down, LB / RB, B). Both replace the SDK's own.
- **English checked** (title, portal menu, details, settings) with `--language=english`.
- **Player 2 leaves with RS** (#27): the menu's "Leave the game" presses this game's
  own button for it (`portal_menu_leave_button = "rs"`; Giants and Swap Force keep B). Checked by
  script: the game goes on with player 1 and shows "Drücke A zum Mitspielen" again.
- **No sidekicks in the menu** (#28): they came with Giants (`portal_menu_hidden_categories`).
- **Hat bonuses with their name** (#58): the stats screen's labels (StatsElemetal, StatsArmor,
  StatsCT, StatsSpeed): "Generalsmütze: +7 Kritischer Treffer, +7 Elementar".
- **Gold symbol:** none found in uigameimage.str (the HUD's coin is elsewhere), so the drawn coin
  stays for now.
- **Animations above 30 FPS** (#26): the portal screen's light and sparks step once per frame:
  between two screenshots 0.1 s apart they move 4.0 (mean difference) at 30 FPS and 9.6 uncapped
  (54.6 FPS), the pulse every 0.3 s instead of 0.8 s. `frame_rate_limit=60` gives 30 FPS on this PC
  (a frame takes ~19 ms and waits for the second 60 Hz vblank). The one float 1/30 in the
  executable belongs to another function (82702B28). F4 > Bildrate now says that some effects run
  faster above 30; the default stays 30.
- **A crash under RenderDoc:** with RenderDoc attached (19 FPS) the game crashed 18 s into Chapter 1
  (read of guest 0x368 in sub_826326B8, called from sub_82631098); not seen without RenderDoc.
- **Upgrades in the details view** (#20): every Skylander's starting attacks, basic upgrades, both named paths and the Soul Gem from the game's text table, bought ones marked, the attack icons as "Angriff 1/2/3", an upgrade arrow as the tile (no pictures in this game). The prices are Giants' until this game's own table is found; checked on Chop Chop.
