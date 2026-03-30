#pragma once
// Minimal unit test harness for XRetroCode DAP tests
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <functional>

struct TestCase {
	const char* name;
	std::function<void()> fn;
};

std::vector<TestCase>& GetTests();

extern int g_passed;
extern int g_failed;
extern int g_total;

#define TEST(name) \
	static void test_##name(); \
	namespace { struct TestReg_##name { \
		TestReg_##name() { GetTests().push_back({#name, test_##name}); } \
	} g_reg_##name; } \
	static void test_##name()

#define ASSERT_TRUE(expr) do { \
	g_total++; \
	if(!(expr)) { \
		fprintf(stderr, "  FAIL: %s:%d: ASSERT_TRUE(%s)\n", __FILE__, __LINE__, #expr); \
		g_failed++; \
	} else { g_passed++; } \
} while(0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b) do { \
	g_total++; \
	auto _a = (a); auto _b = (b); \
	if(_a != _b) { \
		fprintf(stderr, "  FAIL: %s:%d: ASSERT_EQ(%s, %s)\n", __FILE__, __LINE__, #a, #b); \
		g_failed++; \
	} else { g_passed++; } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
	g_total++; \
	std::string _a = (a); std::string _b = (b); \
	if(_a != _b) { \
		fprintf(stderr, "  FAIL: %s:%d: ASSERT_STR_EQ(\"%s\", \"%s\")\n", \
			__FILE__, __LINE__, _a.c_str(), _b.c_str()); \
		g_failed++; \
	} else { g_passed++; } \
} while(0)
