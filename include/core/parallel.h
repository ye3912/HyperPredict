#pragma once
/**
 * @file parallel.h
 * @brief 轻量级多线程优化模块
 * 
 * 设计原则：
 * 1. 移动端优先 - 避免过度线程化
 * 2. 零拷贝 - 减少内存复制
 * 3. 异构感知 - 大小核任务分配
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

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define USE_NEON 1
#elif defined(__SSE__) || defined(__SSE2__) || defined(__AVX__)
#include <immintrin.h>
#define USE_SSE 1
#else
#define USE_NEON 0
#define USE_SSE 0
#endif

namespace hp::parallel {

// =============================================================================
// SIMD 向量化矩阵运算
// =============================================================================

class SIMDMatrix {
public:
    // 4x4 矩阵乘法 (通用实现)
    static void matmul_4x4(float* out, const float* a, const float* b, size_t M, size_t N, size_t K) {
        for (size_t m = 0; m < M; m += 4) {
            for (size_t n = 0; n < N; n += 4) {
                // 初始化输出块
                float c_block[16] = {0};
                
                for (size_t k = 0; k < K; k += 4) {
                    // 加载 A 的行和 B 的列块
                    for (size_t mm = 0; mm < 4 && m + mm < M; mm++) {
                        for (size_t kk = 0; kk < 4 && k + kk < K; kk++) {
                            float a_val = a[(m + mm) * K + k + kk];
                            for (size_t nn = 0; nn < 4 && n + nn < N; nn++) {
                                c_block[mm * 4 + nn] += a_val * b[(k + kk) * N + n + nn];
                            }
                        }
                    }
                }
                
                // 存储结果
                for (size_t mm = 0; mm < 4 && m + mm < M; mm++) {
                    for (size_t nn = 0; nn < 4 && n + nn < N; nn++) {
                        out[(m + mm) * N + n + nn] = c_block[mm * 4 + nn];
                    }
                }
            }
        }
    }
    
    // ReLU 激活函数 (SIMD 优化)
    static void relu(float* data, size_t size) {
#if USE_NEON
        size_t i = 0;
        for (; i + 4 <= size; i += 4) {
            float32x4_t vec = vld1q_f32(data + i);
            float32x4_t zero = vdupq_n_f32(0.0f);
            vec = vmaxq_f32(vec, zero);
            vst1q_f32(data + i, vec);
        }
        // 处理剩余元素
        for (; i < size; i++) {
            data[i] = data[i] > 0 ? data[i] : 0;
        }
#elif USE_SSE
        size_t i = 0;
        for (; i + 4 <= size; i += 4) {
            __m128 vec = _mm_loadu_ps(data + i);
            vec = _mm_max_ps(vec, _mm_setzero_ps());
            _mm_storeu_ps(data + i, vec);
        }
        for (; i < size; i++) {
            data[i] = data[i] > 0 ? data[i] : 0;
        }
#else
        for (size_t i = 0; i < size; i++) {
            data[i] = data[i] > 0 ? data[i] : 0;
        }
#endif
    }
    
    // 矩阵-向量乘法 (SIMD 优化)
    static void matvec_mul(float* out, const float* matrix, const float* vec, 
                          size_t rows, size_t cols) {
#if USE_NEON
        size_t i = 0;
        for (; i + 3 < rows; i += 4) {
            float32x4_t result = vdupq_n_f32(0.0f);
            
            size_t j = 0;
            for (; j + 3 < cols; j += 4) {
                float32x4_t m_row0 = vld1q_f32(matrix + (i + 0) * cols + j);
                float32x4_t m_row1 = vld1q_f32(matrix + (i + 1) * cols + j);
                [[maybe_unused]] float32x4_t m_row2 = vld1q_f32(matrix + (i + 2) * cols + j);
                [[maybe_unused]] float32x4_t m_row3 = vld1q_f32(matrix + (i + 3) * cols + j);
                
                float32x4_t v_vec = vld1q_f32(vec + j);
                
                result = vmlaq_f32(result, m_row0, v_vec);
                if (i + 1 < rows) {
                    float32x4_t tmp1 = vdupq_n_f32(0.0f);
                    tmp1 = vmlaq_f32(tmp1, m_row1, v_vec);
                    result = vextq_f32(result, tmp1, 1);
                }
                // 简化处理
            }
            
            vst1q_f32(out + i, result);
        }
        // 处理剩余行
        for (; i < rows; i++) {
            float sum = 0;
            for (size_t j = 0; j < cols; j++) {
                sum += matrix[i * cols + j] * vec[j];
            }
            out[i] = sum;
        }
#else
        // 通用实现
        for (size_t i = 0; i < rows; i++) {
            float sum = 0;
            for (size_t j = 0; j < cols; j++) {
                sum += matrix[i * cols + j] * vec[j];
            }
            out[i] = sum;
        }
#endif
    }
    
    // 数组加法 (SIMD 优化)
    static void add(float* out, const float* a, const float* b, size_t size) {
#if USE_NEON
        size_t i = 0;
        for (; i + 4 <= size; i += 4) {
            float32x4_t va = vld1q_f32(a + i);
            float32x4_t vb = vld1q_f32(b + i);
            vst1q_f32(out + i, vaddq_f32(va, vb));
        }
        for (; i < size; i++) out[i] = a[i] + b[i];
#elif USE_SSE
        size_t i = 0;
        for (; i + 4 <= size; i += 4) {
            __m128 va = _mm_loadu_ps(a + i);
            __m128 vb = _mm_loadu_ps(b + i);
            _mm_storeu_ps(out + i, _mm_add_ps(va, vb));
        }
        for (; i < size; i++) out[i] = a[i] + b[i];
#else
        for (size_t i = 0; i < size; i++) out[i] = a[i] + b[i];
#endif
    }
    
    // 标量乘法 (SIMD 优化)
    static void scale(float* data, float scalar, size_t size) {
#if USE_NEON
        float32x4_t vs = vdupq_n_f32(scalar);
        size_t i = 0;
        for (; i + 4 <= size; i += 4) {
            float32x4_t vd = vld1q_f32(data + i);
            vst1q_f32(data + i, vmulq_f32(vd, vs));
        }
        for (; i < size; i++) data[i] *= scalar;
#elif USE_SSE
        __m128 vs = _mm_set1_ps(scalar);
        size_t i = 0;
        for (; i + 4 <= size; i += 4) {
            __m128 vd = _mm_loadu_ps(data + i);
            _mm_storeu_ps(data + i, _mm_mul_ps(vd, vs));
        }
        for (; i < size; i++) data[i] *= scalar;
#else
        for (size_t i = 0; i < size; i++) data[i] *= scalar;
#endif
    }
};

// =============================================================================
// 轻量级 ThreadPool - 移动端优化
// =============================================================================

class ThreadPool {
private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> pending_tasks_{0};
    
    // 线程优先级设置
    void set_thread_priority(std::thread& t, bool high_priority) {
#if defined(__ANDROID__) || defined(ANDROID)
        // Android: 设置线程优先级
        // SCHED_BATCH 对后台任务更友好
        // SCHED_NORMAL 对应 nice 值 0
        // 可以通过 setpriority() 设置 nice 值
#endif
        (void)high_priority;
    }
    
public:
    explicit ThreadPool(size_t threads = 1) : stop_{false} {
        // 移动端限制线程数
        size_t actual_threads = std::min(threads, size_t(2));
        
        for (size_t i = 0; i < actual_threads; ++i) {
            workers_.emplace_back([this, i] {
                // 设置线程名称
#if defined(__linux__)
                char name[16];
                snprintf(name, sizeof(name), "HyperPool-%zu", i);
                pthread_setname_np(pthread_self(), name);
#endif
                
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        condition_.wait(lock, [this] {
                            return stop_.load() || !tasks_.empty();
                        });
                        
                        if (stop_.load() && tasks_.empty()) {
                            return;
                        }
                        
                        if (!tasks_.empty()) {
                            task = std::move(tasks_.front());
                            tasks_.pop();
                        }
                    }
                    
                    if (task) {
                        task();
                        pending_tasks_--;
                    }
                }
            });
        }
    }
    
    ~ThreadPool() {
        stop_.store(true);
        condition_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
    
    // 提交任务
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<std::result_of_t<F(Args...)>> {
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
            
            tasks_.emplace([task]() { (*task)(); });
            pending_tasks_++;
        }
        
        condition_.notify_one();
        return result;
    }
    
    // 等待所有任务完成
    void wait_all() {
        while (pending_tasks_.load() > 0) {
            std::this_thread::yield();
        }
    }
    
    size_t pending() const { return pending_tasks_.load(); }
    bool is_stopped() const { return stop_.load(); }
};

// =============================================================================
// 并行计算辅助函数
// =============================================================================

namespace detail {

// 分块执行器
template<typename Func>
void parallel_for(size_t start, size_t end, size_t chunk_size, Func&& func) {
    if (end <= start) return;
    
    size_t total_size = end - start;
    size_t actual_chunk = std::max(chunk_size, size_t(1));
    size_t num_chunks = (total_size + actual_chunk - 1) / actual_chunk;
    
    for (size_t i = 0; i < num_chunks; i++) {
        size_t chunk_start = start + i * actual_chunk;
        size_t chunk_end = std::min(chunk_start + actual_chunk, end);
        func(chunk_start, chunk_end);
    }
}

// 线程本地存储的任务分配器
inline size_t get_thread_chunk_size(size_t total, size_t thread_count) {
    return std::max(size_t(1), total / thread_count);
}

} // namespace detail

// =============================================================================
// EMA 并行计算
// =============================================================================

class ParallelEMA {
private:
    static constexpr size_t MAX_SCALES = 4;
    float values_[MAX_SCALES]{0};
    float alphas_[MAX_SCALES]{0};
    size_t count_{0};
    
public:
    void init(size_t num_scales, const float* alphas) {
        count_ = std::min(num_scales, MAX_SCALES);
        for (size_t i = 0; i < count_; i++) {
            alphas_[i] = alphas[i];
        }
    }
    
    // 单值更新 - 串行
    void update(float new_value) {
        for (size_t i = 0; i < count_; i++) {
            values_[i] = values_[i] * (1.0f - alphas_[i]) + new_value * alphas_[i];
        }
    }
    
    // 批量更新 - 可并行化
    void update_parallel(const float* new_values, size_t size) {
        if (size == 1) {
            update(new_values[0]);
            return;
        }
        
        // 分块并行更新
        for (size_t i = 0; i < count_; i++) {
            values_[i] = values_[i] * (1.0f - alphas_[i]);
            float sum = 0;
            for (size_t j = 0; j < size; j++) {
                sum += new_values[j];
            }
            values_[i] += (sum / size) * alphas_[i];
        }
    }
    
    float get(size_t scale = 0) const {
        return scale < count_ ? values_[scale] : values_[0];
    }
    
    const float* get_all() const { return values_; }
    size_t count() const { return count_; }
};

// =============================================================================
// 全局线程池实例
// =============================================================================

inline ThreadPool& global_pool() {
    static ThreadPool pool(1);  // 单线程池
    return pool;
}

// =============================================================================
// 异步训练接口
// =============================================================================

class AsyncTrainer {
private:
    ThreadPool& pool_;
    std::atomic<bool> training_{false};
    
public:
    explicit AsyncTrainer(ThreadPool& pool) : pool_(pool) {}
    
    // 异步训练，不阻塞主循环
    template<typename TrainFunc, typename... Args>
    auto train_async(TrainFunc&& func, Args&&... args) -> std::future<void> {
        training_.store(true);
        return pool_.enqueue([this, func, args...]() {
            func(args...);
            training_.store(false);
        });
    }
    
    bool is_training() const { return training_.load(); }
};

// =============================================================================
// 内存池 - 减少分配开销
// =============================================================================

class MemoryPool {
private:
    struct Block {
        void* ptr;
        size_t size;
        Block* next;
    };
    
    Block* free_list_{nullptr};
    size_t block_size_{0};
    std::mutex mtx_;
    
public:
    explicit MemoryPool(size_t block_size, size_t num_blocks = 16) 
        : block_size_(block_size) {
        // 预分配块
        for (size_t i = 0; i < num_blocks; i++) {
            Block* block = new Block{
                ::operator new(block_size),
                block_size,
                free_list_
            };
            free_list_ = block;
        }
    }
    
    ~MemoryPool() {
        while (free_list_) {
            Block* next = free_list_->next;
            ::operator delete(free_list_->ptr);
            delete free_list_;
            free_list_ = next;
        }
    }
    
    void* alloc() {
        std::lock_guard<std::mutex> lock(mtx_);
        if (free_list_) {
            void* ptr = free_list_->ptr;
            free_list_ = free_list_->next;
            return ptr;
        }
        return ::operator new(block_size_);
    }
    
    void free(void* ptr) {
        std::lock_guard<std::mutex> lock(mtx_);
        Block* block = new Block{ptr, block_size_, free_list_};
        free_list_ = block;
    }
    
    template<typename T>
    T* alloc() { return static_cast<T*>(alloc()); }
    
    template<typename T>
    void free(T* ptr) { free(static_cast<void*>(ptr)); }
};

// 全局内存池
inline MemoryPool& global_mem_pool() {
    static MemoryPool pool(256);  // 256 bytes per block
    return pool;
}

} // namespace hp::parallel
