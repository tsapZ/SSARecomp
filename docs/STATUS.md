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
| Picture | the 3D scene is 1120x704 on the 360, stretched to 720p; F4 > Grafik > "Natives 3D-Bild" draws it at 1264x704 (experimental, off by default; 86.0 instead of 86.7 FPS in Chapter 1) |
| Frame rate | 30 FPS by default as on the console; `frame_rate_limit` (F4 > Bildrate) lets it run as fast as the PC allows (50-91 FPS in Chapter 1 at 1080p, scale 2); cutscenes and walking keep their speed |
| Audio | works; the game mixes stereo only: center and LFE silent, the rear channels below 1 % in the title, intro and Chapter 1 (measured 2026-10-20 with a temporary per-channel peak log of the guest's 6-channel render frames), so a 5.1 device gets the front pair, as from the console |
| Portal | ✅ emulated portal: figures are read, shown with their portal card and swapped during play (Spyro for Eruptor); the game writes their progress back to the figure file |
| Portal menu | ✅ opens by itself on the game's "place a Skylander" screen for player 1 and 2 (or with Guide), lists this game's figures by element with the game's portraits, item names and hats; the portal check on "zu viele Skylander" and "zu viele Spielzeuge"; German with the game |
| SDK | our fork `tsapZ/rexglue-sdk`, branch `skylanders` (shared with SwapForceRecomp and later games): the rexglue-sdk commit SwapForceRecomp uses plus two fixes, each also on its own branch (`fix/ring-buffer-reinit`, `fix/xma-stream-end`) |

## 2026-10-20: session log

- **Scaffold:** copied from SwapForceRecomp (its tracked files at 96fad5d), renamed, its Swap
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
  (`user_language`), so "auto" works (portal fork 9e7b5c7, also in SwapForceRecomp).
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
  the Soul Gems' pictures (`upgradeNNN`, used since 2026-10-21, see the upgrades), level pictures
  and the collection frames, not used yet. Magic
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
  8f56b05, found in SwapForceRecomp with the USB portal).
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
- **Upgrades in the details view** (#20): every Skylander's starting attacks, basic upgrades, both named paths and the Soul Gem from the game's text table, bought ones marked, the attack icons as "Angriff 1/2/3". The Soul Gem's tile shows the game's own picture (`upgrade<toy id>` in uigameimage.str: 32 pictures, one per Skylander, e.g. Spyro's ground pound, Gill Grunt's anchor; Dark Spyro and the legendary figures show their character's; the owner asked why the tiles had no pictures, 2026-10-21); the other upgrades have none in the game's files and keep an upgrade arrow. The prices are Giants' until this game's own table is found; checked on Chop Chop.

## 2026-10-21: session log (morning)

- **Native 3D picture** (#30, experimental, off by default): the game draws its scene into
  textures of 1120x704 (constants in the code: `sub_829611D8` creates every scene texture, nine
  times 1120x704, seven times 560x352, twice 280x176; one header in `sub_82929EE8`; a 35 x 22
  grid of 32-pixel cells in `sub_828924B8`) and stretches them 14 % wider to 1280x720. Four mid-asm
  hooks put `scene_size` into all of them (src/hooks/scene_size_hook.cpp). The main pass then
  needs more than 2048 EDRAM tiles, so `scene_size` turns on the SDK's 4096-tile EDRAM; the game
  places its surfaces beyond tile 2048 by itself (no EDRAM hooks as in Swap Force). A configured
  1280x720 draws 1264x704: 1 of 6 runs at exactly 1280x720 ended in an abort while the save loaded
  (no log line; cause not found), 0 of 5 at 1264x704. Checked by script in Chapter 1: right
  picture and framing, the stretch's stair steps gone, 86.0 FPS instead of 86.7 (1080p window,
  scale 2, limit 200).
- **The game's fonts in the portal menu** (src/game/game_fonts.cpp, #34): this engine's fonts
  are rom/textures/<name>.fnt (glyph table) and <name>.apk (one 32-bit tiled page); five of them
  (arial, bleeker, lt, ltnob, robotic). The titles in "ltnob", the game's title letters without
  the outline "lt" has, the text in the rounded "bleeker". The table's cell width is the advance,
  fitted on the portal prompt's "Bitte setze einen Skylander" (within 0.9 px per letter); ASCII
  from entry 0, Latin-1 at entry code + 33 with German's ä ö ü ß. Read in 14 ms at start. Checked
  by script: the portal menu's title in the same letters as the game's "Spieler 1:" behind it;
  tabs, names, hints and the hat's "Generalsmütze" in the text font. Fixed the same evening: a
  letter's picture is only its ink's width (the cells overlap the next letter in the page), the
  cell's width only its advance; before, a sliver of the next letter showed after "a", "l", "m"
  and "n" ("auf dem Portal" read "a|uf dem Portal|").
- **Adventure packs (location pieces) in the menu** (#21, 2026-10-21): the library labels the toys
  300-399 as adventure packs; this game names them as its prompts do ("Ortselement" / "Location
  piece", game_text.cpp), the details view says they open a level of their own, and the portal
  check with a magic item and a location piece says "Zu viele Spielzeuge auf dem Portal" like the
  game. Checked by script: Spyro, Ambossregen and Düstergruft listed, Düstergruft as "Ortselement".
  Their pictures are still the menu's symbol (#31).
- **Ultrawide** (src/hooks/ultrawide_hook.cpp, `ultrawide_fov`, on, #22): a window wider than 16:9
  (`resolution`) shows more to the sides instead of black bars, as in the other games. This
  engine has no perspective builder like theirs; its cameras take their aspect from one global,
  the display's (0x82BB45EC, the video mode's width over its height, stored by sub_827F56A0), and
  widen their horizontal FOV with it themselves (Hor+). The hook sets that value to the window's
  aspect; the app turns the letterbox off. Found from the 16:9 floats in the image's data;
  checked by script in a 2560x1080 window: Hugo's cutscene and Chapter 1 keep their proportions
  and show more at both sides; the HUD and the dialogue box are stretched.
- **The HUD for ultrawide, experimental** (`ultrawide_hud`, off): the UI lays itself out on the
  screen's width and half width kept next to the aspect (0x82BB45DC / 0x82BB45E4); set to the
  window's width at the UI's height, the HUD, the dialogue box and the menus keep their
  proportions (the portrait stays where the game anchors it, 480 left of the centre). Off by
  default because two notices the game parks left of the 16:9 screen come into view at the left
  edge and the pause menu's dimming is drawn only 1280 wide. Checked by script in a 2560x1080
  window.
- **Movies on ultrawide** (2026-10-21): the logos, the cutscene movies and the figures' movies are
  16:9 Bink movies; while one plays the presenter shows it with black bars at the sides instead of
  stretched (present_use_actual_aspect off and present_letterbox on for that time, not saved).
  Start: BinkOpen's header hook; end: a hook at the start of BinkClose (sub_829EDEB8, called only
  from the game's movie player close). Checked by script in a 2560x1080 window: the XPEC logo
  and the opening movie (Eon) 16:9 in the middle, Hugo's in-game cutscene after it fills the window
  again.

## 2026-10-22: session log (night)

- **Portal library** (83cc22e, 141d32e): the later games' cards say "Fahrzeug", "Sensei", "Falle" and "Serie 2" in the German menu; a plain hat symbol where a game has no hat picture.
- **New issues** for options the other games have: #35 bloom and motion blur strength, #36 the game's controller picture.

## 2026-10-22: later the same night

- **The controls page shows the game's controller picture** (ui/eng/startmenu.str's 360_controller.dds, cut to the pad), #36 closed.
- The settings example's frame rate line says 60 like the default.
- **Settings file read even with a repeated key** (SDK fork 8761248): a key set twice in a table makes the whole TOML file invalid, and the game then ignored every setting in it without a visible error (the error went to a log that didn't exist yet). The SDK now reads such a file with the repeats left out and logs which key was repeated.
- **`--settings_schema=<file>`** (skylanders-portal 4521ced): the game writes its F4 menu's pages with their values and quits before a window opens; the Core of Light launcher shows them as the game's settings and writes into this game's settings file.
- **Figure pictures for the launcher** (skylanders-portal e17786b): while the game runs, every figure picture it reads from the disc goes into `Documents\Skylanders\Pictures\<game>` once, for the launcher's figure library; later the same night (skylanders-portal 6c3c6a3) also the hats it knows (hats.txt, their pictures) and the upgrade trees of the figures in the figure folder; and
(skylanders-portal f571d26) the game's achievement list with the title's own pictures, for the
launcher's achievements screen.
- **Leuchten (Bloom) as an option (F4 > Grafik, #35, off by default):** the game has a bloom pass
  ("Bloom Render", `sub_8289A898` / callback `sub_8289A428`) but leaves its parameter block at 0
  in play, so nothing shows. The hook at the callback's start (0x8289A428) fills the block with a
  threshold of 0.7, the colour of the one scene that sets it and a strength of a quarter per step
  (Schwach 0.5, Mittel 1, Stark 1.5): a glow the game was never tuned with, so Aus is the
  original look. Checked in Chapter 1, the figure standing: the picture's mean brightness 119.9
  off, 125.5 / 129.3 / 132.3 at the three steps, a soft glow on the sunlit path and the sky; the
  first try (threshold 0.4, the full strength) washed the whole ground out orange.
- **Nothing skipped by default** (the owner's choice, 2026-10-22): `skip_intro` is off, the logo
  movies play as on the console; the settings templates say so too.

## 2026-10-23: session log (night)

- **Title music from the disc** for the Core of Light launcher (skylanders-portal 6e5c284,
  rexglue-sdk 63e87be): `--export_title_music=<file.wav>` decodes the track the title screen
  streams (found with a run logging the game's file reads) from XMA2 with the SDK's FFmpeg and
  quits before a window opens, in under a second; each normal start leaves `title_music.wav` in
  the game's picture folder once. Checked: the Wwise stream 205913188 of audio/streaming.pck (62.8 s); identical to FFmpeg 8.1's own decode of the
  track (-63 dB).
- **Leaving the game with the pad:** F4 > Spiel ends with "Spiel beenden" and "Spiel neu
  starten" (both ask, "Nein" first; restart not offered when the launcher started the game; the
  new instance waits for the old one), the player menu is a row on the Portal page, the settings
  menu's buttons have no "Aus" any more (it locked a pad out), "Alle Einstellungen" says it is for
  keyboard and mouse (skylanders-portal 5fff7aa).
- **HD texture packs:** rexglue-sdk 37d3e77 dumps the game's BC1-BC3 textures
  (`texture_dump_dir`) and loads `<hash>.dds` replacements of any size (`texture_replace_dir`);
  a pack in `Documents\Skylanders\Textures\Spyro's Adventure` is used by itself (skylanders-portal 94175f0).
  Checked in Swap Force's hub (see SwapForceRecomp docs/texture-packs.md, #62).
- **F4 > Grafik:** "Schärfe-Stärke" (CAS's additional sharpness) and "Farbverläufe glätten
  (Dithering)", dithering on by default.
- **Installer:** its own AppId (all six shared one); `/NOROM=1` installs without the disc for the
  launcher (the settings file is still written); reasons a silent install stops go into its log.
  Silent install from the owner's ISO, start (German title screen), silent uninstall: checked.
- **Blocky shadows out of the ground fixed (#30):** the game lights the sun only in the 32x32
  tiles it marks in a half-resolution 4x MSAA stencil, chosen on the CPU from one resolve (the
  1120x704 32-bit one at guest 0x19bc0000). Without resolve readback the CPU never saw it.
  `readback_resolve = fast` plus `readback_resolve_addresses = 0x19bc0000` (rexglue-sdk 0bdd909)
  as defaults: smooth shadows for 2.5 % of the frame rate (91.6 -> 89.3 FPS at 2x, uncapped; all
  resolves read back cost 31 %). Found on a copy of the owner's save in the hub with a RenderDoc
  capture and `readback_resolve_log`; the address is the same in Chapter 1 and at scale 1 and 2.

## 2026-10-24: 0.1.0-beta.7 (a colleague's evening on SteamOS)

- **Ultrawide by itself in fullscreen** (skylanders-portal bd1b318): the ultrawide camera and the
  presenter take the aspect from `resolution`, so "Fenstergröße: Automatisch" stayed 16:9 on a
  21:9 screen. `ResolutionFromScreenIfAutomatic`, first thing in OnPreSetup, sets it for the start
  to the primary screen's mode (nothing written; not for the launcher's schema).
- **Window sizes** add 2560x1080 and 3440x1440 (21:9), 3840x1600 (24:10) and 5120x1440 (32:9).
- **"Tastatur spielt als"** is greyed and can't be changed while "Tastatur und Maus" is off
  (skylanders-portal 10a00cd, `enabled_by`, also in the schema for Core of Light).

## 2026-10-24: 0.1.0-beta.8

- **Later games' versions in the portal menu's version choice** (skylanders-portal 202b4df): Y on a
  Skylander lists its versions from the later games too (Spyro in Spyro's Adventure: Series 2 and
  3, LightCore, Eon's Elite; the game plays them by their id as its own; the owner's wish). The
  tabs stay this game's own; "Figuren späterer Spiele" now only adds the later games' tabs.
- Ultrawide checked in a 2560x1080 window from the title to the first menus in Spyro's Adventure,
  Giants, Swap Force, SuperChargers and Imaginators (menus centred, nothing stretched, movies with
  bars); Trap Team's window couldn't be captured here (black at 16:9 too), so not by picture.

## 2026-10-24: 0.1.0-beta.9

- **60 FPS that really are 60** (a colleague saw 30 although 60 was set): the game waits for the
  next vblank after each frame, and at frame_rate_limit 60 the SDK's guest vblank ran at 60 Hz, so
  a frame of about 19 ms waited for the second one: 30.0 FPS in Chapter 1 here. The SDK's new
  guest_vblank_rate (rexglue-sdk fix/guest-vblank-rate) runs it at 240 Hz in this game
  (ssarecomp_app.h), frame_rate_limit still caps the rate: 57.0 FPS in the same scene.
- **One data folder for every game** (the owner's call; skylanders-portal data_folder.h): the saves,
  achievements and shader cache are in `Documents\Skylanders\Spyro's Adventure` now, next to Figures and
  Pictures, or in the folder picked in Core of Light (`%APPDATA%\Skylanders\data-folder.txt`). The
  first start copies the old `Documents\ssarecomp` there (complete or not at all) and leaves a
  note in it; Core of Light asks before the old folder goes. `--user_data_root` works as before.
- **Keyboard keys changeable in F4** (SwapForceRecomp #63): a "Tastatur" page after "Steuerung", a
  row per controller button; A and then the key. Y also saves with a German keyboard now (the
  menu reads keys by their US place, where that key is Z).
- The portal menu's Portal of Power: a band of light round its side, ridges on the rim, rings of
  light running out over the floor (SwapForceRecomp #57).
- Checked: every game boots to its first menus on fresh scratch data with the new data folder
  code (with --user_data_root, so the copy itself was checked in the launcher on a scratch
  Documents).

## 2026-10-24: after 0.1.0-beta.9

- **Blocky shadows with "Natives 3D-Bild" (#38):** the #30 fix reads back one resolve (0x19bc0000);
  with scene_size the same resolve lands at 0x19a80000 (found with readback_resolve_log, same order
  of resolves in both modes), so both are in readback_resolve_addresses now. For the owner to look at
  in Chapter 1 (grass by the pillars, the vase). A chosen 4x texture filter stays 4x (lib 23be5f2).

## 2026-10-25: 0.2.0-beta.1

- **No blocky shadows with the native 3D picture either** (#38): its shadow resolve sits at
  0x19a80000, next to the one without it; `readback_resolve_addresses` takes both.
- **Portal menu, companions by game** (skylanders-portal 2278b99): what goes on next to a player's
  Skylander now depends on the game (sidekicks up to Swap Force, traps in Trap Team, vehicles from
  SuperChargers on). Here the sidekicks behave as before.
- **Mouse wheel in the menus** (SwapForceRecomp #65; skylanders-portal ccc7009): F4's rows, the
  portal menu's cards and its variant list scroll a row per notch; the chosen row stays, and the view
  follows it again once the keys or the pad move it.
- **A chosen 4x texture filter stays 4x** (Core of Light #21, a colleague's report and fix;
  skylanders-portal 23be5f2): 4x is the SDK's own default, so it counted as unset and became 16x.

## 2026-10-25: 0.2.0-beta.2

- The setup's version for what came since 0.2.0-beta.1: in every game: the portal overview (Back + Y / F5: what is on the portal, taken off one by one without lifting the
  Skylander) and F4 > Controller (button jobs, vibration strength per player; not yet tried with a
  real controller), skylanders-portal f2c4940 / 6e96c16; an A press right after the menu opened by
  itself no longer picks a figure (SwapForceRecomp #72); one bad command-line option no longer drops
  the others (rexglue-sdk 605000d).

## 2026-10-25: afternoon (after 0.2.0-beta.2)

- **"Dieser Skylander braucht eine Pause"** (its text hash 0x8F980361, the portal prompt watch's
  text check): opens player 1's menu and marks the Skylander tired.
- **The single upgrade pictures** (#39): every character archive has its own `<prefix>_primary`,
  `_secondary`, `_up1` ... `_up4`, `_up5a` ... `_up7b` and `_up8` (prefix = the strip's name before
  "upgrades"); the starting attacks and the basic upgrades now show those. Checked with Chop Chop:
  every row of the tree has its picture.
- **The portal overview holds the game** while it is open (rexglue-sdk adf2a90, a frame gate in
  VdSwap: the guest waits before handing over its next frame while the menus keep drawing;
  skylanders-portal ab11cbc). It closes when the portal menu opens and after a vehicle is taken
  off there (the game's next request took the Skylander off with the same A, SuperChargersRecomp).
  Its cards stand just above the portal, and the portal's band of light goes all the way round.
- **Tired Skylanders** (skylanders-portal `PlayerSkylanderTired`): when the game says a player's
  Skylander is out of health, the menu opens and that figure gets a red cross until it has been on
  the portal a while again (a new level). It stays pickable, so a wrong guess can't lock a figure.
- **A hat shows only in a game that knows it** (Enigma's SuperChargers hat showed as "Hut Nr. 269").
- **F4 opens on "Spiel"**, which now has the game's **volume** (0-100 %, SwapForceRecomp #68) and
  the **frame rate display** (the SDK's F3 window, Core of Light #28). **F4 > Tastatur** names each
  key as this PC's keyboard prints it ("Ö / Leertaste", "Umschalt+Pfeil hoch") and has "Alle
  Tasten zurücksetzen" (SwapForceRecomp #66). skylanders-portal 218defd.
- The "who's talking" circle sits higher, above the game's health bar.
- **Settings descriptions broken into lines** (SwapForceRecomp #69): one line on what a setting does,
  the choices on lines of their own, notes ("Kostet ...", "Braucht D3D12") after a blank line;
  the same wording in every game where the setting is the same. The Back + Y row is called
  "Portal-Übersicht" now (it opens the portal overview since skylanders-portal f2c4940).
  skylanders-portal 2a97055.
- F4 > Bildrate says that a few effects (the shine over "Drücke Start", the portal screen's light)
  run faster above 30 (#26; the owner saw the title's shine flicker, 2026-10-25).
- **Hints in the pad's own buttons** (Core of Light #29, skylanders-portal 39ab210): after a press on a
  PlayStation pad the menus' hints show its symbols and L1 / R1 / Options, on a Nintendo pad its
  letters in their places and L / R / ZL / ZR / +; Xbox-like pads stay as they were.
- **A guard against a crash at the start of a new game** (#32): one test run of three crashed about
  40 s in (resolution scale 1, no RenderDoc), a read of guest 0x4 in sub_822895E0 with r3 = 0.
  That function walks a list of game objects from the node in r3; its caller sub_82631098 passes a
  manager's member (+840) that can still be 0, the same caller as the RenderDoc crash in #32. A
  mid-asm hook at its start returns at once when r3 is 0 and leaves the list's sentinel as the
  current node, so the caller's loop ends too (src/hooks/object_list_guard_hook.cpp). Three more
  runs didn't crash, but the guard didn't fire in them either, so the crash it is for wasn't seen
  again yet; it logs "Object list: ... skipped" when it does.
- **The "second layer" round objects** (the owner, 2026-10-25): seen in test captures only at
  resolution scale 2 and 3 (dark and light outlines at the edges of mushrooms and rocks against the
  sky), not at 1, so it comes from the resolution scaling; the cause isn't found yet.
- **#32's crash itself guarded, and figure changes with an HD pack without the stutter** (the owner,
  2026-10-25: "selecting a figure lags a lot and hangs oddly, again and again"): his log shows about
  100 HD textures loaded in the seconds after a figure goes on, and the SDK read each replacement
  byte by byte on the GPU thread (rexglue-sdk ce9dbb6 reads it in one go: about 2 ms each now).
  With the HD pack a test run also hit #32's crash (a read of guest 0x368, sub_826326B8 from
  sub_82631098): the tree node in 0x82BB4F4C was 0 while the game ran slowly. A second mid-asm hook
  at sub_82631098's start returns when it is 0. The next run with the pack didn't crash.

## 2026-10-25: 0.2.0-beta.3

- The setup's version for what came since the last one: the single upgrade pictures (#39) and two guards for #32's crash. Also in every game: the portal overview holds the game while open, a Skylander out of health gets the menu and a red cross, hats only where the game knows them, F4 opens on "Spiel" with volume and frame rate rows, key names as the keyboard prints them and a reset row, descriptions in short lines, hints in a PlayStation or Nintendo pad's own buttons, and figure changes with an HD pack without the stutter (skylanders-portal 39ab210, rexglue-sdk ce9dbb6).

## 2026-10-26: 0.2.0-beta.4

- The setup's version for what came since the last one: the menu pictures load in one read (85 to 18 ms). Also in every game: no crash at start when the window moves to another monitor (rexglue-sdk 07f1106), and the portal menu no longer lifts a Skylander the game is still loading from the portal (skylanders-portal d4ce7b6).

## 2026-10-26: night

- rexglue-sdk 90f3bad: an XMA input counts as used up only once its output is out (the cut-off ends of Trap Team's portal lines, TrapTeamRecomp #15). Checked here: Chapter 1's spoken cutscene (Flynn, then Hugo) still plays through to Spyro's arrival.
- skylanders-portal 8cbf30d: the picture cache also leaves the game's element symbols for the launcher's figure library (SkylandersCoreOfLight #38). Checked: all eight.

## 2026-10-26: 0.2.0-beta.5

- The setup's version for what came since the last one: In every game: a sound the game stops right at its end keeps its last 1024 samples (rexglue-sdk 90f3bad), and the game leaves its element symbols for the launcher (skylanders-portal 8cbf30d).

## 2026-10-27: night

- **No more ghost outline round close objects at resolution scale 2 and up** (#40). The depth-of-field composite turns the pixel position into a texture coordinate the D3D9 way, (position + 0.5) / size; with the SDK's corner position it read the scene, its depth and the blur one host pixel off (1.5 at scale 3), so foreground edges picked up the blurred background's colour. rexglue-sdk 5201aa3 adds draw_resolution_scaled_pixel_position_centered (each host pixel's centre), and the game turns it on in OnPostSetup unless it was set. Measured in a RenderDoc capture of the Chapter 1 cutscene at scale 2: the composite's output one host pixel up-left of its input before, on it after (mean difference 0.4 of 255); the downsample before it was exact both times (checked against a CPU version). The title screen lines up with scale 1 either way.

## 2026-10-27: 0.2.0-beta.6

- The setup's version for what came since the last one: no more ghost outline round close objects at resolution scale 2 and up (#40, rexglue-sdk 5201aa3).

## 2026-10-27: shadows, start-up

- **Softer shadows and blur at resolution scale 2 and up** (ImaginatorsRecomp #12): skylanders-portal afba1dd sets draw_resolution_scaled_texture_offsets = false unless it was set. The SDK divided texture fetch offsets by the resolution scale, so the games' fixed blur kernels (the shadow mask's 9-tap blur, bloom) shrank with the scale. Compared here at scale 2, on and off: the shadow under a character in the Chapter 1 cutscene a little softer. Scale 1 is the same either way.
- **A start-up crash less** (rexglue-sdk 807af87): SDL's audio subsystem starts on the UI thread before the game's audio driver asks for it. Started from the audio thread it could race SDL_UpdateAudio in the UI thread's event pump and end in exception C0000264 (once in Trap Team, 2026-10-27).

## 2026-10-27: 0.2.0-beta.7

- The setup's version for what came since the last one: softer shadows and blur at resolution scale 2 and up (texture fetch offsets in the console's texels, skylanders-portal afba1dd) and SDL's audio started on the UI thread against a rare start-up crash (rexglue-sdk 807af87).

## 2026-10-27: shared libraries

- SDK 7be1eb2: two upstream fixes (kernel objects freed again, saved-game results checked properly) and resolved memory read back on request.

## 2026-10-27: shared libraries

- SDK 642311f: menus over a held game (the portal overview) present at the frame cap instead of every 100 ms: no more 10 FPS stutter in the overview.

## 2026-10-27: 0.3.0-beta.1

- The setup's version for what came since the last one: one version for all six games from now on; the portal overview (Back + Y, F5) runs smoothly while it holds the game (rexglue-sdk 642311f), and two upstream SDK fixes: kernel objects freed again, saved-game results checked properly (rexglue-sdk 4a34314, 3cb725a).

## 2026-10-28: shared libraries

- SDK cf59726: the XMA "no bits to copy" line is debug only now; library 433cb9f: the crash report also logs where the SDK's fatal errors happen (SSARecomp #42), and developer tools (pollwatch, the write watch works in Trap Team).

## 2026-10-28: shared libraries

- SDK 88d6730: cheaper register writes on the GPU thread (Spyro's Adventure Chapter 1: 51-55 -> 60 FPS at 1080p, frame cap off).

## 2026-10-28: 0.3.0-beta.2

- The setup's version for what came since the last one: the GPU thread writes registers more cheaply (rexglue-sdk 88d6730: Spyro's Adventure's Chapter 1 51-55 -> 60 FPS at 1080p), the crash report also says where the SDK's fatal errors happen (skylanders-portal 433cb9f, SSARecomp #42), and the XMA "no bits to copy" line is debug only (rexglue-sdk 7d147fe).

## 2026-10-28: 0.3.0-beta.3

- The setup's version for what came since the last one: setjmp/longjmp are wired up for codegen, so an error the game catches no longer runs into exit() and a crash (SSARecomp #42, Oilspill Island)
