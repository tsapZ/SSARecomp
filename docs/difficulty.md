# Difficulty levels

Spyro's Adventure has no difficulty setting; the later games ask for one when a new game starts. The
port adds four, chosen for each save, with the later games' own values:

| Level | Damage you take | Enemy and boss health | Food heals | A level-up heals | XP and gold |
|---|---|---|---|---|---|
| Easy | x0.4 | x0.6 | all of it | to full | x1 |
| Adventurer | x1 | x1 | all of it | to full | x1 |
| Hard | x2.1 | x1.35 | 75 % | 20 % of full health | x1.25 |
| Nightmare | x5 | x2.5 | 60 % | 10 % of full health | x1.5 |

Adventurer is the game as it plays on the console.

- **Damage you take:** everything that hurts your Skylander goes through one place in the game (enemy
  hits, projectiles, hazards, bosses), and that place multiplies it.
- **Enemy and boss health:** damage to anything that isn't a player's Skylander is divided by the
  health multiplier, so enemies, bosses and their minions take that many times the hits; a boss's
  health bar goes down that much slower. The game's own scripted deaths still kill at once.
- **XP and gold:** every XP award and gold pickup, on top of the XP and gold multiplier cheats.
- **Battle Mode** arenas are left alone.
- **Achievements** unlock on every difficulty: the difficulty is not a cheat. (Only the cheats on F4 >
  Extras hold achievements back while they are on.)

## Where the values come from

Read from the games' own data on this PC (their decrypted executables name every field; the values
are in their system data, unpacked from the IGA archives):

| | Easy | Medium | Hard | Nightmare |
|---|---|---|---|---|
| **Swap Force** (`data/systemdata.mua`, `globalTuningData`) | | | | |
| `_difficultyFixedEnemyDamageScale` | 0.4 | 1 | 2.1 | 5 |
| `_difficultyFixedEnemyHealthScale` | 0.6 | 1 | 1.35 | 2.5 |
| `_difficultyFixedFoodHealthGainScale` | 1 | 1 | 0.75 | 0.6 |
| `_difficultyLevelUpHealthRecovery` | 1 | 0.6 | 0.2 | 0.1 |
| **Imaginators** (`LooseData/systemData.igz`) | | | | |
| `_difficultyFixedEnemyDamageScale` | 0.4 | 1 | 1.8 | 3.5 |
| `_difficultyFixedEnemyHealthScale` | 0.5 | 1 | 1.5 | 2.5 |
| `_difficultyFoodHealthGainPercentage` | 100 | 75 | 50 | 33 |
| `_difficultyShieldRepairGainPercentage` | 100 | 75 | 50 | 40 |
| `_difficultyEnemyAttackCooldownMultiplier` | 1.2 | 1 | 0.83 | 0.78 |
| `_xpScalar*`, `_moneyScalar*` | 1 | 1 | 1.25 | 1.5 |

The port uses Swap Force's damage, health, food and level-up values (Adventurer keeps Spyro's
Adventure's full level-up heal) and Imaginators' XP and gold scalars, the "+50% Rewards" of its
Nightmare. Trap Team works its multipliers out in its visual scripts ("Difficulty - Boss HP Mult",
"Difficulty - CriminalEnemy Damage Mult", "Difficulty - Hazard Damage Mult", "Difficulty XP
Multiplier" in its `level.bld`): the names are there, the values are inside the script graphs and
couldn't be read. Giants and Trap Team also have achievements for finishing the story on Nightmare;
Spyro's Adventure's achievement list is the game's own.

## The picker

Starting a new game (in an empty slot or over an old save) opens the picker when the game writes the
new save, on the main menu before the opening movie; the game waits for the choice. It follows the
later games' "CHOOSE YOUR DIFFICULTY" screen: a starry blue backdrop, the title in gold, four round
portraits in gold rings, from the mildest to the hardest (Gill Grunt for Easy, Stealth Elf for
Adventurer, Spyro for Hard, Kaos for Nightmare; the game's own HUD portraits and Kaos's dialogue
picture). The chosen one grows and glows, and only its name is shown, under the portraits, with its
description in a red banner with a gold frame: who it is for, what it changes, and the rewards
("+50% Rewards"). A blue Accept button sits below, with the game's own A picture (and a Back button, B, when the
picker is opened from F4 > Extras; a new game waits for a choice, so it has none). Left / right (stick, d-pad, arrow keys)
or pointing with the mouse choose, A, Start, Enter or a click accepts. English and German.

**Portraits of your own:** PNG files named `easy.png`, `adventurer.png`, `hard.png` and
`nightmare.png` in a `difficulty` folder of the data folder (`Documents\Skylanders\Spyro's
Adventure\difficulty`, or the folder Core of Light uses) replace the game's portraits in the circles:
square, transparent around the character, about 512x512 (the circle shows them cut round). They are
read at start; any that is missing keeps the game's own. None come with the port.

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

`src/hooks/difficulty_hook.cpp` (the hooks, with how they were found), `src/hooks/multiplier_cheats_hook.cpp`
(XP and gold) and `src/game/difficulty.cpp` (the choice and the picker). Midasm hooks in
`config/default.toml`:

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
  the damage rules and every other hurt ends. r29 is the component, f29 the damage: times the damage
  multiplier when the component is a player's Skylander's (found through the player list, as the
  no-health-loss cheat does), divided by the health multiplier otherwise.
- **Food** (0x8273B44C, the pickup handler `sub_8273B3D8`): the health gain is scaled.
- **Level-up heal** (0x8275DDD4, in `sub_8275DBC8`, which sets the XP and level and then refills the
  health): a hurt Skylander in play gets back part of its full health instead of all of it.
- **XP and gold** (0x8275A564 and 0x8275A454, the XP and gold gain functions the multiplier cheats
  hook): the award is scaled.

Nothing is written to the game's memory. The log says what happens: `Difficulty: Skylanders - Slot 1
is played on Hard (damage taken x2.1, enemy health x1.35, rewards x1.25)`, the choice, a new game,
and the first scaled hit each way, food, level-up and award (once each).
