#include "pose-tracker.h"

#include <algorithm>

namespace obm {

static float iouBox(float ax0, float ay0, float ax1, float ay1, float bx0, float by0, float bx1,
		    float by1)
{
	float ix0 = std::max(ax0, bx0), iy0 = std::max(ay0, by0);
	float ix1 = std::min(ax1, bx1), iy1 = std::min(ay1, by1);
	float iw = std::max(0.f, ix1 - ix0), ih = std::max(0.f, iy1 - iy0);
	float inter = iw * ih;
	float ua = (ax1 - ax0) * (ay1 - ay0) + (bx1 - bx0) * (by1 - by0) - inter;
	return ua > 0 ? inter / ua : 0.f;
}

std::vector<int> PoseTracker::update(const std::vector<Detection> &dets, float iouThresh,
				     int maxMisses)
{
	std::vector<int> ids(dets.size(), -1);

	// Score every track/detection pair, then greedily match highest IoU
	// first so each track and detection is used at most once.
	struct Pair {
		float iou;
		int t, d;
	};
	std::vector<Pair> pairs;
	for (size_t t = 0; t < tracks_.size(); ++t)
		for (size_t d = 0; d < dets.size(); ++d) {
			float i = iouBox(tracks_[t].x0, tracks_[t].y0, tracks_[t].x1, tracks_[t].y1,
					 dets[d].x0, dets[d].y0, dets[d].x1, dets[d].y1);
			if (i >= iouThresh)
				pairs.push_back({i, (int)t, (int)d});
		}
	std::sort(pairs.begin(), pairs.end(),
		  [](const Pair &a, const Pair &b) { return a.iou > b.iou; });

	std::vector<bool> trackUsed(tracks_.size(), false);
	std::vector<bool> detUsed(dets.size(), false);
	for (const auto &p : pairs) {
		if (trackUsed[p.t] || detUsed[p.d])
			continue;
		trackUsed[p.t] = true;
		detUsed[p.d] = true;
		ids[p.d] = tracks_[p.t].id;
		tracks_[p.t].x0 = dets[p.d].x0;
		tracks_[p.t].y0 = dets[p.d].y0;
		tracks_[p.t].x1 = dets[p.d].x1;
		tracks_[p.t].y1 = dets[p.d].y1;
		tracks_[p.t].misses = 0;
	}

	// Age unmatched existing tracks and drop the stale ones. Do this before
	// appending new tracks so trackUsed (sized to the pre-frame track count)
	// stays in bounds.
	for (size_t t = 0; t < tracks_.size(); ++t)
		if (!trackUsed[t])
			tracks_[t].misses++;
	tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
				     [&](const Track &tr) { return tr.misses > maxMisses; }),
		      tracks_.end());

	// New people -> new IDs.
	for (size_t d = 0; d < dets.size(); ++d) {
		if (detUsed[d])
			continue;
		Track tr{nextId_++, dets[d].x0, dets[d].y0, dets[d].x1, dets[d].y1, 0};
		ids[d] = tr.id;
		tracks_.push_back(tr);
	}

	return ids;
}

} // namespace obm
