# Roadmap

Where the project stands, what is done, what comes next. Details of every session are in
[STATUS.md](STATUS.md); each phase is a GitHub milestone. A step only counts as done when it was
observed in a real build or a real play test, not inferred.

## Overview (2026-10-20)

| Phase | State | Issues | What is left |
|---|---|---|---|
| 0 Repository and scaffold | ✅ done | 2/2 | – |
| 1 Recompile and boot | ✅ done | 3/3 | – |
| 2 In-game | 🟡 Chapter 1 | 3/5 | the levels after Chapter 1 (#9), the owner's play test (#24) |
| 3 Portal | 🟡 almost | 4/6 | the real USB portal (#13), two players with two pads (#14) |
| 4 Menus and polish | 🟡 in progress | 4/8 | upgrades in the details view (#20), adventure packs (#21), ultrawide (#22), installer (#23) |

**Where we are:** Chapter 1 plays with voices and cutscenes, the emulated portal and the portal
menu work, the menu opens by itself at the game's portal screens and shows the game's own
pictures, names and hats. The game runs at 30 FPS like the console, or at 50-91 FPS with a higher
frame rate limit.

## Now, next, later

| When | What | Who |
|---|---|---|
| **Now** | Play test (#24): Chapter 1 and further, the menu at "Bitte setze einen Skylander", the portal check with too many Skylanders and with an item plus an adventure pack, player 2 with A on pad 2, 200 FPS, a figure with a hat, the real USB portal | you |
| Next | Upgrades in the details view: find where the game keeps the prices (#20) | me |
| Next | Adventure packs: their own label ("Ortselement") and pictures (#21) | me |
| Then | Play further (#9), ultrawide (#22), installer (#23) | me |
| After all games | The hub, [Skylanders: Core of Light](https://github.com/tsapZ/SkylandersCoreOfLight) | both |

## Phase 0: repository and scaffold ✅

- [x] Disc extracted, `default.xex` hash pinned: title `415608AB`, v0.0.0.2 (#1)
- [x] Scaffold from SwapForceRecomp, same SDK and portal fork branches (#2)

## Phase 1: recompile and boot ✅

- [x] Codegen and build, no config entries needed (#3)
- [x] Black screen on boot fixed in the SDK (`fix/ring-buffer-reinit`) (#4)
- [x] Logo movies skipped, the opening movie optional (#6)

**Done when:** the title screen and the menus come up. Observed 2026-10-20.

## Phase 2: in-game 🟡

- [x] Voiced lines no longer stall their cutscene, fixed in the SDK (`fix/xma-stream-end`) (#5)
- [x] German and the other languages, `auto` by default (#7)
- [x] Chapter 1 playable, saves list their progress (#8)
- [ ] The levels after Chapter 1, the Ruins, saving and loading (#9)
- [ ] The owner's play test (#24)

**Done when:** the first levels are played through with a controller.

## Phase 3: portal 🟡

- [x] Emulated portal: figures read, swapped during play, progress written back (#10)
- [x] The portal menu opens by itself at "Bitte setze einen Skylander", for player 1 and 2 (#11)
- [x] The portal check on "zu viele Skylander" and "zu viele Spielzeuge" (#12)
- [x] One figure folder for every game (#15)
- [ ] The real USB portal and the hybrid mode (#13)
- [ ] Two players with two pads (#14)

**Done when:** both portals work and two players can play.

## Phase 4: menus and polish 🟡

- [x] The game's portraits and element symbols in the portal menu (#16)
- [x] The game's names for magic items and adventure packs (#17)
- [x] Hats in the details view (#18)
- [x] Frame rate above 30 without changing the game's speed (#19)
- [x] Upgrades in the details view (#20): built; prices to check against the game
- [ ] Adventure packs with their own label and pictures (#21)
- [ ] Ultrawide (#22)
- [ ] Installer tested on a clean Windows VM (#23)

**Done when:** the menus show everything the game's own screens do, and the installer works on a
clean PC.

## From the play tests (2026-10-21)

- [x] Player 2 leaves with RS (#27)
- [x] No sidekicks in the menu (#28)
- [x] Controller picture on the controls page (#29, drawn) and a keyboard view
- [ ] Animations above 30 FPS (#26): confirmed for the portal screen's effect (frame-tied);
      the effect's update still to be found
- [ ] Picture quality: blurry, a shadow out of the ground (#30); the native 3D picture is an option since 2026-10-21, the shadow is still to look at
- [ ] Pictures for the items without one (#31)
- [x] Upgrades in the details view (#20): built; prices to check against the game
