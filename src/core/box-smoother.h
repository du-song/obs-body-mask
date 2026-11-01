// Temporal smoothing for the per-person head boxes ("Box" highlight).
//
// Raw pose output moves a few pixels every frame, and the box size jumps when a
// head keypoint (eye/ear) crosses the confidence threshold. This smoother keeps
// one state per tracked person (keyed by the PoseTracker ID) and:
//   - filters box center and size with a One Euro filter (strong smoothing when
//     the head is still, fast follow when it moves);
//   - uses hysteresis on head-keypoint confidence, so a keypoint near the
//     threshold does not switch on and off every frame;
//   - limits how much the box size can change in one update;
//   - holds the last box for a few updates when a person is not detected.
//
// Cost: a few float operations per person per inference step. No GPU work.
#pragma once

#include <map>
#include <vector>

#include "pose-detector.h"

namespace obm {

struct BoxSmootherConfig {
	// 0 = off (raw per-frame boxes, no hold), 1 = maximum smoothing.
	float smoothing = 0.5f;
	// Updates a box is held (drawn at its last position) after its person is
	// no longer detected.
	int holdFrames = 15;
};

// One Euro filter (Casiez et al., CHI 2012) for one scalar. The speed used to
// adapt the cutoff is divided by `speedScale`, so `beta` is independent of
// the image resolution (we pass the head size, so speed is in heads/second).
class OneEuroFilter {
public:
	float filter(float v, float dt, float minCutoff, float beta, float speedScale);
	void reset() { init_ = false; }
	float value() const { return x_; }

private:
	bool init_ = false;
	float x_ = 0.f, dx_ = 0.f;
};

class HeadBoxSmoother {
public:
	// Feed the detections of one inference step, with the tracker IDs aligned
	// to `dets`, at time `tSec` (seconds, any monotonic clock). Returns the
	// boxes to draw: one per live person, including held ones.
	std::vector<Box> update(const std::vector<Detection> &dets, const std::vector<int> &ids,
				double tSec, const BoxSmootherConfig &cfg);

	void reset() { states_.clear(); }

private:
	struct State {
		OneEuroFilter cx, cy, half;
		bool kpOn[5] = {false, false, false, false, false};
		double lastT = 0.0;
		int misses = 0;
		Box box;
	};
	std::map<int, State> states_;
};

} // namespace obm
