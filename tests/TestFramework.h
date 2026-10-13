#pragma once

// Intentionally hand-rolled rather than pulling in GoogleTest/Catch2: at
// Phase 1 scope (two small modules, a handful of behavioral cases) a real
// framework is an unnecessary dependency. Revisit if test volume grows
// enough that registration/fixtures/parameterization become worth the cost.

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace voipmodem::test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> registry;
    return registry;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        Registry().push_back(TestCase{name, std::move(fn)});
    }
};

inline int& FailureCount() {
    static int failures = 0;
    return failures;
}

template <typename A, typename B>
void ExpectEq(const A& a, const B& b, const char* aExpr, const char* bExpr, const char* file, int line) {
    if (!(a == b)) {
        std::cerr << file << ":" << line << " FAIL: expected (" << aExpr << ") == (" << bExpr << ")\n";
        ++FailureCount();
    }
}

inline void ExpectTrue(bool value, const char* expr, const char* file, int line) {
    if (!value) {
        std::cerr << file << ":" << line << " FAIL: expected (" << expr << ") to be true\n";
        ++FailureCount();
    }
}

inline int RunAll() {
    int total = 0;
    for (const TestCase& test : Registry()) {
        const int before = FailureCount();
        test.fn();
        ++total;
        if (FailureCount() == before) {
            std::cout << "[PASS] " << test.name << "\n";
        } else {
            std::cout << "[FAIL] " << test.name << "\n";
        }
    }
    std::cout << total << " test case(s) run, " << FailureCount() << " assertion failure(s)\n";
    return FailureCount() == 0 ? 0 : 1;
}

} // namespace voipmodem::test

#define TEST(name)                                                                  \
    static void name();                                                            \
    static ::voipmodem::test::Registrar registrar_##name(#name, name);             \
    static void name()

#define EXPECT_EQ(a, b) ::voipmodem::test::ExpectEq((a), (b), #a, #b, __FILE__, __LINE__)
#define EXPECT_TRUE(a) ::voipmodem::test::ExpectTrue((a), #a, __FILE__, __LINE__)
