// Multi-person pose detection (YOLO11-pose) used to locate heads for the text
// overlay. Single-shot, scales to many people in one pass, and detects head
// keypoints from behind (it does not require a visible face). Provides a head
// anchor per person with a bbox-top fallback for fully back-facing subjects.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "image.h"
#include "../ort-utils/ort-session-utils.h"

namespace obm {

struct Keypoint {
	float x = 0, y = 0, conf = 0;
};

struct Detection {
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0; // bbox in source pixels
	float score = 0;
	Keypoint kpts[17]; // COCO order; 0 nose, 1/2 eyes, 3/4 ears
	float headX = 0, headY = 0; // chosen head anchor in source pixels
};

// Axis-aligned box in source pixels.
struct Box {
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// Square box around the head, sized from the confident head keypoints
// (nose/eyes/ears) with a margin; falls back to a fraction of the person's
// body box (centered on the head anchor) when the head isn't keypoint-visible.
Box headSquare(const Detection &d);
// Same, but only the head keypoints (0..4) with use[k] == true count. Used by
// the box smoother, which picks keypoints with confidence hysteresis.
Box headSquare(const Detection &d, const bool use[5]);

struct PoseConfig {
	int inputSize = 640;
	float confThreshold = 0.25f;
	float iouThreshold = 0.45f;
};

class PoseDetector {
public:
	PoseDetector(Ort::Env &env, const std::string &modelPath, Backend backend);
	bool ok() const { return model_ != nullptr; }

	// Returns one detection per person, sorted left-to-right (stable
	// "Person N" numbering). `bgr` flags BGR(A) input channel order.
	std::vector<Detection> detect(const Image &img, const PoseConfig &cfg, bool bgr = false);

private:
	std::unique_ptr<OrtModel> model_;
	int inSize_ = 640;
};

} // namespace obm
