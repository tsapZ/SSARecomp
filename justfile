# Spyro's Adventure Recompiled: dev recipes. Run `just` with no arguments to list them. They are the same for
# all six games and live in skylanders-portal (just/game.just); this file only names the game.
#
# Needs CMake, Ninja, `just`, Git and clang (Windows: Visual Studio 2022 with "C++ Clang tools for
# Windows", plus Git for Windows); details in docs/development.md.

game := "ssarecomp"
repo := "SSARecomp"

# Optional, so `just submodules` works on a fresh clone before the submodules are there.
import? 'thirdparty/skylanders-portal/just/game.just'

# Init/update every git submodule recursively: after a fresh clone, or after pulling a branch
# that added a new one.
submodules:
    git submodule update --init --recursive
