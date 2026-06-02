#include "pose-detector.h"

#include <algorithm>
#include <cstdio>

namespace obm {

PoseDetector::PoseDetector(Ort::Env &env, const std::string &modelPath, Backend backend)
{
	model_ = createOrtModel(env, modelPath, backend);
	if (model_) {
		const auto &s = model_->inputShapes[0]; // [1,3,H,W]
		if (s.size() == 4 && s[2] > 0)
			inSize_ = (int)s[2];
	}
}

// Letterbox: scale to fit inSize x inSize preserving aspect, pad with gray.
static Image letterbox(const Image &src, int size, float &scale, int &padX, int &padY)
{
	scale = std::min((float)size / src.w, (float)size / src.h);
	int nw = (int)std::round(src.w * scale);
	int nh = (int)std::round(src.h * scale);
	padX = (size - nw) / 2;
	padY = (size - nh) / 2;
	Image resized = resizeBilinear(src, nw, nh);
	Image out(size, size, src.c);
	std::fill(out.data.begin(), out.data.end(), (uint8_t)114);
	for (int y = 0; y < nh; ++y)
		std::copy(resized.pixel(0, y), resized.pixel(0, y) + (size_t)nw * src.c,
			  out.pixel(padX, padY + y));
	return out;
}

static float iou(const Detection &a, const Detection &b)
{
	float ix0 = std::max(a.x0, b.x0), iy0 = std::max(a.y0, b.y0);
	float ix1 = std::min(a.x1, b.x1), iy1 = std::min(a.y1, b.y1);
	float iw = std::max(0.f, ix1 - ix0), ih = std::max(0.f, iy1 - iy0);
	float inter = iw * ih;
	float ua = (a.x1 - a.x0) * (a.y1 - a.y0) + (b.x1 - b.x0) * (b.y1 - b.y0) - inter;
	return ua > 0 ? inter / ua : 0.f;
}

std::vector<Detection> PoseDetector::detect(const Image &img, const PoseConfig &cfg, bool bgr)
{
	std::vector<Detection> result;
	if (!model_ || img.empty())
		return result;

	const int S = inSize_;
	float scale;
	int padX, padY;
	Image lb = letterbox(img, S, scale, padX, padY);

	// NCHW RGB 0..1
	std::vector<float> input((size_t)3 * S * S);
	const size_t hw = (size_t)S * S;
	const int rIdx = bgr ? 2 : 0, bIdx = bgr ? 0 : 2;
	for (int y = 0; y < S; ++y)
		for (int x = 0; x < S; ++x) {
			const uint8_t *p = lb.pixel(x, y);
			size_t o = (size_t)y * S + x;
			input[o] = p[rIdx] / 255.f;
			input[hw + o] = p[1] / 255.f;
			input[2 * hw + o] = p[bIdx] / 255.f;
		}

	std::vector<int64_t> inShape{1, 3, S, S};
	Ort::Value inTensor = Ort::Value::CreateTensor<float>(
		model_->memoryInfo, input.data(), input.size(), inShape.data(), inShape.size());
	auto inNames = model_->inputNamePtrs();
	auto outNames = model_->outputNamePtrs();
	std::vector<Ort::Value> outputs;
	try {
		outputs = model_->session->Run(Ort::RunOptions{nullptr}, inNames.data(), &inTensor,
					       1, outNames.data(), outNames.size());
	} catch (const Ort::Exception &e) {
		fprintf(stderr, "[obs-body-mask] pose inference failed: %s\n", e.what());
		return result;
	}

	// Two output layouts are supported, distinguished by feature width:
	//   * feat == 57  -> YOLO26 (NMS-free, end-to-end): per detection
	//       [x0,y0,x1,y1, conf, cls, 17*(x,y,conf)], already filtered and
	//       sorted by confidence. Boxes are xyxy. No NMS needed.
	//   * feat == 56  -> YOLOv8/YOLO11: per anchor
	//       [cx,cy,w,h, conf, 17*(x,y,conf)] over a dense grid. Boxes are
	//       cxcywh and the candidates require NMS.
	// Either layout may be channels-first ([1,feat,N]) or anchors-first
	// ([1,N,feat]).
	auto os = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
	const float *d = outputs[0].GetTensorData<float>();
	if (os.size() != 3) {
		fprintf(stderr, "[obs-body-mask] unexpected pose output rank\n");
		return result;
	}
	int feat = 0, n = 0;
	bool chFirst = false;
	for (int F : {57, 56}) {
		if (os[1] == F) {
			chFirst = true;
			feat = F;
			n = (int)os[2];
			break;
		} else if (os[2] == F) {
			chFirst = false;
			feat = F;
			n = (int)os[1];
			break;
		}
	}
	if (feat == 0) {
		fprintf(stderr, "[obs-body-mask] unexpected pose output shape\n");
		return result;
	}
	const bool nmsFree = (feat == 57); // YOLO26 end-to-end
	const int kptStart = nmsFree ? 6 : 5;
	auto at = [&](int f, int i) -> float {
		return chFirst ? d[(size_t)f * n + i] : d[(size_t)i * feat + f];
	};

	std::vector<Detection> cand;
	for (int i = 0; i < n; ++i) {
		float conf = at(4, i);
		if (conf < cfg.confThreshold)
			continue;
		Detection det;
		if (nmsFree) { // xyxy in input space
			det.x0 = (at(0, i) - padX) / scale;
			det.y0 = (at(1, i) - padY) / scale;
			det.x1 = (at(2, i) - padX) / scale;
			det.y1 = (at(3, i) - padY) / scale;
		} else { // cxcywh
			float cx = at(0, i), cy = at(1, i), w = at(2, i), h = at(3, i);
			det.x0 = (cx - w / 2 - padX) / scale;
			det.y0 = (cy - h / 2 - padY) / scale;
			det.x1 = (cx + w / 2 - padX) / scale;
			det.y1 = (cy + h / 2 - padY) / scale;
		}
		det.score = conf;
		for (int k = 0; k < 17; ++k) {
			det.kpts[k].x = (at(kptStart + k * 3, i) - padX) / scale;
			det.kpts[k].y = (at(kptStart + k * 3 + 1, i) - padY) / scale;
			det.kpts[k].conf = at(kptStart + k * 3 + 2, i);
		}
		cand.push_back(det);
	}

	std::sort(cand.begin(), cand.end(),
		  [](const Detection &a, const Detection &b) { return a.score > b.score; });
	if (nmsFree) {
		result = std::move(cand); // already deduplicated by the model
	} else {
		std::vector<bool> removed(cand.size(), false);
		for (size_t i = 0; i < cand.size(); ++i) {
			if (removed[i])
				continue;
			result.push_back(cand[i]);
			for (size_t j = i + 1; j < cand.size(); ++j)
				if (!removed[j] && iou(cand[i], cand[j]) > cfg.iouThreshold)
					removed[j] = true;
		}
	}

	// Head anchor: confidence-weighted mean of head keypoints (nose, eyes,
	// ears); fall back to top-center of bbox when none are confident
	// (e.g. fully back-facing and the model gives low keypoint scores).
	for (auto &det : result) {
		float sx = 0, sy = 0, sw = 0;
		for (int k = 0; k < 5; ++k) {
			if (det.kpts[k].conf > 0.3f) {
				sx += det.kpts[k].x * det.kpts[k].conf;
				sy += det.kpts[k].y * det.kpts[k].conf;
				sw += det.kpts[k].conf;
			}
		}
		if (sw > 0) {
			det.headX = sx / sw;
			det.headY = sy / sw;
		} else {
			det.headX = (det.x0 + det.x1) * 0.5f;
			det.headY = det.y0;
		}
	}

	std::sort(result.begin(), result.end(),
		  [](const Detection &a, const Detection &b) { return a.headX < b.headX; });
	return result;
}

} // namespace obm
