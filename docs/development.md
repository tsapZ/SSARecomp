# Development

Building SSARecomp from source. There is no playable build yet; see [STATUS.md](STATUS.md).

## What you need

1. **Windows 10 or 11, 64-bit,** with a DirectX 12 capable graphics card. The CMake presets for
   Linux and macOS come from the template and are untested here.
2. **Your own copy of the game:** the Xbox 360 disc of Skylanders: Spyro's Adventure, as an ISO or an
   extracted folder. Which exact version is supported is decided in Phase 1, see
   [game/spyros-adventure.md](game/spyros-adventure.md).
3. **A controller.** An Xbox controller works. Keyboard and mouse via `mnk_mode`.
4. **Build tools:** Visual Studio 2022 with "Desktop development with C++" and "C++ Clang tools for
   Windows" (clang 18 or newer), CMake 3.25+, Ninja, Git, [just](https://github.com/casey/just)
   and Python 3.

## Setup

**1. Turn on symlinks (once).** One of the libraries uses symbolic links. Turn on **Developer Mode**
(Settings → System → For developers), then:

```
git config --global core.symlinks true
git config --global core.longpaths true
```

**2. Get the code with its submodules:**

```
git clone --recursive https://github.com/tsapZ/SSARecomp.git
cd SSARecomp
```

Already cloned without `--recursive`? Run `just submodules`.

**3. Open the right prompt.** From now on, run everything in the **x64 Native Tools Command Prompt
for VS 2022** (Start menu). Elsewhere CMake does not find the compiler.

**4. Put the game files in place.** `rom\default.xex` must exist, next to the rest of the disc:

```
extract-xiso -d rom "path\to\Spyro's Adventure.iso"
```

Options must come **before** the file name. A `-d` after it is ignored, and the disc lands in a new
folder named after the ISO in the current directory, which `.gitignore` does not cover.

An already extracted folder is copied as is. GOD containers (folders with hex names) are not
supported directly. `rom/` is git-ignored.

**5. Build.** The first build takes a long time because it also builds the ReXGlue SDK.

```
just build-release      # configure on first run, then build
just regen-release      # codegen from rom/default.xex, reconfigure, rebuild
just play-release
```

Without `just`:

```
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release --target ssarecomp_codegen
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release
```

The codegen step turns your `default.xex` into C++ on your machine; that code is never uploaded.
Use the `debug` presets/recipes when chasing a crash, `release` for playing.

Without a ROM, configuring still works (about 2 minutes, tested): CMake prints "No recompiled code
yet" with the codegen steps and writes the build files. There is just no game code to compile until
codegen has run.

Keep the checkout path short. In a deep folder, CMake warns that FidelityFX object paths may exceed
its 250-character limit.

## Everyday commands

| Command | What it does |
|---|---|
| `just` | list all recipes |
| `just build-debug` / `build-release` | configure if needed, build |
| `just regen-debug` / `regen-release` | rerun codegen after changing `config/default.toml` or the dump |
| `just play-debug` / `play-release` | build and start; extra arguments are passed on |
| `just test-debug` / `test-release` | unit tests (ctest) |
| `just logs-debug -f` | follow the newest log file |
| `just clean` | remove build output |

## Testing without touching your saves

Automated or experimental runs write saves and figure progress. Point them at a copy:

    just play-release --user_data_root=C:\path\to\test-userdata

Copy `Documents\ssarecomp` (saves under `B13EBABEBABEBABE\`) into that folder first, and the
shared figure folder `Documents\Skylanders\Figures` into `Skylanders\Figures` next to it: the
game looks for figures in "Skylanders\Figures" beside its data folder, so a copied
`--user_data_root` gets its own figures (and a first start copies `<copy>\figures` there). Never
test against the real user folder.

### Scripted input

`--input_script=<file>` plays a fixed controller timeline as an extra input device, independent of
the window focus (the keyboard emulation only reads keys while the game window has the focus, so a
system dialog in the foreground breaks keyboard-driven test runs). One command per line or `;`:

    wait 18
    tap start            # title screen
    wait 4
    tap a                # story mode
    hold right 600       # d-pad, in milliseconds
    stick 0 1 1500       # left stick forward for 1.5 s
    fps 10               # log the guest frame rate over 10 s
    renderdoc 2          # capture the next 2 guest frames with an attached RenderDoc
    log reached level    # mark the point in the log
    scan 00000500 000002D0   # log where these big-endian words are in guest memory
    poke 40008FD0 00000500   # write a big-endian word to guest memory
    player 2                 # the scripted controller plays as player 2 from now on
    quit

Buttons: `guide a b x y start back up down left right lb rb ls rs lt rt`, several at once joined
with `+` (`tap back+y`, `tap ls+rs`); `key F4` presses a key for the key binds (opens an overlay
or menu); `watch <addr> [<value>]` / `unwatch` log which guest functions (with their call chain)
write a guest address, through a hardware write breakpoint (`src/dev/write_watch.cpp`), with the
guest registers and 1 KB of guest stack at the first hit; `watch <addr> <value> <snap> <words>` also
copies guest memory at the first hit (e.g. the text a parser is reading), `watchread <addr>` traps
reads too, `dump <addr> <words>` logs guest memory, and `stacks [<samples>]` logs where every
thread is (call chains, recompiled frames named by guest function): finds where the game hangs
without a debugger. `save <addr> <bytes> <file>` writes guest memory to a file: snapshots at
different moments (prompt, play, pause menu), compared offline, show which fields belong to a
state. For data whose heap address changes from run to run, `watch`, `watchread` and `dump` take
`hit<n>[+<off>]` (the n-th hit of the last `scan`) or `*<addr>[+<off>]` (the pointer stored there)
as the address. `remove <slot>` takes a figure off the emulated portal again. (Taken over from
GiantsRecomp, where they found its portal prompt.) Full syntax in
`src/dev/input_script.h`. For RenderDoc, start the game through `renderdoccmd capture`; the capture
spans whole guest frames (RenderDoc's own triggers capture host frames, and one guest frame's GPU
work is spread over several of them).

`tools/run_scripted.ps1` starts the game for such a run and saves screenshots of its window on a
timeline (`PrintWindow`, so other windows in front don't matter):

    tools/run_scripted.ps1 -OutDir logs\shots -Shots "wait 50; shot card; full card_big" `
        -GameArgs "--user_data_root=C:\path\to\test-userdata --input_script=card.txt --portal_mode=software"

### Graphics debugging with RenderDoc

For rendering bugs that only one render target path shows (RTV vs ROV), compare RenderDoc captures
of the same guest frame. RenderDoc is not installed; use the portable build in the gitignored
toolchain folder:

    curl -L -o rd.zip https://renderdoc.org/stable/1.46/RenderDoc_1.46_64.zip
    # unzip into logs\toolchain\ -> logs\toolchain\RenderDoc_1.46_64\

Capture with `tools/run_scripted.ps1 -RenderDocCapture <prefix>` and a `renderdoc 2` line in the input
script. Windows asks once whether `ssarecompiled.exe` may use the network: that is RenderDoc's
target control port, local access is enough. With `rov`, the first start under RenderDoc builds all
pipelines very slowly (minutes); wait longer before the first input. `--d3d12_bindless=false` makes
the textures each draw reads visible to RenderDoc.

`tools/renderdoc/rd.ps1` runs the analysis scripts headless: `list` (actions and their render
targets; `group.py` folds them into passes), `srvs` (textures read per draw, to find the same draw in
two captures), `dump` (render targets after given events as PNG) and `inspect` (pipeline state and
input textures of given draws), `timings` (GPU time per action, grouped by kind and target) and
`overhead` (GPU time of render target emulation: ownership transfers, EDRAM dumps, resolves). Host render targets with the RTV path are EDRAM-shaped (for example
1040x2528): the surface at an EDRAM base starts at row 0 of the target created for that base. This
is how the RTV black characters were found (docs/STATUS.md, 2026-10-17).

## Troubleshooting

- **CMake can't find `clang`/`clang++`:** use the `just` recipes instead of calling `cmake`
  directly; they put the VS Clang tools on `PATH`. Or run `just clean-debug` and try again.
- **"Administrator rights required" on symlinks:** Developer Mode is off, or `core.symlinks` is
  not set. Git uses Developer Mode; Windows PowerShell 5.1's `New-Item -ItemType SymbolicLink`
  does not, so don't use it as a test.
- **The game refuses to start because of the XEX check:** expected until Phase 1 records the
  real hash; after that it means the dump is a different version.
- **Paths too long:** turn on Windows long paths and `git config --global core.longpaths true`.
