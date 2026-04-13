#include "test_framework.h"
#include "../include/core/lockfree_queue.h"
#include "../include/predict/predictor.h"
#include "../include/cache/lru_cache.h"
#include <thread>
#include <vector>

using namespace hp;

// === LockFreeQueue Tests ===
TestResult test_queue_push_pop() {
    LockFreeQueue<int, 16> q;
    TEST_ASSERT(q.try_push(42), "push failed");
    auto v = q.try_pop();
    TEST_ASSERT(v.has_value(), "pop returned empty");
    TEST_ASSERT(*v == 42, "value mismatch");
    return {true, __func__, nullptr};
}

TestResult test_queue_full() {
    LockFreeQueue<int, 4> q;
    for(int i = 0; i < 4; ++i) TEST_ASSERT(q.try_push(i), "push failed");
    TEST_ASSERT(!q.try_push(5), "should be full");
    return {true, __func__, nullptr};
}

TestResult test_queue_concurrent() {
    LockFreeQueue<int, 256> q;
    std::vector<std::thread> producers, consumers;
    int produced = 0, consumed = 0;
    
    for(int i = 0; i < 4; ++i) {
        producers.emplace_back([&]() {
            for(int j = 0; j < 100; ++j) {
                if(q.try_push(j)) produced++;
            }
        });
        consumers.emplace_back([&]() {
            for(int j = 0; j < 100; ++j) {
                if(q.try_pop()) consumed++;
            }
        });
    }
    for(auto& t : producers) t.join();
    for(auto& t : consumers) t.join();
    
    TEST_ASSERT(produced > 0 && consumed > 0, "no data transferred");
    return {true, __func__, nullptr};
}

// === FTRL Predictor Tests ===
TestResult test_ftrl_predict_range() {
    predict::FTRL f;
    std::array<float, 10> x = {0.5f, 0.3f, 0.2f, 0.8f, 0.1f, 0.6f, 0.4f, 0.7f, 0.2f, 0.5f};
    float p = f.predict(x);
    TEST_ASSERT(p >= 0.0f && p <= 1.0f, "predict out of range");
    return {true, __func__, nullptr};
}

TestResult test_ftrl_update() {
    predict::FTRL f;
    std::array<float, 10> x = {0.5f, 0.3f, 0.2f, 0.8f, 0.1f, 0.6f, 0.4f, 0.7f, 0.2f, 0.5f};
    float p1 = f.predict(x);
    f.update(x, true);
    float p2 = f.predict(x);
    TEST_ASSERT(p1 != p2, "model should change after update");
    return {true, __func__, nullptr};
}

// === LRU Cache Tests ===
TestResult test_lru_basic() {
    cache::LRUCache<4> c(16);
    cache::Key k{"pkg", "scene"};
    TEST_ASSERT(!c.get(k).has_value(), "should be empty initially");
    
    FreqConfig cfg;
    cfg.target_freq = 2000000;
    c.put(k, cfg);
    
    auto v = c.get(k);
    TEST_ASSERT(v.has_value(), "should have value after put");
    TEST_ASSERT(v->target_freq == 2000000, "value mismatch");
    return {true, __func__, nullptr};
}

int main() {
    printf("=== HyperPredict Unit Tests ===\n\n");
    
    RUN_TEST(test_queue_push_pop);
    RUN_TEST(test_queue_full);
    RUN_TEST(test_queue_concurrent);
    RUN_TEST(test_ftrl_predict_range);
    RUN_TEST(test_ftrl_update);
    RUN_TEST(test_lru_basic);
    
    hp::test::TestRunner::summary();
    return 0;
}