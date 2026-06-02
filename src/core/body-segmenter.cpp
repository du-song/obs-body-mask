#include "body-segmenter.h"

#include <algorithm>
#include <cstdio>

namespace obm {

BodySegmenter::BodySegmenter(Ort::Env &env, const std::string &modelPath, Backend backend)
{
	model_ = createOrtModel(env, modelPath, backend);
	if (!model_)
		return;

	// Detect input layout/size from the model. Selfie Segmentation is
	// NHWC [1,256,256,3]; some exports are NCHW [1,3,H,W]. Dynamic dims
	// (-1) fall back to the configured input size.
	const auto &s = model_->inputShapes[0];
	if (s.size() == 4) {
		if (s[1] == 3) { // NCHW
			nchw_ = true;
			inH_ = (int)s[2];
			inW_ = (int)s[3];
		} else { // NHWC
			nchw_ = false;
			inH_ = (int)s[1];
			inW_ = (int)s[2];
		}
	}
	if (inH_ <= 0 || inW_ <= 0)
		dynamic_ = true;
}

Image BodySegmenter::segment(const Image &img, const SegConfig &cfg, bool bgr)
{
	Image mask(img.w, img.h, 1);
	if (!model_ || img.empty())
		return mask;

	int H = dynamic_ ? cfg.inputSize : inH_;
	int W = dynamic_ ? cfg.inputSize : inW_;

	// Resize to model input, then pack normalized RGB into the tensor.
	Image small = resizeBilinear(img, W, H);
	std::vector<float> input((size_t)3 * H * W);
	const int rIdx = bgr ? 2 : 0;
	const int bIdx = bgr ? 0 : 2;
	for (int y = 0; y < H; ++y) {
		for (int x = 0; x < W; ++x) {
			const uint8_t *p = small.pixel(x, y);
			float r = p[rIdx] / 255.f, g = p[1] / 255.f, b = p[bIdx] / 255.f;
			if (nchw_) {
				size_t hw = (size_t)H * W;
				size_t o = (size_t)y * W + x;
				input[o] = r;
				input[hw + o] = g;
				input[2 * hw + o] = b;
			} else {
				size_t o = ((size_t)y * W + x) * 3;
				input[o] = r;
				input[o + 1] = g;
				input[o + 2] = b;
			}
		}
	}

	std::vector<int64_t> inShape = nchw_ ? std::vector<int64_t>{1, 3, H, W}
					     : std::vector<int64_t>{1, H, W, 3};
	Ort::Value inTensor = Ort::Value::CreateTensor<float>(
		model_->memoryInfo, input.data(), input.size(), inShape.data(), inShape.size());

	auto inNames = model_->inputNamePtrs();
	auto outNames = model_->outputNamePtrs();
	std::vector<Ort::Value> outputs;
	try {
		outputs = model_->session->Run(Ort::RunOptions{nullptr}, inNames.data(), &inTensor,
					       1, outNames.data(), outNames.size());
	} catch (const Ort::Exception &e) {
		fprintf(stderr, "[obs-body-mask] seg inference failed: %s\n", e.what());
		return mask;
	}

	// Interpret the output probability map. Supported shapes:
	//   [1,H,W,1] / [1,1,H,W]  -> sigmoid foreground prob
	//   [1,H,W,2] / [1,2,H,W]  -> softmax, channel 1 = foreground
	auto info = outputs[0].GetTensorTypeAndShapeInfo();
	auto os = info.GetShape();
	const float *od = outputs[0].GetTensorData<float>();
	int oH = H, oW = W, classes = 1;
	bool outNchw = false;
	if (os.size() == 4) {
		if (os[3] == 1 || os[3] == 2) { // NHWC
			oH = (int)os[1];
			oW = (int)os[2];
			classes = (int)os[3];
		} else { // NCHW
			outNchw = true;
			classes = (int)os[1];
			oH = (int)os[2];
			oW = (int)os[3];
		}
	}

	// Build a HxW probability image, then resize back to source resolution.
	Image prob(oW, oH, 1);
	for (int y = 0; y < oH; ++y) {
		for (int x = 0; x < oW; ++x) {
			float v;
			if (classes == 2) {
				float c0, c1;
				if (outNchw) {
					size_t hw = (size_t)oH * oW;
					c0 = od[(size_t)y * oW + x];
					c1 = od[hw + (size_t)y * oW + x];
				} else {
					size_t o = ((size_t)y * oW + x) * 2;
					c0 = od[o];
					c1 = od[o + 1];
				}
				// softmax of two logits -> P(foreground)
				float m = std::max(c0, c1);
				float e0 = std::exp(c0 - m), e1 = std::exp(c1 - m);
				v = e1 / (e0 + e1);
			} else {
				v = od[(size_t)y * oW + x];
				if (v < 0.f || v > 1.f) // logit -> sigmoid
					v = 1.f / (1.f + std::exp(-v));
			}
			prob.data[(size_t)y * oW + x] = (uint8_t)std::clamp(v * 255.f, 0.f, 255.f);
		}
	}

	Image full = resizeBilinear(prob, img.w, img.h);
	const uint8_t cut = (uint8_t)std::clamp(cfg.threshold * 255.f, 0.f, 255.f);
	for (size_t i = 0; i < full.data.size(); ++i) {
		bool human = full.data[i] >= cut;
		if (cfg.invert)
			human = !human;
		mask.data[i] = human ? 255 : 0;
	}
	if (cfg.featherPx > 0)
		mask = boxBlur1(mask, cfg.featherPx);
	return mask;
}

} // namespace obm
