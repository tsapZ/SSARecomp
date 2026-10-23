# Menus and keys

Everything the port adds on top of the game, reachable with a controller or the keyboard. The
menus come from the `feature/portal-menu` branch of the skylanders-portal fork
(`thirdparty/skylanders-portal/src/portal_rex/`), the same as in SwapForceRecomp and GiantsRecomp;
they share one look and hide all input from the game while they are open. They speak the language
the game runs in.

| What | Controller | Keyboard | Setting |
|---|---|---|---|
| Portal menu: put Skylanders and items on the portal | Guide (Xbox), Back + Start, hold Back | F6 | `portal_menu_open_buttons`, `bind_portal_menu` |
| Take the item off the portal | Back + Y | F5 | `portal_item_off_buttons`, `bind_portal_item_off` |
| Player menu: which controller is which player | L3 + R3, or F4 > Portal > "Spieler-Menü öffnen" | F9 | `player_menu_buttons`, `bind_player_menu` |
| Settings menu | Back + RB | F4 | `settings_menu_buttons`, `bind_settings_menu` |
| Achievements | F4 > Spiel > Erfolge | F7 | `bind_achievements` |
| Quit or restart the game (both ask first, "Nein" chosen) | F4 > Spiel > "Spiel beenden" / "Spiel neu starten" | Alt+F4 | |
| All settings (developer list of the SDK) | from the settings menu, last page | F10 | `bind_settings` |
| Debug overlay (FPS) | | F3 | `bind_debug_overlay` |

Button entries are comma-separated: a button (`guide`, `back`, `start`, `a`, `b`, `x`, `y`, `lb`,
`rb`, `ls`, `rs`, `up`, `down`, `left`, `right`), a chord joined with `+`, or `hold_<button>`.

## Portal menu

Opens by itself when the game asks a player for a Skylander (see [portal.md](portal.md)), or with
the buttons above. It lists this game's figures by element with the game's own portraits; X opens a
figure's details (level, gold, hat with its picture and bonuses). Magic items and adventure packs
carry the game's names. The full description of the menu is in SwapForceRecomp's
[menus.md](https://github.com/tsapZ/SwapForceRecomp/blob/main/docs/menus.md).

## Portal check

Opens by itself on "Es befinden sich zu viele Skylander" and on the "Zu viele Spielzeuge / magische
Objekte" screens: it lists the figures on the emulated portal, A marks one to take off, Continue
takes them off. See [portal.md](portal.md#portal-check).

## Settings menu

`portal_rex/settings_menu.cpp` draws the menu; `src/settings_pages.cpp` describes its pages:

- **Graphics:** preset (Performance: scale 1 + FSR; Quality: scale 2 + CAS; Best: scale 3 + CAS),
  internal resolution (`resolution_scale`), sharpening (`present_effect`), texture filtering
  (`anisotropic_override`), HD textures on or off (`hd_textures`, with a pack installed), extra edge smoothing (`swap_post_effect`), fullscreen, window size.
- **Frame rate:** `frame_rate_limit` (0 = 30 FPS like the console; higher values let the game run
  as fast as the PC allows, its speed stays the same), VSync, render path, graphics card, graphics
  interface (D3D12 or Vulkan).
- **Game:** language (automatic, English, German, French, Spanish, Italian), skip the logo movies,
  skip the opening movie.
- **Portal:** virtual / both / USB / none (`portal_mode`), the portal menu opening by itself, only
  my figures.
- **Controls:** the menu buttons, each with a few choices, keyboard and mouse, which player the
  keyboard is.
- **Advanced:** opens the SDK's list of every setting (F10).

LB / RB switch the page, up / down pick a setting, left / right change it, **Y saves** into
`ssarecomp.toml` (only the changed lines), B closes. For test runs `--config_file=<path>` makes the
app use another settings file.
