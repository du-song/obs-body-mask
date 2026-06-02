#include "obs-utils.h"

#include <graphics/graphics.h>

namespace obm {

bool getBGRAFromTarget(obs_source_t *filter, gs_texrender_t *texrender,
		       gs_stagesurf_t **stagesurface, Image &out)
{
	obs_source_t *target = obs_filter_get_target(filter);
	if (!target)
		return false;
	const uint32_t width = obs_source_get_base_width(target);
	const uint32_t height = obs_source_get_base_height(target);
	if (width == 0 || height == 0)
		return false;

	gs_texrender_reset(texrender);
	if (!gs_texrender_begin(texrender, width, height))
		return false;

	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	obs_source_video_render(target);
	gs_blend_state_pop();
	gs_texrender_end(texrender);

	// (Re)create the staging surface when the size changes.
	if (*stagesurface) {
		if (gs_stagesurface_get_width(*stagesurface) != width ||
		    gs_stagesurface_get_height(*stagesurface) != height) {
			gs_stagesurface_destroy(*stagesurface);
			*stagesurface = nullptr;
		}
	}
	if (!*stagesurface)
		*stagesurface = gs_stagesurface_create(width, height, GS_BGRA);

	gs_stage_texture(*stagesurface, gs_texrender_get_texture(texrender));

	uint8_t *data = nullptr;
	uint32_t linesize = 0;
	if (!gs_stagesurface_map(*stagesurface, &data, &linesize))
		return false;

	if (out.w != (int)width || out.h != (int)height || out.c != 4)
		out = Image((int)width, (int)height, 4);
	for (uint32_t y = 0; y < height; ++y)
		std::copy(data + (size_t)y * linesize,
			  data + (size_t)y * linesize + (size_t)width * 4,
			  out.data.begin() + (size_t)y * width * 4);

	gs_stagesurface_unmap(*stagesurface);
	return true;
}

} // namespace obm
