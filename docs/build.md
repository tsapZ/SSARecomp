# Build fixes and testing

How the recompile is corrected when codegen gets something wrong, and how the project is tested.
See [development.md](development.md) for building and [reverse-engineering.md](reverse-engineering.md)
for finding out what a function does.

## Codegen fixes (`config/default.toml`)

The file starts empty. Entries are added one at a time in Phase 1, each because a concrete error
showed that it is needed. The entry types known from GiantsRecomp:

- **`[functions]`**: pins a function's start address and size. Needed for small functions that are
  only reached through a virtual-table call (`bctr`): the analyzer only splits functions on `blr`
  and tail-call `b`, so these get folded into their neighbour. The Ghidra `rename` command also
  writes names here.
- **`[[switch_tables]]`**: gives the real entries of a jump table whose index has no bounds check,
  so the analyzer under-counts it and the generated switch traps at runtime.
- **Midasm hooks:** call into our own C++ at a guest address (implemented in `src/hooks/`).

### Comment convention

Every entry, or group of entries with the same reason, gets a comment that answers three
questions: what is at the address, how it was found, why the fix is correct. Example from Giants:

```toml
# Jump table at 0x82166BEC was detected with 8 entries; it has 11 (found by reading the table until a
# non-code word). The index has no bounds check the analyzer could use, so the generated switch trapped.
[[switch_tables]]
address = 0x82166BE8
register = 19
labels = [0x82166C18, ...]
```

All addresses belong to one exact `default.xex`. That is why the SHA-256 check exists.

## Kernel stubs (`src/game/kernel_stubs.cpp`)

When the boot log reports an import the runtime does not implement, add a stub:

```cpp
REX_EXPORT_STUB_RETURN(ExampleImport, 0)  // returns STATUS_SUCCESS; result unused by game
```

The comment must say why that return value is safe, for example "caller ignores the result" or
"game only checks for success". A stub that guesses is a bug waiting to happen; check the caller
in Ghidra first.

## The fix loop (Phase 1 and 2)

1. `just regen-debug`, then `just play-debug`.
2. Read the crash or the last log lines (`just logs-debug -n 200`).
3. Find the function involved: `just ghidra dump <address>`.
4. Decide: codegen fix in `config/default.toml`, a kernel stub, or a hook.
5. Add the fix with its comment, rebuild, run again.
6. One fix per commit (`fix: ...`), and a line in [STATUS.md](STATUS.md) at the end of the session.

If a problem looks like a ReXGlue bug rather than a game quirk, note it in STATUS.md and do not
patch the submodule on `main`; use a clearly named branch.

## Testing

- `just test-debug` / `test-release` run ctest over the build directory.
- `tests/` holds this project's tests: XEX verification and the check tool's command line.
- `thirdparty/skylanders-portal` brings its own tests (figure encode/decode, catalog scanner,
  `SoftwarePortal` protocol state machine, Xbox frame conversion). They need neither ReXGlue nor the
  game.
- Anything that needs the real game, a real figure or real USB hardware is tested by hand, and the
  result goes into STATUS.md.
