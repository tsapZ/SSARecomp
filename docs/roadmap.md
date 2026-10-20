# Roadmap

Spyro's Adventure Recompiled, the first Skylanders game, built the same way as SwapForceRecomp.

| Phase | State | What |
|---|---|---|
| 0 Repository and scaffold | ✅ | scaffold from SwapForceRecomp, disc extracted, xex pinned |
| 1 Recompile and boot | ✅ | codegen, boot to the main menu (SDK ring buffer fix), the first level's cutscenes with voices (SDK XMA fix) |
| 1.1 Portal | ✅ | emulated portal, portal menu (Guide), figures swapped during play |
| 2 In-game | ⬜ | the first levels playable, saves, figures |
| 3 Polish | ⬜ | high frame rate (not the swap interval as in Swap Force: docs/game/spyros-adventure.md), resolution, ultrawide, settings |

## Now, next, later

| When | What | Who |
|---|---|---|
| **Now** | Playing further (levels, saves, the hub); what still differs from the console | me |
| Next | Details view: upgrades (names and descriptions are in the text table, `<Char>Up00`-`Up10`; costs not found yet: Giants' prices 500 ... 4000 are in none of this disc's data files, the unpacked global.str or the executable as a table; GiantsRecomp shows these Skylanders' upgrades with Giants' prices and pictures); the "too many magic items" screen | me |
| Later | This game's own pictures and texts for the menus (other file formats than Swap Force) | me |
| After this | Giants, then the Skylanders hub | both |
