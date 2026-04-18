#pragma once
#include "core/types.h"
#include <string>
#include <fcntl.h>
#include <chrono>

namespace hp {

class SystemCollector {
    std::string proc_stat_path_;
    std::string thermal_base_path_;
    
    // 优化: fd 缓存
    int thermal_fds_[4] = {-1, -1, -1, -1};
    int battery_fd_ = -1;
    int proc_stat_fd_ = -1;
    int proc_loadavg_fd_ = -1;
    
    // ✅ 新增: 结果缓存 + TTL
    struct CacheEntry {
        uint32_t value = 0;
        std::chrono::steady_clock::time_point timestamp;
        bool valid = false;
    };
    
    // 缓存 TTL (毫秒)
    static constexpr uint32_t CPU_UTIL_TTL_MS = 20;
    static constexpr uint32_t THERMAL_TTL_MS = 500;
    static constexpr uint32_t BATTERY_TTL_MS = 5000;
    static constexpr uint32_t FPS_TTL_MS = 100;
    static constexpr uint32_t LOAD_TTL_MS = 50;
    static constexpr uint32_t GAMING_TTL_MS = 5000;
    
    CacheEntry cpu_util_cache_;
    CacheEntry run_queue_cache_;
    CacheEntry thermal_cache_;
    CacheEntry battery_cache_;
    CacheEntry frame_interval_cache_;
    bool cached_gaming_ = false;
    std::chrono::steady_clock::time_point gaming_cache_time_;
    
    bool cache_expired(const CacheEntry& entry, uint32_t ttl_ms) noexcept;
    void update_cache(CacheEntry& entry, uint32_t value) noexcept;
    
public:
    SystemCollector();
    ~SystemCollector();
    
    LoadFeature collect() noexcept;
    bool is_gaming_scene() noexcept;
    
    // 预读取下一帧数据
    void prefetch() noexcept;
    // 强制刷新缓存
    void invalidate() noexcept;

private:
    uint32_t read_cpu_util_cached() noexcept;
    uint32_t read_run_queue_cached() noexcept;
    uint32_t read_wakeups() noexcept;
    int8_t read_thermal_margin_cached() noexcept;
    uint8_t read_battery_level_cached() noexcept;
    uint32_t read_touch_rate() noexcept;
    uint32_t read_frame_interval_cached() noexcept;
    
    // 原始读取 (bypass cache)
    uint32_t read_cpu_util_raw() noexcept;
    uint32_t read_run_queue_raw() noexcept;
    int8_t read_thermal_margin_raw() noexcept;
    uint8_t read_battery_level_raw() noexcept;
    uint32_t read_frame_interval_raw() noexcept;

    // 别名 - 调用原始版本 (实现在 cpp 中)
    uint32_t read_cpu_util() noexcept;
    uint32_t read_run_queue() noexcept;
    int8_t read_thermal_margin() noexcept;
    uint8_t read_battery_level() noexcept;
    
    // 辅助: safe read from fd
    bool safe_read(int fd, char* buf, size_t len) noexcept;
};

} // namespace hp