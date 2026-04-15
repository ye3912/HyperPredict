#include "test_framework.h"
#include <cstdio>

namespace hp::test {
int TestRunner::passed_ = 0;
int TestRunner::failed_ = 0;

void TestRunner::run(TestResult (*test_func)()) {
    TestResult r = test_func();
    if(r.passed) {
        printf("[PASS] %s\n", r.name);
        passed_++;
    } else {
        printf("[FAIL] %s: %s\n", r.name, r.error);
        failed_++;
    }
}

void TestRunner::summary() {
    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed_);
    printf("Failed: %d\n", failed_);
    printf("Total:  %d\n", passed_ + failed_);
}
} // namespace hp::test