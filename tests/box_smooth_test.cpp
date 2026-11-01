// Verifies HeadBoxSmoother: it removes per-frame keypoint noise on a still
// head, ignores a keypoint that flickers around the confidence threshold,
// still follows a moving head, limits sudden size jumps, holds a box through
// short misses, and is a pass-through when smoothing is 0.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "../src/core/box-smoother.h"

using namespace obm;

static int fails = 0;
static void check(bool cond, const char *msg)
{
	printf("  [%s] %s\n", cond ? "PASS" : "FAIL", msg);
	if (!cond)
		fails++;
}

// Deterministic noise in [-amp, amp].
static uint32_t rng = 12345;
static float noise(float amp)
{
	rng = rng * 1664525u + 1013904223u;
	return amp * (2.f * (float)(rng >> 8) / (float)(1u << 24) - 1.f);
}

// A person whose head (nose) is at (cx, cy); H = head width in px.
static Detection person(float cx, float cy, float H, float jitter, float earConf = 0.9f)
{
	Detection d;
	d.x0 = cx - 60;
	d.x1 = cx + 60;
	d.y0 = cy - 40;
	d.y1 = cy + 360;
	d.score = 0.9f;
	const float kx[5] = {0.f, -0.15f, 0.15f, -0.35f, 0.35f};
	const float ky[5] = {0.f, -0.1f, -0.1f, 0.f, 0.f};
	for (int k = 0; k < 5; ++k) {
		d.kpts[k].x = cx + kx[k] * H + noise(jitter);
		d.kpts[k].y = cy + ky[k] * H + noise(jitter);
		d.kpts[k].conf = k >= 3 ? earConf : 0.9f;
	}
	d.headX = cx;
	d.headY = cy;
	return d;
}

static float cxOf(const Box &b) { return 0.5f * (b.x0 + b.x1); }
static float cyOf(const Box &b) { return 0.5f * (b.y0 + b.y1); }
static float halfOf(const Box &b) { return 0.5f * (b.x1 - b.x0); }

int main()
{
	const double dt = 1.0 / 30.0;
	BoxSmootherConfig cfg; // smoothing 0.5, hold 15
	const std::vector<int> id1{1};

	// 1. Still head with +-4 px keypoint noise: center jitter must drop a lot.
	{
		HeadBoxSmoother sm;
		float rawMove = 0, smMove = 0;
		Box prevRaw{}, prevSm{};
		for (int i = 0; i < 90; ++i) {
			Detection d = person(500, 300, 60, 4.f);
			Box raw = headSquare(d);
			Box s = sm.update({d}, id1, i * dt, cfg)[0];
			if (i > 0) {
				rawMove += std::hypot(cxOf(raw) - cxOf(prevRaw), cyOf(raw) - cyOf(prevRaw));
				smMove += std::hypot(cxOf(s) - cxOf(prevSm), cyOf(s) - cyOf(prevSm));
			}
			prevRaw = raw;
			prevSm = s;
		}
		printf("  still: raw center move %.1f px/frame, smoothed %.2f px/frame\n",
		       rawMove / 89, smMove / 89);
		check(smMove < 0.25f * rawMove, "still head: smoothed jitter < 25% of raw");
	}

	// 2. Ears flicker around the 0.3 threshold: raw size jumps, smoothed not.
	{
		HeadBoxSmoother sm;
		float rawMin = 1e9f, rawMax = 0, smMin = 1e9f, smMax = 0;
		for (int i = 0; i < 90; ++i) {
			Detection d = person(500, 300, 60, 0.f, (i % 2) ? 0.35f : 0.28f);
			float rh = halfOf(headSquare(d));
			float sh = halfOf(sm.update({d}, id1, i * dt, cfg)[0]);
			if (i >= 30) {
				rawMin = std::min(rawMin, rh);
				rawMax = std::max(rawMax, rh);
				smMin = std::min(smMin, sh);
				smMax = std::max(smMax, sh);
			}
		}
		printf("  ear flicker: raw size range %.1f px, smoothed %.2f px\n", rawMax - rawMin,
		       smMax - smMin);
		check(rawMax - rawMin > 10.f, "ear flicker: raw size really jumps (test is valid)");
		check(smMax - smMin < 1.f, "ear flicker: smoothed size stays stable");
	}

	// 3. Head moves at 300 px/s: the box must follow with small lag.
	{
		HeadBoxSmoother sm;
		Box s{};
		for (int i = 0; i < 60; ++i)
			s = sm.update({person(200 + 10.f * i, 300, 60, 2.f)}, id1, i * dt, cfg)[0];
		const float lag = (200 + 10.f * 59) - cxOf(s);
		printf("  moving 300 px/s: lag %.1f px\n", lag);
		check(std::fabs(lag) < 20.f, "moving head: lag < 20 px");
	}

	// 4. Keypoints lost (fallback sizing): size changes at most 15% per update.
	{
		HeadBoxSmoother sm;
		float prev = 0, worst = 0;
		for (int i = 0; i < 60; ++i) {
			Detection d = person(500, 300, 120, 0.f);
			if (i >= 30)
				for (auto &k : d.kpts)
					k.conf = 0.f; // back turned: fallback to body-box size
			float h = halfOf(sm.update({d}, id1, i * dt, cfg)[0]);
			if (i > 0)
				worst = std::max(worst, std::fabs(h - prev) / prev);
			prev = h;
		}
		printf("  sizing mode switch: worst size step %.1f%%\n", worst * 100);
		check(worst <= 0.15f + 1e-4f, "mode switch: size step <= 15% per update");
	}

	// 5. Hold: box stays (unchanged) for holdFrames misses, then disappears.
	{
		HeadBoxSmoother sm;
		BoxSmootherConfig c = cfg;
		c.holdFrames = 3;
		Box last{};
		for (int i = 0; i < 10; ++i)
			last = sm.update({person(500, 300, 60, 0.f)}, id1, i * dt, c)[0];
		bool heldSame = true;
		for (int i = 0; i < 3; ++i) {
			auto out = sm.update({}, {}, (10 + i) * dt, c);
			heldSame = heldSame && out.size() == 1 && out[0].x0 == last.x0 &&
				   out[0].y0 == last.y0;
		}
		check(heldSame, "hold: box kept at last position for 3 missed updates");
		check(sm.update({}, {}, 13 * dt, c).empty(), "hold: box removed after the 4th miss");
	}

	// 6. Smoothing 0: identical to the raw per-frame box, no hold.
	{
		HeadBoxSmoother sm;
		BoxSmootherConfig c = cfg;
		c.smoothing = 0.f;
		bool same = true;
		for (int i = 0; i < 10; ++i) {
			Detection d = person(500, 300, 60, 4.f);
			Box r = headSquare(d), s = sm.update({d}, id1, i * dt, c)[0];
			same = same && r.x0 == s.x0 && r.y0 == s.y0 && r.x1 == s.x1 && r.y1 == s.y1;
		}
		check(same, "smoothing 0: output equals raw headSquare");
		check(sm.update({}, {}, 1.0, c).empty(), "smoothing 0: no hold on miss");
	}

	printf(fails ? "BOX SMOOTH TEST FAILED (%d)\n" : "BOX SMOOTH TEST PASSED\n", fails);
	return fails ? 1 : 0;
}
