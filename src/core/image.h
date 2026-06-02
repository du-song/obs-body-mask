// Minimal interleaved 8-bit image container + the handful of pixel operations
// the pipeline needs (bilinear resize, box blur, channel order). Replaces the
// OpenCV dependency so the core stays portable and dependency-light.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace obm {

struct Image {
	int w = 0;
	int h = 0;
	int c = 0; // channels (1, 3 or 4)
	std::vector<uint8_t> data; // size = w*h*c, row-major interleaved

	Image() = default;
	Image(int w_, int h_, int c_) : w(w_), h(h_), c(c_), data((size_t)w_ * h_ * c_, 0) {}

	bool empty() const { return data.empty(); }
	uint8_t *pixel(int x, int y) { return &data[((size_t)y * w + x) * c]; }
	const uint8_t *pixel(int x, int y) const { return &data[((size_t)y * w + x) * c]; }
};

// Bilinear resize. Works for any channel count.
inline Image resizeBilinear(const Image &src, int dstW, int dstH)
{
	Image dst(dstW, dstH, src.c);
	if (src.empty() || dstW <= 0 || dstH <= 0)
		return dst;
	const float sx = (float)src.w / dstW;
	const float sy = (float)src.h / dstH;
	for (int y = 0; y < dstH; ++y) {
		float fy = (y + 0.5f) * sy - 0.5f;
		int y0 = (int)std::floor(fy);
		float wy = fy - y0;
		int y0c = std::clamp(y0, 0, src.h - 1);
		int y1c = std::clamp(y0 + 1, 0, src.h - 1);
		for (int x = 0; x < dstW; ++x) {
			float fx = (x + 0.5f) * sx - 0.5f;
			int x0 = (int)std::floor(fx);
			float wx = fx - x0;
			int x0c = std::clamp(x0, 0, src.w - 1);
			int x1c = std::clamp(x0 + 1, 0, src.w - 1);
			const uint8_t *p00 = src.pixel(x0c, y0c);
			const uint8_t *p10 = src.pixel(x1c, y0c);
			const uint8_t *p01 = src.pixel(x0c, y1c);
			const uint8_t *p11 = src.pixel(x1c, y1c);
			uint8_t *o = dst.pixel(x, y);
			for (int ch = 0; ch < src.c; ++ch) {
				float top = p00[ch] * (1 - wx) + p10[ch] * wx;
				float bot = p01[ch] * (1 - wx) + p11[ch] * wx;
				o[ch] = (uint8_t)std::clamp(top * (1 - wy) + bot * wy, 0.f, 255.f);
			}
		}
	}
	return dst;
}

// Separable box blur on a single-channel image; `radius` in pixels per side.
inline Image boxBlur1(const Image &src, int radius)
{
	if (radius <= 0 || src.c != 1)
		return src;
	Image tmp(src.w, src.h, 1), dst(src.w, src.h, 1);
	const int win = radius * 2 + 1;
	// horizontal
	for (int y = 0; y < src.h; ++y) {
		int acc = 0;
		for (int x = -radius; x <= radius; ++x)
			acc += src.data[(size_t)y * src.w + std::clamp(x, 0, src.w - 1)];
		for (int x = 0; x < src.w; ++x) {
			tmp.data[(size_t)y * src.w + x] = (uint8_t)(acc / win);
			int add = std::clamp(x + radius + 1, 0, src.w - 1);
			int sub = std::clamp(x - radius, 0, src.w - 1);
			acc += src.data[(size_t)y * src.w + add] - src.data[(size_t)y * src.w + sub];
		}
	}
	// vertical
	for (int x = 0; x < src.w; ++x) {
		int acc = 0;
		for (int y = -radius; y <= radius; ++y)
			acc += tmp.data[(size_t)std::clamp(y, 0, src.h - 1) * src.w + x];
		for (int y = 0; y < src.h; ++y) {
			dst.data[(size_t)y * src.w + x] = (uint8_t)(acc / win);
			int add = std::clamp(y + radius + 1, 0, src.h - 1);
			int sub = std::clamp(y - radius, 0, src.h - 1);
			acc += tmp.data[(size_t)add * src.w + x] - tmp.data[(size_t)sub * src.w + x];
		}
	}
	return dst;
}

} // namespace obm
