// Verifies PoseTracker keeps IDs glued to people across frames in a simulated
// moving-camera sequence: subjects drift, one is briefly occluded (a missed
// frame) then returns, and a new person enters mid-sequence.
#include <cstdio>
#include <vector>

#include "../src/core/pose-tracker.h"

using namespace obm;

static Detection box(float x0, float y0, float x1, float y1)
{
	Detection d;
	d.x0 = x0;
	d.y0 = y0;
	d.x1 = x1;
	d.y1 = y1;
	d.score = 0.9f;
	return d;
}

static int fails = 0;
static void check(bool cond, const char *msg)
{
	printf("  [%s] %s\n", cond ? "PASS" : "FAIL", msg);
	if (!cond)
		fails++;
}

int main()
{
	PoseTracker t;

	// Frame 1: persons A (left) and B (right).
	auto f1 = t.update({box(100, 100, 200, 400), box(400, 100, 500, 400)});
	int idA = f1[0], idB = f1[1];
	check(idA != idB, "frame1: A and B get distinct IDs");

	// Frame 2: camera pans, both drift right; IDs must persist.
	auto f2 = t.update({box(140, 100, 240, 400), box(440, 100, 540, 400)});
	check(f2[0] == idA && f2[1] == idB, "frame2: A and B keep their IDs after motion");

	// Frame 3: A occluded (missed). B drifts. New person C enters at left.
	auto f3 = t.update({box(50, 100, 150, 400), box(480, 100, 580, 400)});
	int idC = f3[0];
	check(f3[1] == idB, "frame3: B keeps its ID");
	check(idC != idA && idC != idB, "frame3: new person C gets a fresh ID");

	// Frame 4: A returns near its last position -> should reclaim its ID
	// (track bridged the one-frame miss). B and C persist.
	auto f4 = t.update(
		{box(180, 100, 280, 400), box(520, 100, 620, 400), box(90, 100, 190, 400)});
	check(f4[0] == idA, "frame4: A reclaims its original ID after occlusion");
	check(f4[1] == idB, "frame4: B keeps its ID");
	check(f4[2] == idC, "frame4: C keeps its ID");

	printf(fails ? "TRACKER TEST FAILED (%d)\n" : "TRACKER TEST PASSED\n", fails);
	return fails ? 1 : 0;
}
