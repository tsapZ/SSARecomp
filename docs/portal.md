# Portal

The Portal of Power comes from the portal library (`thirdparty/skylanders-portal`, our fork's
branch `feature/portal-menu`), the same code as in SwapForceRecomp and GiantsRecomp. Its protocol,
the figure files and the menus are described in SwapForceRecomp's
[portal.md](https://github.com/tsapZ/SwapForceRecomp/blob/main/docs/portal.md); this page covers
what is particular to Spyro's Adventure.

## Modes

`portal_mode` (F4 > Portal):

| Value | What |
|---|---|
| `software` | the emulated portal, filled from the portal menu (default) |
| `hybrid` | the emulated portal and the real USB portal together |
| `usb` | the real USB portal only |
| `none` | no portal |

## How the game talks to the portal

Unchanged from Swap Force: the game resolves `XamInputNonControllerGetRaw` / `SetRaw` at run time
(`XexGetProcedureAddress`, ordinals 0x4A1 / 0x4A2) instead of importing them, then resets and
activates the portal, polls its status, sends `A 01` and `S` every 10 seconds, and reads every
block of a figure with `Q` (the slot byte is `0x10` / `0x20` + slot). It writes a figure's progress
back while it is on the portal, so the figure file keeps it.

## Figures

`Documents\Skylanders\Figures`, one folder for every game (portal fork `SharedFiguresDir`), with a
subfolder per game. The first start copies the old `Documents\ssarecomp\figures` into it and leaves
a note there. Test runs with a copied `--user_data_root` get their own folder next to the copy.

## The menu at the game's portal screens

`src/game/portal_prompt_watch.cpp` polls every 30 ms what the game's portal screen shows and opens
the portal menu for the player it asks, or the portal check when there is too much on the portal.

- **Is the screen up?** The prompt object, a function-local static of `sub_82205D18` at
  `0x82C569B0`, has the mode at +56: 2 while the portal screen is up, 4 while a figure comes in,
  0 in play. Its text is chosen every frame by `sub_827E78F0`.
- **What does it say?** The HUD (a function-local static of `sub_82204058` at `0x82C56AF8`) keeps
  its portal message at +3260; the text element that +376 of it points to holds the text's hash at
  +432 (`sub_82695070` -> `sub_8267B808`). The hashes are CRC-32s of the keys in
  `text\<lang>\global.lxb`:

| Hash | Text (German) | The watch opens |
|---|---|---|
| `0x049B83C4` | Spieler 1: Bitte setze einen Skylander auf das Portal of Power. | the menu for player 1 |
| `0x9D92D27E` | Spieler 2: Bitte setze einen Skylander ... | the menu for player 2 |
| `0xCE3BDD24` | Es befinden sich zu viele Skylander auf dem Portal of Power. | the check for Skylanders |
| `0xC23393BE` | Zu viele Spielzeuge auf dem Portal of Power. (different kinds, e.g. an item and an adventure pack) | the check for items |
| `0xA66A468E`, `0xF568FF72`, `0x4F826D46` | Zu viele magische Objekte / Haustiere / Ortselemente ... | the check for items |

Reading the text instead of the prompt object's fields matters: for "too many toys" the object
looks like the request for a Skylander (screen field +52 = 0, player 2's figure bytes empty), so a
field check took it for player 2's request.

Player 2 joins by pressing A on their controller ("Drücke A zum Mitspielen"); the game then asks
them for a Skylander and the menu opens for them.

## Portal check

Lists everything on the emulated portal, marks what there is too much of, and takes off what the
player picks; B closes it, and while the error stays it comes back after 8 s. The title names what
is doubled ("Zu viele magische Objekte auf dem Portal"; this game has no sidekicks).
