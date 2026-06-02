#include "compositor.h"

#include <algorithm>

namespace obm {

Image maskToRGB(const Image &mask)
{
	Image out(mask.w, mask.h, 3);
	for (size_t i = 0; i < mask.data.size(); ++i) {
		uint8_t v = mask.data[i];
		out.data[i * 3 + 0] = v;
		out.data[i * 3 + 1] = v;
		out.data[i * 3 + 2] = v;
	}
	return out;
}

Image backgroundReplace(const Image &src, const Image &mask, const Image &bg, Color solid,
			bool useImage, float originalBlend)
{
	Image out(src.w, src.h, 3);
	Image bgScaled;
	if (useImage && !bg.empty())
		bgScaled = resizeBilinear(bg, src.w, src.h);
	const float ob = std::clamp(originalBlend, 0.f, 1.f);

	for (int y = 0; y < src.h; ++y) {
		for (int x = 0; x < src.w; ++x) {
			const uint8_t *s = src.pixel(x, y);
			float m = mask.pixel(x, y)[0] / 255.f; // foreground weight
			uint8_t br, bgc, bb;
			if (useImage && !bgScaled.empty()) {
				const uint8_t *bp = bgScaled.pixel(x, y);
				br = bp[0];
				bgc = bp[1];
				bb = bp[2];
			} else {
				br = solid.r;
				bgc = solid.g;
				bb = solid.b;
			}
			// replaced = bg*(1-m) + src*m, then blend original back in
			auto mix = [&](float fg, float bgv) {
				float rep = bgv * (1 - m) + fg * m;
				return (uint8_t)std::clamp(rep * (1 - ob) + fg * ob, 0.f, 255.f);
			};
			uint8_t *o = out.pixel(x, y);
			o[0] = mix(s[0], br);
			o[1] = mix(s[1], bgc);
			o[2] = mix(s[2], bb);
		}
	}
	return out;
}

void drawLabel(Image &canvas, const Image &label, int cx, int bottomY)
{
	if (label.empty() || canvas.c < 3)
		return;
	int ox = cx - label.w / 2;
	int oy = bottomY - label.h;
	for (int y = 0; y < label.h; ++y) {
		int dy = oy + y;
		if (dy < 0 || dy >= canvas.h)
			continue;
		for (int x = 0; x < label.w; ++x) {
			int dx = ox + x;
			if (dx < 0 || dx >= canvas.w)
				continue;
			const uint8_t *lp = label.pixel(x, y);
			float a = lp[3] / 255.f;
			if (a <= 0.f)
				continue;
			uint8_t *c = canvas.pixel(dx, dy);
			c[0] = (uint8_t)(lp[0] * a + c[0] * (1 - a));
			c[1] = (uint8_t)(lp[1] * a + c[1] * (1 - a));
			c[2] = (uint8_t)(lp[2] * a + c[2] * (1 - a));
		}
	}
}

} // namespace obm
