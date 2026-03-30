#include "test_harness.h"

int g_passed = 0;
int g_failed = 0;
int g_total = 0;

std::vector<TestCase>& GetTests()
{
	static std::vector<TestCase> tests;
	return tests;
}

int main(int argc, char* argv[])
{
	const char* filter = (argc > 1) ? argv[1] : nullptr;

	auto& tests = GetTests();
	int ran = 0;

	for(auto& t : tests) {
		if(filter && !strstr(t.name, filter)) continue;

		fprintf(stderr, "--- %s ---\n", t.name);
		int failBefore = g_failed;
		t.fn();
		if(g_failed == failBefore) {
			fprintf(stderr, "  OK\n");
		}
		ran++;
	}

	fprintf(stderr, "\n========================================\n");
	fprintf(stderr, "Ran %d test(s), %d assertion(s): %d passed, %d failed\n",
		ran, g_total, g_passed, g_failed);
	fprintf(stderr, "========================================\n");

	return g_failed > 0 ? 1 : 0;
}
