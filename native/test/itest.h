#pragma once

// Minimal host test harness.
//
// No third-party framework: the [H] suite has to build on any machine with a
// C++17 compiler and nothing else, and it needs no more than this.
//
//   ITEST(name) { ... }        define and register a test case
//   ITEST_TRUE(expr)           fail the case if expr is false
//   ITEST_EQ(a, b)             fail the case if a != b (integer values)
//   ITEST_MAIN("suite")        one per test executable
//
// A failing check does not abort its case; the case is reported failed at the
// end. Only the first few failures per case are printed, so an exhaustive loop
// that goes wrong does not flood the log.

#include <cstdio>
#include <vector>

namespace itest {

using Fn = void (*)();

struct Case {
    const char* name;
    Fn          fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct State {
    int failures_in_case = 0;
};

inline State& state() {
    static State s;
    return s;
}

struct Registrar {
    Registrar(const char* name, Fn fn) { registry().push_back({name, fn}); }
};

constexpr int kMaxPrintedPerCase = 10;

inline void fail(const char* file, int line, const char* expr) {
    if (state().failures_in_case++ < kMaxPrintedPerCase) {
        std::printf("  %s(%d): check failed: %s\n", file, line, expr);
    }
}

inline void fail_eq(const char* file, int line, const char* a, const char* b,
                    unsigned long long va, unsigned long long vb) {
    if (state().failures_in_case++ < kMaxPrintedPerCase) {
        std::printf("  %s(%d): %s == %s failed: 0x%llX vs 0x%llX\n",
                    file, line, a, b, va, vb);
    }
}

inline int run_all(const char* suite) {
    int failed_cases = 0;
    for (const Case& c : registry()) {
        state().failures_in_case = 0;
        c.fn();
        if (state().failures_in_case != 0) {
            ++failed_cases;
            std::printf("[FAIL] %s (%d failed checks)\n", c.name, state().failures_in_case);
        } else {
            std::printf("[ OK ] %s\n", c.name);
        }
    }
    std::printf("%s: %zu cases, %d failed\n", suite, registry().size(), failed_cases);
    return failed_cases == 0 ? 0 : 1;
}

}  // namespace itest

#define ITEST(name)                                                   \
    static void name();                                               \
    static const ::itest::Registrar name##_registrar(#name, &name);   \
    static void name()

#define ITEST_TRUE(expr)                                              \
    do {                                                              \
        if (!(expr)) ::itest::fail(__FILE__, __LINE__, #expr);        \
    } while (0)

#define ITEST_EQ(a, b)                                                \
    do {                                                              \
        const unsigned long long itest_a_ = static_cast<unsigned long long>(a); \
        const unsigned long long itest_b_ = static_cast<unsigned long long>(b); \
        if (itest_a_ != itest_b_)                                     \
            ::itest::fail_eq(__FILE__, __LINE__, #a, #b, itest_a_, itest_b_); \
    } while (0)

#define ITEST_MAIN(suite)                                             \
    int main() { return ::itest::run_all(suite); }
