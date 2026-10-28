// test_main.cpp — runs every registered TEST, optionally filtered by name.
//
//   tonnetz_tests.exe            all tests
//   tonnetz_tests.exe tonal      only tests whose name contains "tonal"

#include "test.h"
#include <cstring>

int main(int argc, char **argv)
{
	const char *filter = argc > 1 ? argv[1] : nullptr;
	int run = 0, failedTests = 0;
	for (tt::Case *c = tt::Head(); c; c = c->next)
	{
		if (filter && !std::strstr(c->name, filter)) continue;
		const int before = tt::Failures();
		std::printf("%s\n", c->name);
		c->fn();
		run++;
		if (tt::Failures() != before) failedTests++;
	}
	std::printf("\n%d tests, %d checks, %d failed checks in %d tests\n",
		run, tt::Checks(), tt::Failures(), failedTests);
	return tt::Failures() ? 1 : 0;
}
