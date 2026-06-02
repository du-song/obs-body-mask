// Standalone test driver for the shared segmentation/pose/compositing core.
// Runs on Linux and macOS without OBS, so the models and pipeline can be
// validated on single images (back-facing subjects, crowds, background swap,
// per-head labels) before integrating into the OBS plugin.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include <onnxruntime_cxx_api.h>

#include "../../src/core/body-segmenter.h"
#include "../../src/core/pose-detector.h"
#include "../../src/core/pose-tracker.h"
#include "../../src/core/compositor.h"
#include "../../src/core/text-raster.h"
#include "../../src/core/image-io.h"

using namespace obm;

static Color parseHexColor(const std::string &hex)
{
	Color c;
	if (hex.size() == 6) {
		auto h = [&](int i) { return (uint8_t)strtol(hex.substr(i, 2).c_str(), nullptr, 16); };
		c.r = h(0);
		c.g = h(2);
		c.b = h(4);
	}
	return c;
}

static void usage()
{
	fprintf(stderr,
		"usage: body-mask-cli --input IMG --output PNG --mode mask|background|overlay\n"
		"  --model FILE          segmentation onnx (default models/selfie_segmentation.onnx)\n"
		"  --threshold 0..1      segmentation cutoff (default 0.5)\n"
		"  --input-size N        seg model input size (default 256)\n"
		"  --invert              invert mask\n"
		"  --feather N           mask edge box-blur radius (default 0)\n"
		"  --bg-image FILE       background image (background mode)\n"
		"  --bg-color RRGGBB     solid background color (background mode)\n"
		"  --original-blend 0..100  %% of original kept (default 0)\n"
		"  --pose-model FILE     pose onnx (default models/yolo26n-pose.onnx)\n"
		"  --font FILE           ttf for labels (default fonts/overlay.ttf)\n"
		"  --font-size N         label pixel height (default 28)\n"
		"  --highlight off|outline|box  highlight detected bodies (default off)\n"
		"  --highlight-color RRGGBB     highlight color (default FF0000)\n"
		"  --highlight-thickness N      outline/box line width in px (default 3)\n"
		"  --backend cpu|coreml  (default cpu)\n");
}

int main(int argc, char **argv)
{
	std::map<std::string, std::string> opt;
	std::map<std::string, bool> flag;
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		if (a.rfind("--", 0) != 0)
			continue;
		std::string key = a.substr(2);
		if (key == "invert") {
			flag[key] = true;
		} else if (i + 1 < argc) {
			opt[key] = argv[++i];
		}
	}
	auto get = [&](const std::string &k, const std::string &def) {
		auto it = opt.find(k);
		return it == opt.end() ? def : it->second;
	};

	const std::string input = get("input", "");
	const std::string output = get("output", "");
	const std::string mode = get("mode", "mask");
	if (input.empty() || output.empty()) {
		usage();
		return 2;
	}

	Image src = loadImageRGB(input);
	if (src.empty())
		return 1;

	Backend backend = (get("backend", "cpu") == "coreml") ? Backend::CoreML : Backend::CPU;
	Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "body-mask-cli");

	if (mode == "mask" || mode == "background") {
		BodySegmenter seg(env, get("model", "models/selfie_segmentation.onnx"), backend);
		if (!seg.ok())
			return 1;
		SegConfig cfg;
		cfg.threshold = std::stof(get("threshold", "0.5"));
		cfg.inputSize = std::stoi(get("input-size", "256"));
		cfg.invert = flag["invert"];
		cfg.featherPx = std::stoi(get("feather", "0"));
		Image mask = seg.segment(src, cfg, /*bgr=*/false);

		Image out;
		if (mode == "mask") {
			out = maskToRGB(mask);
		} else {
			bool useImage = opt.count("bg-image") > 0;
			Image bg = useImage ? loadImageRGB(opt["bg-image"]) : Image();
			Color solid = parseHexColor(get("bg-color", "00FF00"));
			float ob = std::stof(get("original-blend", "0")) / 100.f;
			out = backgroundReplace(src, mask, bg, solid, useImage, ob);
		}
		const std::string hl = get("highlight", "off");
		const Color hlColor = parseHexColor(get("highlight-color", "FF0000"));
		const int hlThick = std::stoi(get("highlight-thickness", "3"));
		if (hl == "outline") {
			drawEdge(out, bodyEdge(mask, hlThick), hlColor);
		} else if (hl == "box") {
			PoseDetector pose(env, get("pose-model", "models/yolo26n-pose.onnx"), backend);
			if (pose.ok()) {
				PoseConfig pc;
				pc.confThreshold = std::stof(get("pose-conf", "0.25"));
				for (const auto &d : pose.detect(src, pc, /*bgr=*/false)) {
					Box b = headSquare(d);
					drawRect(out, (int)b.x0, (int)b.y0, (int)b.x1, (int)b.y1, hlColor,
						 hlThick);
				}
			}
		}
		if (!savePNG(output, out))
			return 1;
		printf("wrote %s (%dx%d)\n", output.c_str(), out.w, out.h);
		return 0;
	}

	if (mode == "overlay") {
		PoseDetector pose(env, get("pose-model", "models/yolo26n-pose.onnx"), backend);
		if (!pose.ok())
			return 1;
		PoseConfig pc;
		pc.confThreshold = std::stof(get("threshold", "0.25"));
		auto dets = pose.detect(src, pc, /*bgr=*/false);

		TextRaster tr(get("font", "fonts/overlay.ttf"));
		if (!tr.ok())
			return 1;
		float fontSize = std::stof(get("font-size", "28"));
		Image canvas = src; // draw on a copy of the original
		PoseTracker tracker; // persistent IDs (stable across frames in video use)
		auto ids = tracker.update(dets);
		for (size_t i = 0; i < dets.size(); ++i) {
			Image label = tr.render("Person " + std::to_string(ids[i]), fontSize,
						 Color{255, 230, 0});
			drawLabel(canvas, label, (int)dets[i].headX, (int)dets[i].headY - 4);
		}
		if (!savePNG(output, canvas))
			return 1;
		printf("wrote %s (%d people)\n", output.c_str(), (int)dets.size());
		return 0;
	}

	usage();
	return 2;
}
