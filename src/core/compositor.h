// CPU reference compositing used by the CLI: background replacement (image or
// solid color with an original-blend factor) and alpha-blended label drawing.
// The OBS filter performs the equivalent compositing on the GPU but shares the
// segmentation/pose/label logic; this is the ground-truth implementation for
// offline validation.
#pragma once

#include "image.h"
#include "text-raster.h"

namespace obm {

// Replaces background (mask==0) with `bg` (if provided, resized to src) or
// `solid` color, keeping foreground (mask==255). `originalBlend` in [0,1]
// blends the untouched source back over the result (0 = full replace).
// Returns a 3-channel RGB image. `src` may be 3 or 4 channels.
Image backgroundReplace(const Image &src, const Image &mask, const Image &bg, Color solid,
			bool useImage, float originalBlend);

// Alpha-composites an RGBA `label` onto `canvas` (3ch), centered horizontally
// at `cx` with the label's bottom at `bottomY` (i.e. sitting above a head).
void drawLabel(Image &canvas, const Image &label, int cx, int bottomY);

// Renders the mask itself as a white/black RGB image (for --mode mask output).
Image maskToRGB(const Image &mask);

} // namespace obm
