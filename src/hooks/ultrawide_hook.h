#pragma once

#include <cstdint>

// Ultrawide field of view (src/hooks/ultrawide_hook.cpp).

namespace ssarecomp {

// True when ultrawide_fov is on and the resolution is wider than 16:9: the camera then shows more
// to the sides and the picture has to fill the window (no letterbox).
bool UltrawideActive();

// A movie starts or ends: with ultrawide active the presenter shows it with black bars at the
// sides instead of stretched (BinkOpen's and BinkClose's hooks).
void MovieOpened(uint32_t width, uint32_t height);
void MovieClosed();

}  // namespace ssarecomp
