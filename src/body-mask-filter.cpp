// OBS video filter implementing the three modes from the shared core:
//   - Mask:               output a white(human)/black(background) image
//   - Background Replace:  swap non-human pixels for an image/color + blend
//   - Head overlay:        draw an auto-numbered label on each detected head
//
// Frame flow mirrors obs-backgroundremoval (GPLv2): render the target into a
// texrender, stage it to the CPU, run inference (rate-limited via "run every N
// frames"), upload the resulting mask to a GS_R8 texture, then composite on the
// GPU via body-mask.effect and draw label sprites on top.
#include "body-mask-filter.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/matrix4.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "obs-utils/obs-utils.h"
#include "core/body-segmenter.h"
#include "core/pose-detector.h"
#include "core/pose-tracker.h"
#include "core/text-raster.h"

#define S_MODE "mode"
#define S_THRESHOLD "threshold"
#define S_INPUT_SIZE "input_size"
#define S_INVERT "invert"
#define S_FEATHER "feather"
#define S_SEG_MODEL "seg_model"
#define S_BG_TYPE "bg_type"
#define S_BG_IMAGE "bg_image"
#define S_BG_COLOR "bg_color"
#define S_ORIG_BLEND "original_blend"
#define S_OVERLAY "overlay_enabled"
#define S_POSE_MODEL "pose_model"
#define S_POSE_CONF "pose_conf"
#define S_FONT_SIZE "font_size"
#define S_TEXT_COLOR "text_color"
#define S_VOFFSET "v_offset"
#define S_BACKEND "backend"
#define S_RUN_EVERY "run_every"

using namespace obm;

namespace {

struct LabelSprite {
	gs_texture_t *tex = nullptr;
	int w = 0, h = 0;
	int x = 0, y = 0; // top-left in source pixels
};

struct BodyMaskFilter {
	obs_source_t *source = nullptr;

	gs_texrender_t *texrender = nullptr;
	gs_stagesurf_t *stagesurface = nullptr;
	gs_effect_t *effect = nullptr;
	gs_texture_t *maskTexture = nullptr;
	gs_texture_t *solidTexture = nullptr;

	gs_image_file_t bgImage{};
	bool bgImageLoaded = false;

	Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "obs-body-mask"};
	std::unique_ptr<BodySegmenter> seg;
	std::unique_ptr<PoseDetector> pose;
	PoseTracker tracker; // persistent "Person N" IDs across frames
	std::unique_ptr<TextRaster> textRaster;
	std::mutex modelMutex;

	// cached settings
	std::string mode = "mask";
	float threshold = 0.5f;
	int inputSize = 256;
	bool invert = false;
	int feather = 0;
	std::string segModel, poseModel, fontPath;
	std::string bgType = "color";
	std::string bgImagePath;
	uint32_t bgColor = 0xff00ff00; // ABGR-ish; see usage
	float origBlend = 0.f;
	bool overlay = false;
	float poseConf = 0.25f;
	int fontSize = 28;
	uint32_t textColor = 0xff00e6ff;
	int vOffset = 6;
	Backend backend = Backend::CoreML;
	int runEvery = 1;

	int frameCounter = 0;
	Image mask;                       // latest binary mask (w*h, 1ch)
	std::vector<LabelSprite> labels;  // latest head labels (GPU textures)
	bool dirtySegModel = true, dirtyPoseModel = true, dirtyFont = true, dirtyBg = true;
};

const char *getName(void *)
{
	return obs_module_text("BodyMask.Name");
}

void rebuildModels(BodyMaskFilter *f)
{
	std::lock_guard<std::mutex> lock(f->modelMutex);
	if (f->dirtySegModel) {
		char *p = obs_module_file(("models/" + f->segModel).c_str());
		if (p) {
			f->seg = std::make_unique<BodySegmenter>(f->env, p, f->backend);
			bfree(p);
		}
		f->dirtySegModel = false;
	}
	if (f->overlay && f->dirtyPoseModel) {
		char *p = obs_module_file(("models/" + f->poseModel).c_str());
		if (p) {
			f->pose = std::make_unique<PoseDetector>(f->env, p, f->backend);
			bfree(p);
		}
		f->dirtyPoseModel = false;
	}
	if (f->overlay && f->dirtyFont) {
		char *p = obs_module_file(f->fontPath.c_str());
		if (p) {
			f->textRaster = std::make_unique<TextRaster>(p);
			bfree(p);
		}
		f->dirtyFont = false;
	}
}

void loadBackgroundImage(BodyMaskFilter *f)
{
	if (!f->dirtyBg)
		return;
	obs_enter_graphics();
	if (f->bgImageLoaded) {
		gs_image_file_free(&f->bgImage);
		f->bgImageLoaded = false;
	}
	if (f->bgType == "image" && !f->bgImagePath.empty()) {
		gs_image_file_init(&f->bgImage, f->bgImagePath.c_str());
		gs_image_file_init_texture(&f->bgImage);
		f->bgImageLoaded = f->bgImage.texture != nullptr;
	}
	obs_leave_graphics();
	f->dirtyBg = false;
}

void updateSettings(void *data, obs_data_t *s)
{
	auto *f = static_cast<BodyMaskFilter *>(data);

	std::string newSeg = obs_data_get_string(s, S_SEG_MODEL);
	std::string newPose = obs_data_get_string(s, S_POSE_MODEL);
	Backend newBackend = std::string(obs_data_get_string(s, S_BACKEND)) == "cpu"
				     ? Backend::CPU
				     : Backend::CoreML;
	std::string newBgType = obs_data_get_string(s, S_BG_TYPE);
	std::string newBgImage = obs_data_get_string(s, S_BG_IMAGE);
	bool newOverlay = obs_data_get_bool(s, S_OVERLAY);

	if (newSeg != f->segModel || newBackend != f->backend)
		f->dirtySegModel = true;
	if (newPose != f->poseModel || newBackend != f->backend || (newOverlay && !f->pose))
		f->dirtyPoseModel = true;
	if (newBgType != f->bgType || newBgImage != f->bgImagePath)
		f->dirtyBg = true;

	f->mode = obs_data_get_string(s, S_MODE);
	f->threshold = (float)obs_data_get_double(s, S_THRESHOLD);
	f->inputSize = (int)obs_data_get_int(s, S_INPUT_SIZE);
	f->invert = obs_data_get_bool(s, S_INVERT);
	f->feather = (int)obs_data_get_int(s, S_FEATHER);
	f->segModel = newSeg;
	f->bgType = newBgType;
	f->bgImagePath = newBgImage;
	f->bgColor = (uint32_t)obs_data_get_int(s, S_BG_COLOR);
	f->origBlend = (float)obs_data_get_double(s, S_ORIG_BLEND) / 100.f;
	f->overlay = newOverlay;
	f->poseModel = newPose;
	f->poseConf = (float)obs_data_get_double(s, S_POSE_CONF);
	f->fontSize = (int)obs_data_get_int(s, S_FONT_SIZE);
	f->textColor = (uint32_t)obs_data_get_int(s, S_TEXT_COLOR);
	f->vOffset = (int)obs_data_get_int(s, S_VOFFSET);
	f->backend = newBackend;
	f->runEvery = (int)obs_data_get_int(s, S_RUN_EVERY);

	rebuildModels(f);
	loadBackgroundImage(f);
}

void *createFilter(obs_data_t *settings, obs_source_t *source)
{
	auto *f = new BodyMaskFilter();
	f->source = source;

	obs_enter_graphics();
	f->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	char *effectPath = obs_module_file("effects/body-mask.effect");
	if (effectPath) {
		f->effect = gs_effect_create_from_file(effectPath, nullptr);
		bfree(effectPath);
	}
	obs_leave_graphics();

	updateSettings(f, settings);
	return f;
}

void destroyFilter(void *data)
{
	auto *f = static_cast<BodyMaskFilter *>(data);
	obs_enter_graphics();
	if (f->texrender)
		gs_texrender_destroy(f->texrender);
	if (f->stagesurface)
		gs_stagesurface_destroy(f->stagesurface);
	if (f->maskTexture)
		gs_texture_destroy(f->maskTexture);
	if (f->solidTexture)
		gs_texture_destroy(f->solidTexture);
	if (f->effect)
		gs_effect_destroy(f->effect);
	if (f->bgImageLoaded)
		gs_image_file_free(&f->bgImage);
	for (auto &l : f->labels)
		if (l.tex)
			gs_texture_destroy(l.tex);
	obs_leave_graphics();
	delete f;
}

void getDefaults(obs_data_t *s)
{
	obs_data_set_default_string(s, S_MODE, "mask");
	obs_data_set_default_double(s, S_THRESHOLD, 0.5);
	obs_data_set_default_int(s, S_INPUT_SIZE, 256);
	obs_data_set_default_bool(s, S_INVERT, false);
	obs_data_set_default_int(s, S_FEATHER, 0);
	obs_data_set_default_string(s, S_SEG_MODEL, "selfie_segmentation.onnx");
	obs_data_set_default_string(s, S_BG_TYPE, "color");
	obs_data_set_default_int(s, S_BG_COLOR, 0xff00ff00);
	obs_data_set_default_double(s, S_ORIG_BLEND, 0.0);
	obs_data_set_default_bool(s, S_OVERLAY, false);
	obs_data_set_default_string(s, S_POSE_MODEL, "yolo26n-pose.onnx");
	obs_data_set_default_double(s, S_POSE_CONF, 0.25);
	obs_data_set_default_int(s, S_FONT_SIZE, 28);
	obs_data_set_default_int(s, S_TEXT_COLOR, 0xff00e6ff);
	obs_data_set_default_int(s, S_VOFFSET, 6);
	obs_data_set_default_string(s, S_BACKEND, "coreml");
	obs_data_set_default_int(s, S_RUN_EVERY, 1);
}

bool modeChanged(obs_properties_t *props, obs_property_t *, obs_data_t *s)
{
	std::string mode = obs_data_get_string(s, S_MODE);
	bool bg = mode == "background";
	for (const char *k : {S_BG_TYPE, S_BG_IMAGE, S_BG_COLOR, S_ORIG_BLEND})
		obs_property_set_visible(obs_properties_get(props, k), bg);
	return true;
}

bool overlayChanged(obs_properties_t *props, obs_property_t *, obs_data_t *s)
{
	bool on = obs_data_get_bool(s, S_OVERLAY);
	for (const char *k : {S_POSE_MODEL, S_POSE_CONF, S_FONT_SIZE, S_TEXT_COLOR, S_VOFFSET})
		obs_property_set_visible(obs_properties_get(props, k), on);
	return true;
}

obs_properties_t *getProperties(void *)
{
	obs_properties_t *p = obs_properties_create();

	obs_property_t *mode = obs_properties_add_list(p, S_MODE, obs_module_text("BodyMask.Mode"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(mode, obs_module_text("BodyMask.Mode.Mask"), "mask");
	obs_property_list_add_string(mode, obs_module_text("BodyMask.Mode.Background"), "background");
	obs_property_set_modified_callback(mode, modeChanged);

	obs_properties_add_float_slider(p, S_THRESHOLD, obs_module_text("BodyMask.Threshold"), 0.0,
					1.0, 0.01);
	obs_property_t *isz = obs_properties_add_list(p, S_INPUT_SIZE,
						      obs_module_text("BodyMask.InputSize"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(isz, "256", 256);
	obs_property_list_add_int(isz, "384", 384);
	obs_property_list_add_int(isz, "512", 512);
	obs_properties_add_bool(p, S_INVERT, obs_module_text("BodyMask.Invert"));
	obs_properties_add_int_slider(p, S_FEATHER, obs_module_text("BodyMask.Feather"), 0, 32, 1);

	obs_property_t *segm = obs_properties_add_list(p, S_SEG_MODEL,
						       obs_module_text("BodyMask.SegModel"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(segm, "Selfie Segmentation", "selfie_segmentation.onnx");
	obs_property_list_add_string(segm, "PP-HumanSeg", "pphumanseg.onnx");

	obs_property_t *bgt = obs_properties_add_list(p, S_BG_TYPE,
						      obs_module_text("BodyMask.BgType"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(bgt, obs_module_text("BodyMask.BgType.Image"), "image");
	obs_property_list_add_string(bgt, obs_module_text("BodyMask.BgType.Color"), "color");
	obs_properties_add_path(p, S_BG_IMAGE, obs_module_text("BodyMask.BgImage"), OBS_PATH_FILE,
				"Images (*.png *.jpg *.jpeg *.bmp);;All (*.*)", nullptr);
	obs_properties_add_color(p, S_BG_COLOR, obs_module_text("BodyMask.BgColor"));
	obs_properties_add_float_slider(p, S_ORIG_BLEND, obs_module_text("BodyMask.OrigBlend"), 0.0,
					100.0, 1.0);

	obs_property_t *ov = obs_properties_add_bool(p, S_OVERLAY,
						     obs_module_text("BodyMask.Overlay"));
	obs_property_set_modified_callback(ov, overlayChanged);
	obs_property_t *posem = obs_properties_add_list(p, S_POSE_MODEL,
							obs_module_text("BodyMask.PoseModel"),
							OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(posem, "YOLO26n-pose (NMS-free)", "yolo26n-pose.onnx");
	obs_property_list_add_string(posem, "YOLO11n-pose", "yolo11n-pose.onnx");
	obs_properties_add_float_slider(p, S_POSE_CONF, obs_module_text("BodyMask.PoseConf"), 0.05,
					0.9, 0.01);
	obs_properties_add_int_slider(p, S_FONT_SIZE, obs_module_text("BodyMask.FontSize"), 10, 96,
				      1);
	obs_properties_add_color(p, S_TEXT_COLOR, obs_module_text("BodyMask.TextColor"));
	obs_properties_add_int_slider(p, S_VOFFSET, obs_module_text("BodyMask.VOffset"), 0, 200, 1);

	obs_property_t *be = obs_properties_add_list(p, S_BACKEND,
						     obs_module_text("BodyMask.Backend"),
						     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(be, "CoreML (GPU/ANE)", "coreml");
	obs_property_list_add_string(be, "CPU", "cpu");
	obs_properties_add_int_slider(p, S_RUN_EVERY, obs_module_text("BodyMask.RunEvery"), 1, 4, 1);
	return p;
}

// Build the per-head label textures from the latest detections, using the
// tracker's persistent IDs so a number stays glued to the same person.
void buildLabelSprites(BodyMaskFilter *f, const std::vector<Detection> &dets,
		       const std::vector<int> &ids)
{
	for (auto &l : f->labels)
		if (l.tex)
			gs_texture_destroy(l.tex);
	f->labels.clear();
	if (!f->textRaster || !f->textRaster->ok())
		return;
	Color col{(uint8_t)(f->textColor & 0xff), (uint8_t)((f->textColor >> 8) & 0xff),
		  (uint8_t)((f->textColor >> 16) & 0xff)};
	for (size_t i = 0; i < dets.size(); ++i) {
		const auto &d = dets[i];
		Image rgba = f->textRaster->render("Person " + std::to_string(ids[i]),
						   (float)f->fontSize, col);
		if (rgba.empty())
			continue;
		LabelSprite sp;
		sp.w = rgba.w;
		sp.h = rgba.h;
		sp.x = (int)d.headX - rgba.w / 2;
		sp.y = (int)d.headY - rgba.h - f->vOffset;
		const uint8_t *ptr = rgba.data.data();
		sp.tex = gs_texture_create((uint32_t)rgba.w, (uint32_t)rgba.h, GS_RGBA, 1, &ptr,
					   GS_DYNAMIC);
		f->labels.push_back(sp);
	}
}

void runInference(BodyMaskFilter *f, const Image &frameBGRA)
{
	std::lock_guard<std::mutex> lock(f->modelMutex);
	if (f->seg && f->seg->ok()) {
		SegConfig cfg;
		cfg.threshold = f->threshold;
		cfg.inputSize = f->inputSize;
		cfg.invert = f->invert;
		cfg.featherPx = f->feather;
		f->mask = f->seg->segment(frameBGRA, cfg, /*bgr=*/true);
	}
	if (f->overlay && f->pose && f->pose->ok()) {
		PoseConfig pc;
		pc.confThreshold = f->poseConf;
		auto dets = f->pose->detect(frameBGRA, pc, /*bgr=*/true);
		auto ids = f->tracker.update(dets);
		buildLabelSprites(f, dets, ids);
	}
}

void videoRender(void *data, gs_effect_t *)
{
	auto *f = static_cast<BodyMaskFilter *>(data);
	if (!f->effect || !f->seg) {
		obs_source_skip_video_filter(f->source);
		return;
	}

	const uint32_t w = obs_source_get_base_width(obs_filter_get_target(f->source));
	const uint32_t h = obs_source_get_base_height(obs_filter_get_target(f->source));
	if (w == 0 || h == 0) {
		obs_source_skip_video_filter(f->source);
		return;
	}

	// Rate-limited readback + inference; reuse the previous mask otherwise.
	if (f->frameCounter++ % f->runEvery == 0) {
		Image frame;
		if (getBGRAFromTarget(f->source, f->texrender, &f->stagesurface, frame))
			runInference(f, frame);
	}

	// Upload mask to a GS_R8 texture.
	if (!f->mask.empty()) {
		const uint8_t *mptr = f->mask.data.data();
		if (f->maskTexture && (gs_texture_get_width(f->maskTexture) != (uint32_t)f->mask.w ||
				       gs_texture_get_height(f->maskTexture) != (uint32_t)f->mask.h)) {
			gs_texture_destroy(f->maskTexture);
			f->maskTexture = nullptr;
		}
		if (!f->maskTexture)
			f->maskTexture = gs_texture_create((uint32_t)f->mask.w, (uint32_t)f->mask.h,
							   GS_R8, 1, &mptr, GS_DYNAMIC);
		else
			gs_texture_set_image(f->maskTexture, mptr, (uint32_t)f->mask.w, false);
	}

	if (!f->maskTexture) {
		obs_source_skip_video_filter(f->source);
		return;
	}

	// Base composite pass.
	if (!obs_source_process_filter_begin(f->source, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	gs_eparam_t *maskParam = gs_effect_get_param_by_name(f->effect, "mask");
	gs_effect_set_texture(maskParam, f->maskTexture);

	const char *tech = "Mask";
	if (f->mode == "background") {
		tech = "BackgroundReplace";
		gs_eparam_t *bgParam = gs_effect_get_param_by_name(f->effect, "background");
		gs_texture_t *bgTex = (f->bgType == "image" && f->bgImageLoaded)
					      ? f->bgImage.texture
					      : nullptr;
		struct vec4 color;
		// OBS color int is 0xAABBGGRR.
		vec4_from_rgba(&color, f->bgColor);
		gs_effect_set_vec4(gs_effect_get_param_by_name(f->effect, "solid"), &color);
		gs_effect_set_bool(gs_effect_get_param_by_name(f->effect, "useImage"),
				   bgTex != nullptr);
		if (bgTex)
			gs_effect_set_texture(bgParam, bgTex);
		gs_effect_set_float(gs_effect_get_param_by_name(f->effect, "originalBlend"),
				    f->origBlend);
	}
	// Note: mask invert is applied in the inference core (SegConfig.invert),
	// so the Mask technique is used as-is for both normal and inverted.

	obs_source_process_filter_tech_end(f->source, f->effect, w, h, tech);

	// Label sprites on top.
	if (f->overlay && !f->labels.empty()) {
		gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_eparam_t *img = gs_effect_get_param_by_name(def, "image");
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
		for (const auto &l : f->labels) {
			if (!l.tex)
				continue;
			gs_effect_set_texture(img, l.tex);
			gs_matrix_push();
			gs_matrix_translate3f((float)l.x, (float)l.y, 0.0f);
			while (gs_effect_loop(def, "Draw"))
				gs_draw_sprite(l.tex, 0, (uint32_t)l.w, (uint32_t)l.h);
			gs_matrix_pop();
		}
		gs_blend_state_pop();
	}
}

} // namespace

struct obs_source_info body_mask_filter_info = {
	/* .id            */ "body_mask_filter",
	/* .type          */ OBS_SOURCE_TYPE_FILTER,
	/* .output_flags  */ OBS_SOURCE_VIDEO,
	/* .get_name      */ getName,
	/* .create        */ createFilter,
	/* .destroy       */ destroyFilter,
	/* .get_width     */ nullptr,
	/* .get_height    */ nullptr,
	/* .get_defaults  */ getDefaults,
	/* .get_properties*/ getProperties,
	/* .update        */ updateSettings,
	/* .activate      */ nullptr,
	/* .deactivate    */ nullptr,
	/* .show          */ nullptr,
	/* .hide          */ nullptr,
	/* .video_tick    */ nullptr,
	/* .video_render  */ videoRender,
};
