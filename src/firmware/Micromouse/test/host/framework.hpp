// Minimal header-only test framework for the Micromouse host tests.
//
// No external dependencies: just include this header, declare tests with
// TEST_CASE(name), assert with CHECK*/REQUIRE*, and end the file with
// TEST_MAIN(). Tests self-register at static-init time and run in order.
#pragma once

#include <cstdio>
#include <cmath>
#include <string>
#include <type_traits>
#include <vector>

namespace tf {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
inline int& fail_count() {
    static int f = 0;
    return f;
}
inline int& check_count() {
    static int c = 0;
    return c;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

// Best-effort stringification of a comparable value for failure messages.
template <class T>
inline std::string stringify(const T& v) {
    if constexpr (std::is_same_v<T, bool>) {
        return v ? "true" : "false";
    } else if constexpr (std::is_enum_v<T>) {
        return std::to_string(static_cast<long long>(v));
    } else if constexpr (std::is_integral_v<T>) {
        return std::to_string(static_cast<long long>(v));
    } else if constexpr (std::is_floating_point_v<T>) {
        return std::to_string(static_cast<double>(v));
    } else if constexpr (std::is_convertible_v<T, std::string>) {
        return std::string(v);
    } else {
        return "<?>";
    }
}

inline void report_fail(const char* file, int line, const std::string& msg) {
    fail_count()++;
    std::printf("    [FAIL] %s:%d  %s\n", file, line, msg.c_str());
}

inline int run_all() {
    int failed_tests = 0;
    for (auto& tc : registry()) {
        const int before = fail_count();
        std::printf("[ RUN  ] %s\n", tc.name);
        tc.fn();
        if (fail_count() == before) {
            std::printf("[  OK  ] %s\n", tc.name);
        } else {
            std::printf("[ FAIL ] %s\n", tc.name);
            failed_tests++;
        }
    }
    std::printf("\n=========================================\n");
    std::printf(" %zu test(s), %d checks, %d failed test(s)\n",
                registry().size(), check_count(), failed_tests);
    std::printf("=========================================\n");
    return failed_tests == 0 ? 0 : 1;
}

}  // namespace tf

#define TEST_CASE(name)                                          \
    static void name();                                          \
    static ::tf::Registrar tf_reg_##name(#name, &name);          \
    static void name()

#define CHECK(cond)                                                          \
    do {                                                                     \
        ::tf::check_count()++;                                              \
        if (!(cond)) {                                                       \
            ::tf::report_fail(__FILE__, __LINE__, "CHECK failed: " #cond);  \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                          \
    do {                                                                       \
        ::tf::check_count()++;                                                 \
        auto tf_va = (a);                                                       \
        auto tf_vb = (b);                                                       \
        if (!(tf_va == tf_vb)) {                                                \
            ::tf::report_fail(__FILE__, __LINE__,                              \
                              std::string("CHECK_EQ failed: " #a " == " #b     \
                                          "  (got ") +                         \
                                  ::tf::stringify(tf_va) + " vs " +            \
                                  ::tf::stringify(tf_vb) + ")");               \
        }                                                                      \
    } while (0)

#define CHECK_NE(a, b)                                                          \
    do {                                                                       \
        ::tf::check_count()++;                                                 \
        auto tf_va = (a);                                                       \
        auto tf_vb = (b);                                                       \
        if (!(tf_va != tf_vb)) {                                                \
            ::tf::report_fail(__FILE__, __LINE__,                              \
                              "CHECK_NE failed: " #a " != " #b);               \
        }                                                                      \
    } while (0)

#define CHECK_FLOAT_EQ(a, b, eps)                                               \
    do {                                                                       \
        ::tf::check_count()++;                                                 \
        const double tf_da = (double)(a);                                       \
        const double tf_db = (double)(b);                                       \
        if (std::fabs(tf_da - tf_db) > (eps)) {                                 \
            ::tf::report_fail(__FILE__, __LINE__,                              \
                              std::string("CHECK_FLOAT_EQ failed: " #a         \
                                          " ~= " #b "  (got ") +               \
                                  ::tf::stringify(tf_da) + " vs " +            \
                                  ::tf::stringify(tf_db) + ")");               \
        }                                                                      \
    } while (0)

// Like CHECK but aborts the current test on failure (for preconditions).
#define REQUIRE(cond)                                                        \
    do {                                                                     \
        ::tf::check_count()++;                                              \
        if (!(cond)) {                                                       \
            ::tf::report_fail(__FILE__, __LINE__, "REQUIRE failed: " #cond); \
            return;                                                          \
        }                                                                    \
    } while (0)

#define TEST_MAIN()                 \
    int main() { return ::tf::run_all(); }
