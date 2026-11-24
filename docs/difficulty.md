# Difficulty levels

Spyro's Adventure has no difficulty setting; the later games ask for one when a new game starts
(Giants, Swap Force and Trap Team). The port adds four, chosen for each save:

| Level | Enemies | What it means |
|---|---|---|
| Easy | x0.6 | An enemy's hit does 60 % of its damage; enemies take 1/0.6 of the damage (they go down in 60 % of the hits) |
| Adventurer | x1.0 | The game as it plays on the console |
| Hard | x2.0 | Enemies hit twice as hard and take twice the hits |
| Nightmare | x3.0 | Enemies hit three times as hard and take three times the hits |

## The picker

Starting a new game in an empty save slot opens the picker as soon as the game creates the save,
before the game goes on (it waits for the choice). It follows the later games' difficulty screens:
a title, the four levels side by side from the mildest to the hardest, each with an emblem (one to
four flames on a shield), its name and its multiplier; the chosen card stands raised with a gold
frame, its description below. Left / right (stick, d-pad, arrow keys or the mouse) choose, A, Start,
Enter or a click starts. The menus' look (`portal_rex/menu_draw.h`), in English and German.

- **F4 > Extras > Change this game's difficulty** opens the picker for the save being played (B
  goes back without changing it).
- **F4 > Extras > Ask for a difficulty on a new game** (`difficulty_ask_new_game`, on): off, new
  games are Adventurer and no picker opens.

Saves from before this, and saves of a player who never chose, are Adventurer: nothing changes for
them.

## Where it is kept

`difficulty.txt` in the user data folder (`Documents\Skylanders\Spyro's Adventure`), a line per slot
save, named by its folder:

```
Skylanders - Slot 1=hard
Skylanders - Slot 2=easy
```

The game's own saves are left as they are; a save deleted in the game and started anew in the same
slot asks again.

## How it works

`src/hooks/difficulty_hook.cpp` (the hooks) and `src/game/difficulty.cpp` (the choice and the
picker). Three midasm hooks in `config/default.toml`:

- **The save** (0x822DD4C4): the game's only call of `XamContentCreateEx` (in `sub_822DD460`). The
  package's folder name (`XCONTENT_DATA` +264) says which slot save it is, the flags (low four bits)
  whether the game may create it. A slot save opened or created becomes the one played; one that may
  be created and whose folder isn't there yet is a new game, and the hook holds the game's thread
  there until the picker has its answer (the UI thread keeps drawing the menus meanwhile).
- **An enemy's hit** (0x82761CC8, in `SpyroEnemyDamageRule`, `sub_82761AF0`): the hit's damage (f31)
  and element bonus (f0) right before they are stored to the hit's result are multiplied.
- **A Skylander's hit** (0x827621D4, in `SpyroDamageRule`, `sub_82761F80`): the damage (f31, with a
  critical hit's bonus) and element bonus (f30) are divided, so an enemy takes that many times the
  hits: the enemies' health scaled without touching each enemy.

Both are rounded to whole points as the game's own are, and a hit that did damage still does at
least 1. Battle Mode arenas (`PvP_Level_*`), where Skylanders fight each other, are left alone.
Nothing is written to the game's memory.

The log says what happens: `Difficulty: Skylanders - Slot 1 is played on Hard (enemies x2.0)`, the
choice, and the first hit each way that was scaled (`Difficulty: x2.0 scaled an enemy's hit (40 ->
80)`).
