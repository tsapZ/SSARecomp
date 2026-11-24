# Difficulty levels

Spyro's Adventure has no difficulty setting; the later games ask for one when a new game starts
(Giants, Swap Force, Trap Team and on). The port adds four, chosen for each save:

| Level | Damage you take | Enemy and boss health | Food heals | A level-up heals |
|---|---|---|---|---|
| Easy | x0.6 | x0.6 | all of it | to full |
| Adventurer | x1 | x1 | all of it | to full |
| Hard | x2 | x2 | 75 % | 20 % of full health |
| Nightmare | x4 | x4 | 60 % | 10 % of full health |

Adventurer is the game as it plays on the console.

- **Damage you take:** everything that hurts your Skylander goes through one place in the game (enemy
  hits, projectiles, hazards, bosses), and that place multiplies it.
- **Enemy and boss health:** damage to anything that isn't a player's Skylander is divided by the
  multiplier, so enemies, bosses and their minions take that many times the hits; a boss's health bar
  goes down that much slower. The game's own scripted deaths still kill at once.
- **Food and level-ups** follow Swap Force's difficulty table (below).
- **Battle Mode** arenas are left alone.
- **Achievements** unlock on every difficulty: the difficulty is not a cheat. (Only the cheats on F4 >
  Extras hold achievements back while they are on.)

## What the other games change

Read from the games' own data (the decrypted executables' type and field names, and Swap Force's
`data/systemdata.mua`, `globalTuningData`, from its `.pak` archives):

| Swap Force | Easy | Medium | Hard | Nightmare |
|---|---|---|---|---|
| `_difficultyFixedEnemyDamageScale` | 0.4 | 1 | 2.1 | 5 |
| `_difficultyFixedEnemyHealthScale` | 0.6 | 1 | 1.35 | 2.5 |
| `_difficultyFixedFoodHealthGainScale` | 1 | 1 | 0.75 | 0.6 |
| `_difficultyLevelUpHealthRecovery` | 1 | 0.6 | 0.2 | 0.1 |

- Giants and Trap Team: a difficulty per save (`_gamerDifficulty`), and achievements for finishing
  the story on any difficulty and on Nightmare.
- SuperChargers and Imaginators add enemy stat bonuses on Nightmare (`_nightmarePower`,
  `_nightmareArmor`, ...), more XP and gold on Nightmare (`_xpScalarNightmare`,
  `_moneyScalarNightmare`), a respawn damage (`_difficultyRespawnDamage`) and, in Imaginators, a
  smaller food heal (`_difficultyFoodHealthGainPercentage`). Their data is binary (IGZ), so the values
  weren't read.

The damage and health multipliers here are the ones asked for (x0.6, x1, x2, x4, the same for damage
and health); the food heal and the level-up heal are Swap Force's, except that Adventurer keeps
Spyro's Adventure's full level-up heal.

## The picker

Starting a new game (in an empty slot or over an old save) opens the picker when the game writes the
new save, on the main menu before the opening movie; the game waits for the choice. It follows the
later games' difficulty screens: a title, the four levels side by side from the mildest to the
hardest, each with an emblem (one to four flames on a shield), its name and its multiplier; the
chosen card stands raised with a gold frame, its description below. Left / right (stick, d-pad, arrow
keys or the mouse) choose, A, Start, Enter or a click starts. The menus' look
(`portal_rex/menu_draw.h`), in English and German.

- **F4 > Extras > Change this game's difficulty** opens the picker for the save being played (B goes
  back without changing it).
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

The game's own saves are left as they are; a new game started in a slot asks again.

## How it works

`src/hooks/difficulty_hook.cpp` (the hooks, with how they were found) and `src/game/difficulty.cpp`
(the choice and the picker). Four midasm hooks in `config/default.toml`:

- **The save** (0x822DD4C4): the game's only call of `XamContentCreateEx` (in `sub_822DD460`). The
  package's folder name (`XCONTENT_DATA` +264) says which slot save it is, the flags (low four bits)
  whether the game writes it (1, 2, 5) or opens it (3, 4). A new game writes its slot save while the
  main menu (the FrontEnd level) is loaded, over an old save in that slot too; in play the game saves
  only inside the levels. So a slot save written from the main menu (once per stay there), or one
  created that wasn't there, is a new game, and the hook holds the game's thread until the picker has
  its answer (the UI thread keeps drawing the menus meanwhile). Any slot save opened or written
  becomes the one played.
- **Damage** (0x82827D80, in `sub_828279A0`, a health component's take-damage): every health
  component's health is set by `sub_82826C20`, and this is its only subtraction, where every hit of
  the damage rules and every other hurt ends. r29 is the component, f29 the damage: times the
  multiplier when the component is a player's Skylander's (found through the player list, as the
  no-health-loss cheat does), divided by it otherwise.
- **Food** (0x8273B44C, the pickup handler `sub_8273B3D8`): the health gain is scaled.
- **Level-up heal** (0x8275DDD4, in `sub_8275DBC8`, which sets the XP and level and then refills the
  health): a hurt Skylander in play gets back part of its full health instead of all of it.

Nothing is written to the game's memory. The log says what happens: `Difficulty: Skylanders - Slot 1
is played on Hard (enemies x2.0)`, the choice, and the first few scaled hits, food and level-ups
(`Difficulty: x2.0 damage to a Skylander (20.0 -> 40.0)`).
