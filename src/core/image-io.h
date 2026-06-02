// Image file load/save via stb (CLI only; the OBS filter gets pixels from the
// graphics subsystem, not from disk).
#pragma once

#include <string>

#include "image.h"

namespace obm {

// Loads an image as 3-channel RGB. Returns an empty Image on failure.
Image loadImageRGB(const std::string &path);

// Writes a PNG. `img` must be 1, 3 or 4 channels. Returns false on failure.
bool savePNG(const std::string &path, const Image &img);

} // namespace obm
