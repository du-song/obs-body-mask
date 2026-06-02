#include "compositor.h"

#include <algorithm>

namespace obm {

// Separable 1-D min/max (erosion/dilation) over a [-radius,radius] window.
static Image morph1(const Image &src, int radius, bool dilate)
{
	Image tmp(src.w, src.h, 1), dst(src.w, src.h, 1);
	auto op = [dilate](uint8_t a, uint8_t b) { return dilate ? std::max(a, b) : std::min(a, b); };
	for (int y = 0; y < src.h; ++y)
		for (int x = 0; x < src.w; ++x) {
			uint8_t v = src.data[(size_t)y * src.w + x];
			for (int k = -radius; k <= radius; ++k)
				v = op(v, src.data[(size_t)y * src.w + std::clamp(x + k, 0, src.w - 1)]);
			tmp.data[(size_t)y * src.w + x] = v;
		}
	for (int x = 0; x < src.w; ++x)
		for (int y = 0; y < src.h; ++y) {
			uint8_t v = tmp.data[(size_t)y * src.w + x];
			for (int k = -radius; k <= radius; ++k)
				v = op(v, tmp.data[(size_t)std::clamp(y + k, 0, src.h - 1) * src.w + x]);
			dst.data[(size_t)y * src.w + x] = v;
		}
	return dst;
}

Image bodyEdge(const Image &mask, int thickness)
{
	Image edge(mask.w, mask.h, 1);
	if (mask.empty() || mask.c != 1 || thickness <= 0)
		return edge;
	const int r = std::max(1, thickness / 2);
	// Binarize so a feathered (grey) mask still yields a crisp outline.
	Image bin(mask.w, mask.h, 1);
	for (size_t i = 0; i < bin.data.size(); ++i)
		bin.data[i] = mask.data[i] >= 128 ? 255 : 0;
	const Image dil = morph1(bin, r, /*dilate=*/true);
	const Image ero = morph1(bin, r, /*dilate=*/false);
	for (size_t i = 0; i < edge.data.size(); ++i)
		edge.data[i] = (dil.data[i] && !ero.data[i]) ? 255 : 0;
	return edge;
}

void drawEdge(Image &canvas, const Image &edge, Color color)
{
	if (edge.empty() || canvas.c < 3 || edge.w != canvas.w || edge.h != canvas.h)
		return;
	for (int y = 0; y < canvas.h; ++y)
		for (int x = 0; x < canvas.w; ++x) {
			if (!edge.pixel(x, y)[0])
				continue;
			uint8_t *c = canvas.pixel(x, y);
			c[0] = color.r;
			c[1] = color.g;
			c[2] = color.b;
		}
}

void drawRect(Image &canvas, int x0, int y0, int x1, int y1, Color color, int thickness)
{
	if (canvas.c < 3 || thickness <= 0 || x1 <= x0 || y1 <= y0)
		return;
	auto fill = [&](int ax, int ay, int bx, int by) {
		for (int y = std::max(0, ay); y < std::min(canvas.h, by); ++y)
			for (int x = std::max(0, ax); x < std::min(canvas.w, bx); ++x) {
				uint8_t *c = canvas.pixel(x, y);
				c[0] = color.r;
				c[1] = color.g;
				c[2] = color.b;
			}
	};
	const int t = thickness;
	fill(x0, y0, x1, y0 + t);     // top
	fill(x0, y1 - t, x1, y1);     // bottom
	fill(x0, y0, x0 + t, y1);     // left
	fill(x1 - t, y0, x1, y1);     // right
}

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
