#include <obs-module.h>

#include "body-mask-filter.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-body-mask", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Human body segmentation: white/black mask, background replacement, "
	       "and per-head labels.";
}

bool obs_module_load(void)
{
	obs_register_source(&body_mask_filter_info);
	blog(LOG_INFO, "[obs-body-mask] loaded");
	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[obs-body-mask] unloaded");
}
