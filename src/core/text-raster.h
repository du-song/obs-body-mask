// Rasterizes label strings to RGBA images via stb_truetype. Shared by the CLI
// (bakes labels into the output PNG) and the OBS filter (uploads each label as
// a GPU texture drawn over a head).
#pragma once

#include <string>
#include <vector>

#include "image.h"

namespace obm {

struct Color {
	uint8_t r = 255, g = 255, b = 255;
};

class TextRaster {
public:
	// Loads a TTF from disk. ok() is false if the font could not be read.
	explicit TextRaster(const std::string &fontPath);
	bool ok() const { return !font_.empty(); }

	// Renders `text` at the given pixel height into a tight RGBA image.
	// Glyph pixels use `color`; the alpha channel carries coverage so the
	// result composites cleanly over any background.
	Image render(const std::string &text, float pxHeight, Color color) const;

private:
	std::vector<unsigned char> font_;
};

} // namespace obm
