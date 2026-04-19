#pragma once
/**
 * @file task_decomposer.h
 * @brief 细粒度任务分解器
 *
 * 设计原则：
 * 1. 细粒度 - 将大任务分解为小任务
 * 2. 负载均衡 - 均匀分配任务到线程
 * 3. 低开销 - 最小化分解和同步开销
 * 4. 可配置 - 支持不同的分解策略
 */

#include <vector>
#include <functional>
#include <atomic>
#include <memory>
#include <algorithm>
#include "load_aware_pool.h"

namespace hp::parallel {

// =============================================================================
// 任务分解策略
// =============================================================================

enum class DecomposeStrategy {
    EQUAL,      // 均匀分解
    DYNAMIC,    // 动态分解（工作窃取）
    GUIDED,     // 引导分解（递减块大小）
    AUTO        // 自动选择
};

// =============================================================================
// 任务分解配置
// =============================================================================

struct DecomposeConfig {
    DecomposeStrategy strategy{DecomposeStrategy::AUTO};
    size_t min_chunk_size{1};      // 最小块大小
    size_t max_chunk_size{64};     // 最大块大小
    size_t target_chunks{4};       // 目标块数
    bool enable_stealing{true};    // 是否启用工作窃取
};

// =============================================================================
// 细粒度任务分解器
// =============================================================================

class TaskDecomposer {
private:
    DecomposeConfig config_;
    LoadAwarePool& pool_;

    // 工作窃取队列
    struct WorkStealingQueue {
        std::vector<std::function<void()>> tasks_;
        std::atomic<size_t> head_{0};
        std::atomic<size_t> tail_{0};
        std::mutex mutex_;

        void push(std::function<void()> task) {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push_back(std::move(task));
            tail_.fetch_add(1);
        }

        bool pop(std::function<void()>& task) {
            size_t t = tail_.load();
            size_t h = head_.load();

            if (h >= t) return false;

            std::lock_guard<std::mutex> lock(mutex_);
            h = head_.load();
            t = tail_.load();

            if (h >= t) return false;

            task = std::move(tasks_[h]);
            head_.fetch_add(1);
            return true;
        }

        bool steal(std::function<void()>& task) {
            size_t t = tail_.load();
            size_t h = head_.load();

            if (h >= t) return false;

            std::lock_guard<std::mutex> lock(mutex_);
            h = head_.load();
            t = tail_.load();

            if (h >= t) return false;

            // 从尾部窃取
            task = std::move(tasks_[t - 1]);
            tail_.fetch_sub(1);
            return true;
        }

        size_t size() const {
            return tail_.load() - head_.load();
        }
    };

    std::vector<std::unique_ptr<WorkStealingQueue>> queues_;

public:
    explicit TaskDecomposer(const DecomposeConfig& config = DecomposeConfig{},
                          LoadAwarePool& pool = global_load_aware_pool())
        : config_(config), pool_(pool) {
        // 初始化工作窃取队列
        size_t num_queues = pool.total_threads();
        queues_.resize(num_queues);
        for (size_t i = 0; i < num_queues; ++i) {
            queues_[i] = std::make_unique<WorkStealingQueue>();
        }
    }

    // 并行for循环（细粒度）
    template<typename Func>
    void parallel_for(size_t start, size_t end, Func&& func) {
        if (end <= start) return;

        size_t total_size = end - start;
        size_t num_threads = pool_.active_threads();

        // 自动选择策略
        DecomposeStrategy strategy = config_.strategy;
        if (strategy == DecomposeStrategy::AUTO) {
            if (total_size < 100) {
                strategy = DecomposeStrategy::EQUAL;
            } else if (total_size < 1000) {
                strategy = DecomposeStrategy::GUIDED;
            } else {
                strategy = DecomposeStrategy::DYNAMIC;
            }
        }

        switch (strategy) {
            case DecomposeStrategy::EQUAL:
                parallel_for_equal(start, end, std::forward<Func>(func));
                break;
            case DecomposeStrategy::DYNAMIC:
                parallel_for_dynamic(start, end, std::forward<Func>(func));
                break;
            case DecomposeStrategy::GUIDED:
                parallel_for_guided(start, end, std::forward<Func>(func));
                break;
            default:
                parallel_for_equal(start, end, std::forward<Func>(func));
        }
    }

    // 均匀分解
    template<typename Func>
    void parallel_for_equal(size_t start, size_t end, Func&& func) {
        size_t total_size = end - start;
        size_t num_threads = pool_.active_threads();
        size_t chunk_size = std::max(config_.min_chunk_size,
                                    total_size / num_threads);

        std::vector<std::future<void>> futures;
        futures.reserve(num_threads);

        for (size_t i = 0; i < num_threads; ++i) {
            size_t chunk_start = start + i * chunk_size;
            size_t chunk_end = std::min(chunk_start + chunk_size, end);

            if (chunk_start >= end) break;

            futures.push_back(pool_.enqueue([chunk_start, chunk_end, &func]() {
                for (size_t j = chunk_start; j < chunk_end; ++j) {
                    func(j);
                }
            }, TaskPriority::NORMAL));
        }

        for (auto& future : futures) {
            future.wait();
        }
    }

    // 动态分解（工作窃取）
    template<typename Func>
    void parallel_for_dynamic(size_t start, size_t end, Func&& func) {
        size_t total_size = end - start;
        size_t chunk_size = std::min(config_.max_chunk_size,
                                    std::max(config_.min_chunk_size,
                                            total_size / (pool_.active_threads() * 4)));

        std::atomic<size_t> current{start};
        std::atomic<bool> done{false};

        auto worker = [&current, end, chunk_size, &func, &done]() {
            while (!done.load()) {
                size_t my_start = current.fetch_add(chunk_size);
                if (my_start >= end) {
                    done.store(true);
                    break;
                }

                size_t my_end = std::min(my_start + chunk_size, end);
                for (size_t i = my_start; i < my_end; ++i) {
                    func(i);
                }
            }
        };

        std::vector<std::future<void>> futures;
        size_t num_threads = pool_.active_threads();
        futures.reserve(num_threads);

        for (size_t i = 0; i < num_threads; ++i) {
            futures.push_back(pool_.enqueue(worker, TaskPriority::NORMAL));
        }

        for (auto& future : futures) {
            future.wait();
        }
    }

    // 引导分解（递减块大小）
    template<typename Func>
    void parallel_for_guided(size_t start, size_t end, Func&& func) {
        size_t total_size = end - start;
        size_t num_threads = pool_.active_threads();
        size_t initial_chunk = std::min(config_.max_chunk_size,
                                      total_size / num_threads);

        std::atomic<size_t> current{start};
        std::atomic<size_t> chunk{initial_chunk};

        auto worker = [&current, &chunk, end, &func, total_size, num_threads]() {
            while (true) {
                size_t my_chunk = chunk.load();
                size_t my_start = current.fetch_add(my_chunk);

                if (my_start >= end) break;

                size_t my_end = std::min(my_start + my_chunk, end);
                for (size_t i = my_start; i < my_end; ++i) {
                    func(i);
                }

                // 递减块大小
                size_t remaining = end - current.load();
                size_t new_chunk = std::max(config_.min_chunk_size,
                                          remaining / num_threads);
                chunk.store(new_chunk);
            }
        };

        std::vector<std::future<void>> futures;
        futures.reserve(num_threads);

        for (size_t i = 0; i < num_threads; ++i) {
            futures.push_back(pool_.enqueue(worker, TaskPriority::NORMAL));
        }

        for (auto& future : futures) {
            future.wait();
        }
    }

    // 并行reduce
    template<typename T, typename Func, typename ReduceFunc>
    T parallel_reduce(size_t start, size_t end, T init, Func&& func, ReduceFunc&& reduce) {
        if (end <= start) return init;

        size_t total_size = end - start;
        size_t num_threads = pool_.active_threads();
        size_t chunk_size = std::max(config_.min_chunk_size,
                                    total_size / num_threads);

        std::vector<std::future<T>> futures;
        futures.reserve(num_threads);

        for (size_t i = 0; i < num_threads; ++i) {
            size_t chunk_start = start + i * chunk_size;
            size_t chunk_end = std::min(chunk_start + chunk_size, end);

            if (chunk_start >= end) break;

            futures.push_back(pool_.enqueue([chunk_start, chunk_end, &func, init]() {
                T result = init;
                for (size_t j = chunk_start; j < chunk_end; ++j) {
                    result = func(result, j);
                }
                return result;
            }, TaskPriority::NORMAL));
        }

        // 合并结果
        T final_result = init;
        for (auto& future : futures) {
            final_result = reduce(final_result, future.get());
        }

        return final_result;
    }

    // 并行map
    template<typename T, typename Func>
    std::vector<T> parallel_map(size_t start, size_t end, Func&& func) {
        if (end <= start) return {};

        size_t total_size = end - start;
        std::vector<T> results(total_size);

        parallel_for(start, end, [&results, &func](size_t i) {
            results[i] = func(i);
        });

        return results;
    }

    // 获取配置
    const DecomposeConfig& config() const { return config_; }
    void set_config(const DecomposeConfig& config) { config_ = config; }
};

// =============================================================================
// 全局任务分解器
// =============================================================================

inline TaskDecomposer& global_task_decomposer() {
    static DecomposeConfig config{
        .strategy = DecomposeStrategy::AUTO,
        .min_chunk_size = 1,
        .max_chunk_size = 64,
        .target_chunks = 4,
        .enable_stealing = true
    };
    static TaskDecomposer decomposer(config, global_load_aware_pool());
    return decomposer;
}

} // namespace hp::parallel
