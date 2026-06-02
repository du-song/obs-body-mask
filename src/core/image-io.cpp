#include "image-io.h"

#include <cstdio>

#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"

namespace obm {

Image loadImageRGB(const std::string &path)
{
	int w, h, n;
	unsigned char *data = stbi_load(path.c_str(), &w, &h, &n, 3);
	if (!data) {
		fprintf(stderr, "[obs-body-mask] failed to load image '%s': %s\n", path.c_str(),
			stbi_failure_reason());
		return Image();
	}
	Image img(w, h, 3);
	std::copy(data, data + (size_t)w * h * 3, img.data.begin());
	stbi_image_free(data);
	return img;
}

bool savePNG(const std::string &path, const Image &img)
{
	if (img.empty())
		return false;
	return stbi_write_png(path.c_str(), img.w, img.h, img.c, img.data.data(), img.w * img.c) != 0;
}

} // namespace obm
