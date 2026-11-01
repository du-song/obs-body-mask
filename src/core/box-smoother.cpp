#include "box-smoother.h"

#include <algorithm>
#include <cmath>

namespace obm {

namespace {

constexpr float kPi = 3.14159265358979f;

// Head keypoint hysteresis: a keypoint starts to count above kKpOn and stops
// counting below kKpOff. The raw (unsmoothed) path uses a single 0.3 cut.
constexpr float kKpOn = 0.4f;
constexpr float kKpOff = 0.25f;

// Derivative cutoff (Hz) and speed sensitivity of the One Euro filters.
// Speed is measured in head sizes per second.
constexpr float kDerivCutoff = 1.0f;
constexpr float kBetaPos = 1.5f;
constexpr float kBetaSize = 0.5f;

float smoothingAlpha(float dt, float cutoff)
{
	const float tau = 1.0f / (2.0f * kPi * cutoff);
	return 1.0f / (1.0f + tau / dt);
}

} // namespace

float OneEuroFilter::filter(float v, float dt, float minCutoff, float beta, float speedScale)
{
	if (!init_) {
		init_ = true;
		x_ = v;
		dx_ = 0.f;
		return x_;
	}
	const float d = (v - x_) / dt;
	dx_ += smoothingAlpha(dt, kDerivCutoff) * (d - dx_);
	const float cutoff = minCutoff + beta * std::fabs(dx_) / std::max(speedScale, 1.f);
	x_ += smoothingAlpha(dt, cutoff) * (v - x_);
	return x_;
}

std::vector<Box> HeadBoxSmoother::update(const std::vector<Detection> &dets,
					 const std::vector<int> &ids, double tSec,
					 const BoxSmootherConfig &cfg)
{
	std::vector<Box> out;

	// Off: raw per-frame boxes, exactly as before smoothing was added.
	if (cfg.smoothing <= 0.f) {
		states_.clear();
		out.reserve(dets.size());
		for (const auto &d : dets)
			out.push_back(headSquare(d));
		return out;
	}

	const float s = std::min(cfg.smoothing, 1.f);
	// Minimum cutoff falls geometrically from 4 Hz (light) to 0.2 Hz (heavy).
	const float posMinCutoff = 4.0f * std::pow(0.05f, s);
	const float sizeMinCutoff = 0.5f * posMinCutoff;
	// Max relative size change accepted per update: 25% (light) .. 5% (heavy).
	const float maxSizeStep = 0.25f - 0.20f * s;

	for (auto &kv : states_)
		kv.second.misses++;

	const size_t n = std::min(dets.size(), ids.size());
	for (size_t i = 0; i < n; ++i) {
		const Detection &d = dets[i];
		auto it = states_.find(ids[i]);
		const bool fresh = it == states_.end();
		State &st = states_[ids[i]];

		// Hysteresis on which head keypoints count.
		bool use[5];
		for (int k = 0; k < 5; ++k) {
			const float c = d.kpts[k].conf;
			if (fresh)
				st.kpOn[k] = c > 0.3f;
			else if (st.kpOn[k] && c < kKpOff)
				st.kpOn[k] = false;
			else if (!st.kpOn[k] && c > kKpOn)
				st.kpOn[k] = true;
			use[k] = st.kpOn[k];
		}
		const Box raw = headSquare(d, use);
		const float rcx = 0.5f * (raw.x0 + raw.x1);
		const float rcy = 0.5f * (raw.y0 + raw.y1);
		float rhalf = 0.5f * (raw.x1 - raw.x0);

		float dt = fresh ? 0.f : (float)(tSec - st.lastT);
		dt = std::clamp(dt, 1.f / 240.f, 1.f);

		if (!fresh) {
			const float cur = st.half.value();
			rhalf = std::clamp(rhalf, cur * (1.f - maxSizeStep), cur * (1.f + maxSizeStep));
		}
		const float scale = fresh ? 2.f * rhalf : 2.f * st.half.value();
		const float half = st.half.filter(rhalf, dt, sizeMinCutoff, kBetaSize, scale);
		const float cx = st.cx.filter(rcx, dt, posMinCutoff, kBetaPos, scale);
		const float cy = st.cy.filter(rcy, dt, posMinCutoff, kBetaPos, scale);

		st.box = Box{cx - half, cy - half, cx + half, cy + half};
		st.lastT = tSec;
		st.misses = 0;
	}

	// Drop people missing for longer than the hold time; draw the rest
	// (held boxes stay at their last smoothed position).
	for (auto it = states_.begin(); it != states_.end();) {
		if (it->second.misses > cfg.holdFrames) {
			it = states_.erase(it);
		} else {
			out.push_back(it->second.box);
			++it;
		}
	}
	return out;
}

} // namespace obm
