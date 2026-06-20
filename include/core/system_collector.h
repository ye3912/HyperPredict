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
    static constexpr uint32_t PACKAGE_TTL_MS = 5000;  // 包名缓存 5 秒（减少开销）

    CacheEntry cpu_util_cache_;
    CacheEntry run_queue_cache_;
    CacheEntry thermal_cache_;
    CacheEntry battery_cache_;
    CacheEntry frame_interval_cache_;
    uint64_t last_wakeups_{0};  // 避免 uint32_t 截断
    bool cached_gaming_ = false;
    std::chrono::steady_clock::time_point gaming_cache_time_;

    // 包名缓存
    char cached_package_name_[64]{0};
    std::chrono::steady_clock::time_point package_cache_time_;

    // netlink 监控（用于检测应用切换）
    int netlink_fd_{-1};
    bool netlink_enabled_{false};

    bool cache_expired(const CacheEntry& entry, uint32_t ttl_ms) noexcept;
    void update_cache(CacheEntry& entry, uint32_t value) noexcept;
    void init_netlink() noexcept;  // 初始化 netlink
    void cleanup_netlink() noexcept;  // 清理 netlink
    bool check_netlink_events() noexcept;  // 检查 netlink 事件
    
public:
    SystemCollector();
    ~SystemCollector();
    
    LoadFeature collect() noexcept;
    bool is_gaming_scene() noexcept;

private:
    uint32_t read_wakeups() noexcept;
    uint32_t read_touch_rate() noexcept;
    const char* read_package_name_cached() noexcept;  // 读取前台应用包名

    // 实际实现的读取函数
    uint32_t read_cpu_util() noexcept;
    uint32_t read_run_queue() noexcept;
    int8_t read_thermal_margin() noexcept;
    uint8_t read_battery_level() noexcept;
};

} // namespace hp