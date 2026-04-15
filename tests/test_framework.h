#pragma once
#include <cstdint>
#include <cstring>

namespace hp::test {

struct TestResult {
    bool passed;
    const char* name;
    const char* error;
};

class TestRunner {
    static int passed_;
    static int failed_;
public:
    static void run(TestResult (*test_func)());
    static void summary();
};

#define TEST_ASSERT(cond, msg) \
    if(!(cond)) return {false, __func__, msg}

#define RUN_TEST(test_func) \
    hp::test::TestRunner::run(test_func)

} // namespace hp::test