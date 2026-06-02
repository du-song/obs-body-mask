// Lightweight per-frame tracker that assigns persistent IDs to pose
// detections via greedy IoU association. Keeps "Person N" labels glued to the
// same individual across frames — essential for a moving camera or people
// entering/leaving, where per-frame left-to-right numbering would reshuffle.
// Also bridges brief misses (occlusion / a dropped detection) for `maxMisses`
// frames so labels don't flicker.
#pragma once

#include <vector>

#include "pose-detector.h"

namespace obm {

class PoseTracker {
public:
	// Returns a vector of persistent IDs aligned to `dets`. IDs start at 1
	// and increase over the session (they are not recycled immediately, so
	// a number stays bound to one person).
	std::vector<int> update(const std::vector<Detection> &dets, float iouThresh = 0.3f,
				 int maxMisses = 15);

	void reset() { tracks_.clear(); nextId_ = 1; }

private:
	struct Track {
		int id;
		float x0, y0, x1, y1;
		int misses;
	};
	std::vector<Track> tracks_;
	int nextId_ = 1;
};

} // namespace obm
