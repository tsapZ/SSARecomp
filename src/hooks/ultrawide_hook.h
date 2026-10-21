#pragma once

// Ultrawide field of view (src/hooks/ultrawide_hook.cpp).

namespace ssarecomp {

// True when ultrawide_fov is on and the resolution is wider than 16:9: the camera then shows more
// to the sides and the picture has to fill the window (no letterbox).
bool UltrawideActive();

}  // namespace ssarecomp
