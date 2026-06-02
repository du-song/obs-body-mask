// Human body silhouette segmentation. Produces a single-channel mask where
// 255 = human, 0 = background. Semantic (per-pixel) segmentation, so it is
// independent of how many people are in frame and works for back-facing
// subjects (it segments a silhouette, not a face).
#pragma once

#include <memory>
#include <string>

#include "image.h"
#include "../ort-utils/ort-session-utils.h"

namespace obm {

struct SegConfig {
	int inputSize = 256;     // model square input (used when the model is dynamic)
	float threshold = 0.5f;  // probability cutoff for human vs background
	bool invert = false;     // swap white/black
	int featherPx = 0;       // box-blur radius applied to the mask edge
};

class BodySegmenter {
public:
	BodySegmenter(Ort::Env &env, const std::string &modelPath, Backend backend);
	bool ok() const { return model_ != nullptr; }

	// `img` is interleaved 8-bit; set bgr=true if channel order is BGR(A)
	// (as OBS delivers) rather than RGB(A). Returns a w*h single-channel mask
	// at the same resolution as `img`.
	Image segment(const Image &img, const SegConfig &cfg, bool bgr = false);

private:
	std::unique_ptr<OrtModel> model_;
	bool nchw_ = false; // input tensor layout
	int inH_ = 256, inW_ = 256;
	int dynamic_ = false;
};

} // namespace obm
