// OBS filter registration. The implementation lives in body-mask-filter.cpp
// (C++); plugin-main.c registers this struct.
#pragma once

#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

extern struct obs_source_info body_mask_filter_info;

#ifdef __cplusplus
}
#endif
