// test.h — a deliberately tiny test framework.
//
//   TEST(name) { CHECK(a == b); CHECK_NEAR(x, 1.0, 0.01); }
//
// Each TEST registers itself; test_main.cpp runs them all and reports. A
// failing CHECK prints file:line and the expression, and the test carries on
// so one run shows every failure.

#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace tt {

struct Case { const char *name; void (*fn)(); Case *next; };
inline Case *&Head() { static Case *h = nullptr; return h; }
inline int &Failures() { static int f = 0; return f; }
inline int &Checks() { static int c = 0; return c; }

struct Registrar
{
	Case c;
	Registrar(const char *name, void (*fn)()) : c{ name, fn, nullptr }
	{
		// Append, so tests run in file order.
		Case **p = &Head();
		while (*p) p = &(*p)->next;
		*p = &c;
	}
};

inline void Fail(const char *file, int line, const char *expr)
{
	Failures()++;
	std::printf("    FAIL %s:%d  %s\n", file, line, expr);
}

} // namespace tt

#define TT_CAT2(a, b) a##b
#define TT_CAT(a, b) TT_CAT2(a, b)
#define TEST(name)                                                         \
	static void name();                                                    \
	static tt::Registrar TT_CAT(reg_, name)(#name, &name);                 \
	static void name()

#define CHECK(cond)                                                        \
	do { tt::Checks()++; if (!(cond)) tt::Fail(__FILE__, __LINE__, #cond); } while (0)

#define CHECK_NEAR(a, b, tol)                                              \
	do {                                                                   \
		tt::Checks()++;                                                    \
		double _a = (double)(a), _b = (double)(b);                         \
		if (std::fabs(_a - _b) > (double)(tol)) {                          \
			tt::Fail(__FILE__, __LINE__, #a " ~= " #b);                    \
			std::printf("         %g vs %g (tol %g)\n", _a, _b, (double)(tol)); \
		}                                                                  \
	} while (0)

/// Print a measured value alongside the checks, so a run doubles as a report.
#define NOTE(...) do { std::printf("    . "); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
