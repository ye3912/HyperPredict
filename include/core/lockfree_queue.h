#pragma once
#include <atomic>
#include <optional>
#include <new>
#include "core/types.h"

namespace hp {

template<typename T, size_t Cap>
class LockFreeQueue {
    static_assert((Cap & (Cap-1)) == 0, "Capacity must be power of 2");
    struct alignas(64) Node {
        std::atomic<uint64_t> seq{0};
        T data;
    };
    alignas(64) std::atomic<uint64_t> enq_{0}, deq_{0};
    
    // 优化: 使用静态内存替代 unique_ptr，避免堆分配
    alignas(64) Node buf_[Cap];
    
public:
    LockFreeQueue() {
        for (size_t i = 0; i < Cap; ++i) {
            buf_[i].seq.store(i, std::memory_order_relaxed);
        }
    }
    
    bool try_push(const T& item) noexcept {
        uint64_t pos = enq_.load(std::memory_order_relaxed);
        Node& n = buf_[pos & (Cap-1)];
        uint64_t seq = n.seq.load(std::memory_order_acquire);
        if ((int64_t)seq - (int64_t)pos == 0) {
            if (enq_.compare_exchange_weak(pos, pos+1, std::memory_order_relaxed)) {
                n.data = item; 
                n.seq.store(pos+1, std::memory_order_release); 
                return true;
            }
        }
        return false;
    }
    
    std::optional<T> try_pop() noexcept {
        uint64_t pos = deq_.load(std::memory_order_relaxed);
        Node& n = buf_[pos & (Cap-1)];
        uint64_t seq = n.seq.load(std::memory_order_acquire);
        if ((int64_t)seq - (int64_t)(pos+1) == 0) {
            if (deq_.compare_exchange_weak(pos, pos+1, std::memory_order_relaxed)) {
                T d = n.data; 
                n.seq.store(pos+Cap, std::memory_order_release); 
                return d;
            }
        }
        return std::nullopt;
    }
};
using FeatureQueue = LockFreeQueue<LoadFeature, 4096>;

} // namespace hp