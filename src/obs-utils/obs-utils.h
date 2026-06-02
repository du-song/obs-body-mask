// Helpers for moving pixels between OBS's graphics subsystem and the CPU-side
// `obm::Image` the inference core consumes. Adapted from the proven
// obs-backgroundremoval readback approach (GPLv2).
#pragma once

#include <obs-module.h>

#include "../core/image.h"

namespace obm {

// Renders the filter's target source into `texrender`, stages it to
// `*stagesurface` (created/resized as needed), and copies the BGRA pixels into
// `out`. Must be called inside a graphics context (e.g. video_render, or
// wrapped in obs_enter_graphics/obs_leave_graphics). Returns false if the
// target has no size or the surface could not be mapped.
bool getBGRAFromTarget(obs_source_t *filter, gs_texrender_t *texrender,
		       gs_stagesurf_t **stagesurface, Image &out);

} // namespace obm
