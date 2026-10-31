# ssarecomp - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(ssarecomp LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build the SDK from the pinned submodule when present; otherwise generated/rexglue.cmake falls back
# to an installed SDK found through find_package. Pass -DREXSDK_DIR=<path> to use another checkout.
if(NOT REXSDK_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk/CMakeLists.txt")
    set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk" CACHE PATH
        "Path to the rexglue-sdk source tree" FORCE)
endif()

# Vulkan next to D3D12 (the SDK leaves it off on Windows): gpu_backend = "vulkan" picks it; D3D12
# stays the default (as in SwapForceRecomp, where both ran about equally fast).
set(REXGLUE_USE_VULKAN ON CACHE BOOL "Enable Vulkan graphics backend")

# Builds keep no path of the machine they were made on: the source folder is mapped away in
# __FILE__ (the logs' source locations) and the debug info, for every target here and in the SDK and
# the library. The setups on GitHub Releases are passed on (checked: no user folder left in the exe
# and its DLLs).
add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-ffile-prefix-map=${CMAKE_SOURCE_DIR}/=>")

# Profile-guided optimization, off unless asked for (thirdparty/skylanders-portal/cmake/pgo.cmake:
# SKYLANDERS_PGO=generate for training runs, SKYLANDERS_PGO_PROFILE=<profile> for the optimized
# build); before the SDK, so its targets get it too.
include(thirdparty/skylanders-portal/cmake/pgo.cmake)

include(generated/rexglue.cmake)

# Portal of Power support (thirdparty/skylanders-portal, shared with other Skylanders recomps).
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/skylanders-portal/CMakeLists.txt")
    message(FATAL_ERROR "thirdparty/skylanders-portal is missing. Run: git submodule update --init --recursive")
endif()
add_subdirectory(thirdparty/skylanders-portal)

# Recompiled code is generated from the user's own default.xex and is not in the repository.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/sources.cmake")
    message(WARNING
        "No recompiled code yet (generated/default/sources.cmake is missing).\n"
        "  1. Put your extracted game disc in rom/ (rom/default.xex must exist).\n"
        "  2. Run:  cmake --build --preset <preset> --target ssarecomp_codegen\n"
        "  3. Re-run:  cmake --preset <preset>\n"
        "  4. Build:  cmake --build --preset <preset>\n"
        "See README.md.")
endif()

# Sources
set(SSARECOMP_SOURCES
    src/main.cpp
    src/game/kernel_stubs.cpp
    src/game/game_fonts.cpp
    src/game/game_text.cpp
    src/game/hats.cpp
    src/game/upgrades.cpp
    src/game/portal_prompt_watch.cpp
    src/game/ui_images.cpp
    src/localization.cpp
    src/settings_pages.cpp
    src/hooks/intro_skip_hook.cpp
    src/hooks/lua_error_hook.cpp
    src/hooks/object_list_guard_hook.cpp
    src/hooks/post_effects_hook.cpp
    src/hooks/scene_size_hook.cpp
    src/hooks/ultrawide_hook.cpp
    src/hooks/where_hook.cpp
)

if(WIN32)
    add_executable(ssarecompiled WIN32 ${SSARECOMP_SOURCES})
else()
    add_executable(ssarecompiled ${SSARECOMP_SOURCES})
endif()

rexglue_setup_target(ssarecompiled GPU_PLUGINS xenos)

# The developer kit (a separate private repository, tsapZ/SkylandersDevKit): level loading, Lua
# and level sweeps for test runs. Built only into a developer build folder (a local
# CMakeUserPresets.json preset), never into a release. Off unless SKYLANDERS_DEVKIT_DIR points at
# a checkout of it.
set(SKYLANDERS_DEVKIT_DIR "" CACHE PATH "A checkout of SkylandersDevKit to build in (empty: none)")
if(SKYLANDERS_DEVKIT_DIR)
    include("${SKYLANDERS_DEVKIT_DIR}/games/ssa/devkit.cmake")
    skylanders_devkit_add(ssarecompiled)
endif()

if(REXSDK_DIR)
    # An installed SDK ships imgui's headers next to its own; an in-tree build must point at them.
    target_include_directories(ssarecompiled PRIVATE "${REXSDK_DIR}/thirdparty/imgui")
endif()
# stb_image's zlib decoder for the game's UI picture archive (src/game/ui_images.cpp).
target_include_directories(ssarecompiled PRIVATE "${REXSDK_DIR}/thirdparty/stb")

# Seed a fresh build dir with the settings-file template. Never overwrites an existing one: the
# in-game F4 Settings overlay saves the user's own changes to this exact file.
if(NOT EXISTS "${CMAKE_BINARY_DIR}/ssarecomp.toml")
    file(COPY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/ssarecomp.toml.example" "${CMAKE_BINARY_DIR}/ssarecomp.toml")
endif()

target_link_libraries(ssarecompiled PRIVATE skylanders_portal_rex skylanders_alchemy)
# The exe's icon: the game's logo from the owner's disc (rom/nxeart), made at build time (#37).
skylanders_game_icon(ssarecompiled "${CMAKE_CURRENT_SOURCE_DIR}/rom")

# --- Pinned game executable -------------------------------------------------
# The disc check (skylanders-portal src/xex, cmake/game_xex.cmake): the supported discs' SHA-256
# from docs/game/default.xex.sha256 in the game and in ssarecomp_xexcheck, the installer's check,
# with their tests.
skylanders_game_xex(TARGET ssarecompiled PREFIX ssarecomp TITLE "Skylanders: Spyro's Adventure"
                    HASH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/docs/game/default.xex.sha256")
