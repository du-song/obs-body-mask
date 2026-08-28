// Verifies the label template expansion used by both the OBS filter and the
// CLI: every run of '#' becomes the person number, other text is literal.
#include <cstdio>
#include <string>

#include "../src/core/text-raster.h"

using namespace obm;

static int fails = 0;
static void check(const std::string &got, const std::string &want)
{
	const bool ok = got == want;
	printf("  [%s] \"%s\" (want \"%s\")\n", ok ? "PASS" : "FAIL", got.c_str(), want.c_str());
	if (!ok)
		++fails;
}

int main()
{
	printf("label format:\n");
	check(formatLabel("Person ###", 1), "Person 1");
	check(formatLabel("Person ###", 123), "Person 123");
	check(formatLabel("#", 7), "7");            // any run length, no padding
	check(formatLabel("##/##", 4), "4/4");      // every run expands
	check(formatLabel("Guest", 3), "Guest");    // no placeholder: literal
	check(formatLabel("", 3), "");
	printf("%s\n", fails ? "FAILED" : "OK");
	return fails ? 1 : 0;
}
