// Free camera (mod): the right stick orbits the camera around the player (free_camera_hook.cpp).

#pragma once

#include <cstdint>

#include <rex/ppc/context.h>

namespace rex::system {
class IInputSystem;
}

namespace ssarecomp {

// Remembers the input system the hook reads the right stick from. Call once the runtime exists.
void InstallFreeCamera(rex::system::IInputSystem* input_system);

// At the start of the game's per-frame update (where_hook.cpp): puts the game camera's own
// transform back before its camera logic runs.
void FreeCameraFrameStart(uint8_t* base);

}  // namespace ssarecomp

// The main scene's build (sub_828578E8's wrapper): turns the game camera's transform (ctx.r4).
void FreeCameraSceneBuild(PPCContext& ctx, uint8_t* base);

namespace ssarecomp {

}  // namespace ssarecomp
