#pragma once
/**
 * @file load_aware_pool.h
 * @brief 负载感知的动态线程池
 *
 * 设计原则：
 * 1. 低开销 - 最小化线程创建和同步开销
 * 2. 负载感知 - 根据系统负载动态调整线程数
 * 3. 大小核感知 - 将计算任务分配到合适的核心
 * 4. 细粒度任务 - 支持小任务的高效调度
 */

#include <thread>
#include <atomic>
#include <functional>
#include <future>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <sched.h>
#include <unistd.h>

namespace hp::parallel {

// =============================================================================
// 负载感知配置
// =============================================================================

struct LoadAwareConfig {
    size_t min_threads{1};           // 最小线程数
    size_t max_threads{4};           // 最大线程数（移动端限制）
    size_t idle_threshold_ms{100};   // 空闲阈值（ms）
    size_t busy_threshold_ms{10};    // 忙碌阈值（ms）
    float cpu_util_threshold{0.5f};  // CPU利用率阈值
    bool enable_big_core{true};     // 是否使用大核
};

// =============================================================================
// 任务优先级
// =============================================================================

enum class TaskPriority {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    URGENT = 3
};

// =============================================================================
// 负载感知的线程池
// =============================================================================

class LoadAwarePool {
private:
    // 任务包装器
    struct Task {
        std::function<void()> func;
        TaskPriority priority{TaskPriority::NORMAL};
        uint64_t enqueue_time{0};

        Task() = default;
        Task(std::function<void()> f, TaskPriority p)
            : func(std::move(f)), priority(p), enqueue_time(get_time_ns()) {}

        static uint64_t get_time_ns() {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
                   static_cast<uint64_t>(ts.tv_nsec);
        }

        bool operator<(const Task& other) const {
            // 高优先级先执行
            if (priority != other.priority) {
                return static_cast<int>(priority) < static_cast<int>(other.priority);
            }
            // 同优先级按时间排序
            return enqueue_time < other.enqueue_time;
        }
    };

    // 线程工作器
    struct Worker {
        std::thread thread;
        std::atomic<bool> active{true};
        std::atomic<uint64_t> last_active_time{0};
        size_t core_id{0};  // 绑定的核心ID

        Worker() = default;
        Worker(Worker&& other) noexcept
            : thread(std::move(other.thread)),
              active(other.active.load()),
              last_active_time(other.last_active_time.load()),
              core_id(other.core_id) {}
    };

    std::vector<Worker> workers_;
    std::priority_queue<Task, std::vector<Task>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> pending_tasks_{0};

    LoadAwareConfig config_;
    std::atomic<size_t> active_threads_{0};

    // 系统负载监控
    struct SystemLoad {
        std::atomic<float> cpu_util{0.0f};
        std::atomic<uint64_t> last_update{0};
        std::atomic<uint32_t> sample_count{0};

        void update(float util) {
            cpu_util.store(util);
            last_update.store(Task::get_time_ns());
            sample_count.fetch_add(1);
        }

        float get() const {
            return cpu_util.load();
        }

        bool is_stale(uint64_t max_age_ns = 1000000000ULL) const {
            uint64_t now = Task::get_time_ns();
            return (now - last_update.load()) > max_age_ns;
        }
    } system_load_;

    // 绑定线程到指定核心
    void bind_to_core(std::thread& t, int core_id) {
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(core_id, &mask);
        pthread_setaffinity_np(t.native_handle(), sizeof(mask), &mask);
    }

    // 获取核心类型（大核/小核）
    bool is_big_core(int core_id) {
        // 简化判断：假设核心4-7是大核
        return core_id >= 4;
    }

    // 设置线程优先级
    void set_thread_priority(std::thread& t, bool high_priority) {
#if defined(__ANDROID__) || defined(ANDROID)
        // Android: 设置线程优先级
        int nice_val = high_priority ? -5 : 10;
        setpriority(PRIO_PROCESS, gettid(), nice_val);
#endif
        (void)t;
        (void)high_priority;
    }

    // 工作线程主循环
    void worker_loop(size_t worker_id) {
        // 设置线程名称
#if defined(__linux__)
        char name[16];
        snprintf(name, sizeof(name), "HP-Worker-%zu", worker_id);
        pthread_setname_np(pthread_self(), name);
#endif

        // 绑定到核心（如果配置启用）
        if (config_.enable_big_core && worker_id < workers_.size()) {
            int core_id = static_cast<int>(worker_id);
            if (is_big_core(core_id)) {
                bind_to_core(workers_[worker_id].thread, core_id);
            }
        }

        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                condition_.wait(lock, [this] {
                    return stop_.load() || !tasks_.empty();
                });

                if (stop_.load() && tasks_.empty()) {
                    return;
                }

                if (!tasks_.empty()) {
                    task = std::move(const_cast<Task&>(tasks_.top()));
                    tasks_.pop();
                }
            }

            if (task.func) {
                workers_[worker_id].last_active_time.store(Task::get_time_ns());
                task.func();
                pending_tasks_--;
            }
        }
    }

    // 动态调整线程数
    void adjust_thread_count() {
        size_t current = workers_.size();
        float cpu_util = system_load_.get();

        // 如果CPU利用率高，减少线程数
        if (cpu_util > config_.cpu_util_threshold && current > config_.min_threads) {
            shrink_pool();
        }
        // 如果CPU利用率低，增加线程数
        else if (cpu_util < config_.cpu_util_threshold * 0.5f &&
                 current < config_.max_threads) {
            grow_pool();
        }
    }

    // 扩大线程池
    void grow_pool() {
        size_t new_size = std::min(workers_.size() + 1, config_.max_threads);
        if (new_size <= workers_.size()) return;

        for (size_t i = workers_.size(); i < new_size; ++i) {
            workers_.emplace_back();
            workers_[i].active = true;
            workers_[i].core_id = i;
            workers_[i].thread = std::thread([this, i]() {
                worker_loop(i);
            });
        }

        active_threads_.store(new_size);
    }

    // 缩小线程池
    void shrink_pool() {
        if (workers_.size() <= config_.min_threads) return;

        // 标记最后一个线程为非活跃
        size_t last = workers_.size() - 1;
        workers_[last].active.store(false);

        // 等待线程结束
        if (workers_[last].thread.joinable()) {
            workers_[last].thread.join();
        }

        workers_.pop_back();
        active_threads_.store(workers_.size());
    }

public:
    explicit LoadAwarePool(const LoadAwareConfig& config = LoadAwareConfig{})
        : config_(config) {
        // 初始化最小线程数
        for (size_t i = 0; i < config_.min_threads; ++i) {
            workers_.emplace_back();
            workers_[i].active = true;
            workers_[i].core_id = i;
            workers_[i].thread = std::thread([this, i]() {
                worker_loop(i);
            });
        }
        active_threads_.store(config_.min_threads);
    }

    ~LoadAwarePool() {
        stop_.store(true);
        condition_.notify_all();

        for (auto& worker : workers_) {
            if (worker.thread.joinable()) {
                worker.thread.join();
            }
        }
    }

    // 更新系统负载
    void update_system_load(float cpu_util) {
        system_load_.update(cpu_util);
        adjust_thread_count();
    }

    // 提交任务（带优先级）
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args, TaskPriority priority = TaskPriority::NORMAL)
        -> std::future<std::result_of_t<F(Args...)>> {
        using return_type = std::result_of_t<F(Args...)>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (stop_.load()) {
                return result;
            }

            tasks_.emplace([task]() { (*task)(); }, priority);
            pending_tasks_++;
        }

        condition_.notify_one();
        return result;
    }

    // 批量提交任务
    template<typename F>
    void enqueue_batch(const std::vector<F>& tasks, TaskPriority priority = TaskPriority::NORMAL) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (stop_.load()) return;

            for (const auto& task : tasks) {
                tasks_.emplace(task, priority);
                pending_tasks_++;
            }
        }

        // 通知所有工作线程
        condition_.notify_all();
    }

    // 并行for循环
    template<typename Func>
    void parallel_for(size_t start, size_t end, size_t chunk_size, Func&& func) {
        if (end <= start) return;

        size_t total_size = end - start;
        size_t actual_chunk = std::max(chunk_size, size_t(1));
        size_t num_chunks = (total_size + actual_chunk - 1) / actual_chunk;

        // 如果任务数很少，直接串行执行
        if (num_chunks <= 2) {
            for (size_t i = 0; i < num_chunks; i++) {
                size_t chunk_start = start + i * actual_chunk;
                size_t chunk_end = std::min(chunk_start + actual_chunk, end);
                func(chunk_start, chunk_end);
            }
            return;
        }

        // 并行执行
        std::vector<std::future<void>> futures;
        futures.reserve(num_chunks);

        for (size_t i = 0; i < num_chunks; i++) {
            size_t chunk_start = start + i * actual_chunk;
            size_t chunk_end = std::min(chunk_start + actual_chunk, end);

            futures.push_back(enqueue([chunk_start, chunk_end, &func]() {
                func(chunk_start, chunk_end);
            }, TaskPriority::NORMAL));
        }

        // 等待所有任务完成
        for (auto& future : futures) {
            future.wait();
        }
    }

    // 等待所有任务完成
    void wait_all() {
        while (pending_tasks_.load() > 0) {
            std::this_thread::yield();
        }
    }

    // 获取统计信息
    size_t pending() const { return pending_tasks_.load(); }
    size_t active_threads() const { return active_threads_.load(); }
    size_t total_threads() const { return workers_.size(); }
    float system_cpu_util() const { return system_load_.get(); }
    bool is_stopped() const { return stop_.load(); }
};

// =============================================================================
// 全局负载感知线程池
// =============================================================================

inline LoadAwarePool& global_load_aware_pool() {
    static LoadAwareConfig config{
        .min_threads = 1,
        .max_threads = 4,
        .idle_threshold_ms = 100,
        .busy_threshold_ms = 10,
        .cpu_util_threshold = 0.5f,
        .enable_big_core = true
    };
    static LoadAwarePool pool(config);
    return pool;
}

} // namespace hp::parallel
