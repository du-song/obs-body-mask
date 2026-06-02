#include "text-raster.h"

#include <cstdio>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"

namespace obm {

TextRaster::TextRaster(const std::string &fontPath)
{
	FILE *f = fopen(fontPath.c_str(), "rb");
	if (!f) {
		fprintf(stderr, "[obs-body-mask] could not open font '%s'\n", fontPath.c_str());
		return;
	}
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n > 0) {
		font_.resize((size_t)n);
		if (fread(font_.data(), 1, (size_t)n, f) != (size_t)n)
			font_.clear();
	}
	fclose(f);
}

Image TextRaster::render(const std::string &text, float pxHeight, Color color) const
{
	if (font_.empty() || text.empty())
		return Image();

	stbtt_fontinfo info;
	if (!stbtt_InitFont(&info, font_.data(), stbtt_GetFontOffsetForIndex(font_.data(), 0)))
		return Image();

	const float scale = stbtt_ScaleForPixelHeight(&info, pxHeight);
	int ascent, descent, lineGap;
	stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
	const int baseline = (int)(ascent * scale);
	const int height = (int)((ascent - descent) * scale) + 2;

	// First pass: measure total advance width.
	float xpos = 0.f;
	for (size_t i = 0; i < text.size(); ++i) {
		int adv, lsb;
		stbtt_GetCodepointHMetrics(&info, text[i], &adv, &lsb);
		xpos += adv * scale;
		if (i + 1 < text.size())
			xpos += scale * stbtt_GetCodepointKernAdvance(&info, text[i], text[i + 1]);
	}
	const int width = (int)xpos + 2;
	if (width <= 0 || height <= 0)
		return Image();

	// Coverage buffer (single channel) then expand to RGBA with the color.
	std::vector<unsigned char> cov((size_t)width * height, 0);
	float x = 1.f;
	for (size_t i = 0; i < text.size(); ++i) {
		int adv, lsb;
		stbtt_GetCodepointHMetrics(&info, text[i], &adv, &lsb);
		int x0, y0, x1, y1;
		stbtt_GetCodepointBitmapBox(&info, text[i], scale, scale, &x0, &y0, &x1, &y1);
		int gw = x1 - x0, gh = y1 - y0;
		if (gw > 0 && gh > 0) {
			std::vector<unsigned char> glyph((size_t)gw * gh, 0);
			stbtt_MakeCodepointBitmap(&info, glyph.data(), gw, gh, gw, scale, scale,
						  text[i]);
			int ox = (int)(x + x0);
			int oy = baseline + y0;
			for (int gy = 0; gy < gh; ++gy) {
				int dy = oy + gy;
				if (dy < 0 || dy >= height)
					continue;
				for (int gx = 0; gx < gw; ++gx) {
					int dx = ox + gx;
					if (dx < 0 || dx >= width)
						continue;
					unsigned char v = glyph[(size_t)gy * gw + gx];
					unsigned char &dst = cov[(size_t)dy * width + dx];
					dst = (unsigned char)std::max<int>(dst, v);
				}
			}
		}
		x += adv * scale;
		if (i + 1 < text.size())
			x += scale * stbtt_GetCodepointKernAdvance(&info, text[i], text[i + 1]);
	}

	Image out(width, height, 4);
	for (size_t i = 0; i < cov.size(); ++i) {
		uint8_t *px = &out.data[i * 4];
		px[0] = color.r;
		px[1] = color.g;
		px[2] = color.b;
		px[3] = cov[i];
	}
	return out;
}

} // namespace obm
