# Reverse engineering

How to work out what a function in `default.xex` does, give it a real name, and get that name into
the recompiled C++. Everything runs headless from the command line; the Ghidra GUI is optional.
The tooling comes from GiantsRecomp unchanged.

## Why

After codegen every function is called `sub_<address>`. To fix crashes, find the portal wrappers
or port the ultrawide hook, we need to know what functions do. This workflow puts real names into
`config/default.toml`, and from there into the next `generated/` build.

## One-time setup (Phase 1)

```
just ghidra setup    # downloads a JDK, Ghidra, XEXLoaderWV and PyGhidra into logs/toolchain/
just ghidra import   # imports and auto-analyzes rom/default.xex into logs/ghidra_project/
```

Both are safe to re-run. Everything lands under `logs/`, which is git-ignored. `setup` needs Python 3
(`pip install pyghidra`). GiantsRecomp runs the script with `pwsh` (PowerShell 7); whether Windows
PowerShell 5.1 works is untested.

## Workflow

1. `just ghidra dump <address>`: decompiled C, callers and callees, referenced strings.
2. `just ghidra rename <address> <name> [<address> <name> ...]`: renames in Ghidra and
   updates `[functions]` in `config/default.toml` in the same call.
3. `git diff config/default.toml`: only the entries you just renamed should change.
4. `just regen-debug`: the new names appear in `generated/` and the project still builds.
5. Add a one-line comment above each new entry: what the function is and how it was identified.
   The exporter only writes `name` and `size`, so the comment is manual.
6. Commit `config/default.toml` only. Never `rom/`, `generated/` (except `rexglue.cmake`) or `logs/`.

Some functions are not found by Ghidra's auto-analysis, typically the same vtable-only functions
ReXGlue misses. `dump` then fails with "No function at ...", and the function has to be created
manually in Ghidra first.

## First targets in Spyro's Adventure

| What | Why | Starting point |
|---|---|---|
| Callers of XAM ordinals 1185/1186 | portal read/write wrappers | import table |
| Bink open/play functions | boot stalls, intro skip | strings like `.bik`, `movies/` |
| Projection-matrix builder | ultrawide | the frustum math pattern described in `src/hooks/ultrawide_hook.cpp` |

## Naming convention

Use the function's real purpose in PascalCase or snake_case, whichever reads more naturally. No
mandatory prefix. On a collision between two different functions, disambiguate by subsystem
(`Camera_UpdateFov` vs. `Hud_UpdateFov`).

## Troubleshooting

- **`setup` can't find a release asset:** the asset naming for Ghidra or XEXLoaderWV changed. Adjust
  the `-match` pattern in `Invoke-Setup` in `thirdparty/skylanders-portal/tools/ghidra_re.ps1`.
- **`import` doesn't pick the XEX loader:** add `-loader <LoaderClassName>` (see XEXLoaderWV's README)
  to the `analyzeHeadless` call in `Invoke-Import`.
- **An extension doesn't load:** the active path is `<ghidra>\Ghidra\Extensions\<name>\`, and the
  `version=` in `extension.properties` must match the installed Ghidra exactly.
- **`rename` reports conflicts:** another address already has that name. Check it with `dump` and
  rename one of them.
