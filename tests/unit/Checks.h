#pragma once

#include <cmath>
#include <cstdio>

namespace regression
{

// Shared across test files. Checks stay active in release builds too.
inline int checks = 0;
inline int failures = 0;

inline void check(bool passed, const char* expression, const char* file, int line)
{
    ++checks;
    if (passed) return;
    ++failures;
    std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
}

inline bool near(float actual, float expected, float epsilon = 0.0001f)
{
    return std::abs(actual - expected) < epsilon;
}

}

#define CHECK(...) regression::check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__, __FILE__, __LINE__)
