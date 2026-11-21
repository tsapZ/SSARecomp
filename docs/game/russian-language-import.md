# The Russian language on the supported 0.0.0.2 build

The Russian disc is a separate 0.0.0.4 build (kept as the reference in
[#51](https://github.com/tsapZ/SSARecomp/pull/51)), but its language is data, and the data
formats match. The supported 0.0.0.2 executable can run the
Russian text, fonts and voices; no second executable is needed for the language.

The obstacle is the 0.0.0.2 text renderer. It knows ASCII and U+00A1..U+00FF only. The Russian
table is UTF-8, and the Russian fonts carry the 64 Cyrillic letters as extra glyphs. The renderer
draws neither, even when the table is rebuilt in the supported layout. The same strings draw in
the 0.0.0.4 build because its renderer knows the extra glyphs.

`tools/add_russian_language.py` bridges the two:

- It reads the supported 0.0.0.2 data and the Russian 0.0.0.4 data and checks both
  `default.xex` SHA-256 values before writing anything. The supported hash is
  `e195f5130c1e23fb4654baf7c383ef97d02c89aeb7d4389705ae31235646ef7b`; the Russian one is
  `0598a9638d92ec0070f4fb701323c132427dd76af3cd5a0ebc1d61f211b84041`.
- For every supported `text/eng/*.lxb` with a Russian counterpart, it keeps the supported
  table's keys and order and substitutes the Russian string. Each Cyrillic letter moves to the
  Latin-1 codepoint with the same cp1251 byte (`U+0410..U+044F` -> `U+00C0..U+00FF`; `Ё`/`ё` ->
  `U+00A8`/`U+00B8`). Dashes, ellipses, `№` and no-break spaces are folded to characters the
  font has; unmapped characters become `?`.
- It copies the 64-glyph Cyrillic block of each Russian `.fnt` into the font's Latin-1 slots
  (entries 225..288), and `Ё`/`ё` into 201/217. The Russian `.apk` glyph pages are copied too, so
  those slots draw the Cyrillic glyphs at the shifted codepoints.
- It copies `audio/rus/voice.bnk`, `ui/rus/uituning.lxb` and `movies/rus/legal.bik` to the
  English paths.
- It writes an unshifted `text/eng/global.port.lxb` for the port's own menus and status lines.
  The guest reads the shifted `global.lxb`; the port reads `global.port.lxb` when it exists.
- `--apply <copy>` copies the overlay into a copy of the supported data. It refuses a target
  whose `default.xex` is not the supported hash, removes a destination symlink before writing
  (so a symlink farm cannot be written through), and leaves a `text/eng/.russian-import` marker.

The guest still runs the supported 0.0.0.2 `default.xex`. No game data is in this repository.

## How to use it

1. Extract the supported 0.0.0.2 disc into a copy of the data.
2. Extract the Russian 0.0.0.4 disc.
3. Build the overlay:

   ```
   python3 tools/add_russian_language.py \
     --supported /path/to/supported-data \
     --russian /path/to/russian-data \
     --out /path/to/overlay
   ```

4. Apply it to a copy of the supported data:

   ```
   python3 tools/add_russian_language.py \
     --supported /path/to/supported-data \
     --russian /path/to/russian-data \
     --out /path/to/overlay \
     --apply /path/to/supported-copy
   ```

   or copy the overlay's files over the copy by hand. The tool prints what it merged and writes a
   `README.txt` with the two source hashes into the overlay.

5. Start the normal build with `--game_data_root=/path/to/supported-copy --language=english`.
   The guest text works on any 0.0.0.2 build. The port's own status lines and menus need the build
   with the `global.port.lxb` fallback (this PR); without it they read the shifted table and
   show Latin-1 mojibake.

The unit tests need no game data:

```
python3 -m unittest discover -s tools -p 'test_*.py'
```

## What it imports

| Path | Source | Note |
|---|---|---|
| `text/eng/global.lxb` | `text/rus/global.lxb` | supported keys and order; shifted |
| `text/eng/*.lxb` (60 more) | `text/rus/*.lxb` | levels and challenges; shifted |
| `text/eng/global.port.lxb` | `text/rus/global.lxb` | unshifted; for the port's own text |
| `textures/{arial,bleeker,lt,ltnob,robotic}.fnt` | Russian `Textures/*.fnt` | Cyrillic block remapped |
| `textures/{...}.apk` | Russian `Textures/*.apk` | the pages those glyphs draw |
| `audio/eng/voice.bnk` | `audio/rus/voice.bnk` | Russian voices |
| `ui/eng/uituning.lxb` | `ui/rus/uituning.lxb` | UI tuning |
| `movies/eng/legal.bik` | `movies/rus/legal.bik` | legal movie |

The supported table has 2,335 keys; the Russian table has 2,342. The 7 Russian-only keys are not
in the supported frontend and are left out. A lookup of one shows the game's `MissingText`
fallback.

## Tested

On 2026-11-21, Linux, clang 23, Intel Iris Xe (Vulkan, 1x), hidden window, muted audio, software
portal, supported 0.0.0.2 `default.xex`:

- The tool's 7 unit tests pass (shift, merge, font layout, remap, other-font layout).
- The tool generated the overlay from the extracted discs. The tool farm's `level_027.lxb` is
  byte-identical to the earlier manual farm's; the shifted `global.lxb` differs from the manual
  one only in punctuation handling (the tool folds dashes, ellipses, `№` and no-break spaces).
- Title screen (`НАЖАТЬ ... ЧТОБЫ НАЧАТЬ`), the autosave notice and the save notice are Russian.
  The tool farm's menu run: 0 trouble, 50 FPS.
- A new game reaches Level_027 and its intro. The tool farm: 0 trouble, 60 FPS. The earlier
  manual farm's Level_027 run showed the Russian `Skip` prompt; the tool farm's level table is
  byte-identical to it.
- A save from that run loads from Continue into Level_027 with the build with the
  `global.port.lxb` fallback:
  `Game text: 2335 entries from .../text/eng/global.port.lxb`, `Where: Level_027`, and
  `Discord status: - / Расколотый остров`; 0 trouble, 26.7 FPS while the level loads.
- The earlier unmodified 0.0.0.2 run (manual farm) reads the shifted `global.lxb` and logs the
  port's level name as `Ðàñêîëîòûé îñòðîâ`; the fallback fixes the port's own text.

## Limits

- The font remap overwrites the Latin-1 glyph slots with Cyrillic. English strings that use
  accented Latin-1 letters in the imported table can draw the Cyrillic glyph in that slot; the
  game's English text is otherwise ASCII. The port's own text uses the unshifted
  `global.port.lxb`, and its menus fall back to a font with Cyrillic for those codepoints.
- The imported table is a Russian table: the shared keys are Russian in every language. Use a
  separate copy of the data for each language.
- This is not the 0.0.0.4 build. It does not add the 0.0.0.4 code hooks or the native Cyrillic
  renderer; it uses the supported build's hooks and the shift/remap above. The 0.0.0.4 build
  remains the native path for the renderer and the 0.0.0.4 code work.
- The tool writes files only; it does not download or ship game data. The user supplies both
  discs. The `--apply` target should be a copy: the tool overwrites the files it imports.
