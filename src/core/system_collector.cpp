#include "core/system_collector.h"
#include "core/frame_pacer.h"
#include "core/logger.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>

namespace hp::core {

// ────────── 辅助函数声明 ──────────
static uint32_t read_cpu_util() noexcept;
static uint32_t read_run_queue() noexcept;
static uint32_t read_wakeups() noexcept;
static uint32_t read_touch_rate() noexcept;
static int32_t read_thermal_margin() noexcept;
static int32_t read_battery_level() noexcept;

// ────────── 静态变量 (保持状态) ──────────
static uint64_t last_cpu_time_[2] = {0, 0};
static uint64_t last_cpu_idle_[2] = {0, 0};
static uint32_t last_wakeups_ = 0;
static uint64_t last_touch_time_ = 0;
static uint32_t touch_count_ = 0;

// ────────── 主采集函数 ──────────
LoadFeature SystemCollector::collect() noexcept {
    LoadFeature f;
    
    // 1. CPU 利用率 (0~1024 scale)
    f.cpu_util = read_cpu_util();
    
    // 2. 运行队列长度
    f.run_queue_len = read_run_queue();
    
    // 3. 唤醒次数 (每 100ms)
    f.wakeups_100ms = read_wakeups();
    
    // 4. ✅ 真实帧生成时间间隔 (Frame Pacer)
    static FramePacer pacer;
    static bool inited = false;
    if (!inited) {
        pacer.init();
        inited = true;
        LOGI("FramePacer initialized");
    }
    
    uint64_t interval = pacer.collect();
    if (interval > 0) {        f.frame_interval_us = static_cast<uint32_t>(interval);
    } else {
        // 采集失败时使用平滑值
        f.frame_interval_us = pacer.get_smooth_interval_us();
    }
    
    // 游戏场景识别：高刷 + 稳定帧率
    f.is_gaming = pacer.is_high_refresh() && pacer.is_stable();
    
    // 5. 触摸采样率 (每 100ms)
    f.touch_rate_100ms = read_touch_rate();
    
    // 6. 温控余量 (°C)
    f.thermal_margin = read_thermal_margin();
    
    // 7. 电量 (0~100)
    f.battery_level = read_battery_level();
    
    return f;
}

// ────────── CPU 利用率读取 ──────────
static uint32_t read_cpu_util() noexcept {
    FILE* fp = fopen("/proc/stat", "r");
    if (!fp) return 512;  // 默认 50%
    
    char line[256] = {0};
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
    
    if (fgets(line, sizeof(line), fp)) {
        fclose(fp);
        
        int n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
        if (n < 4) return 512;
        
        uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
        uint64_t idle_total = idle + iowait;
        
        // 计算差值
        uint64_t total_diff = total - last_cpu_time_[0];
        uint64_t idle_diff = idle_total - last_cpu_idle_[0];
        
        // 保存当前值
        last_cpu_time_[0] = total;
        last_cpu_idle_[0] = idle_total;
        
        // 计算利用率 (0~1024 scale)
        if (total_diff > 0) {
            uint32_t util = static_cast<uint32_t>((total_diff - idle_diff) * 1024 / total_diff);            return std::min(util, static_cast<uint32_t>(1024));
        }
    } else {
        fclose(fp);
    }
    
    return 512;
}

// ────────── 运行队列长度读取 ──────────
static uint32_t read_run_queue() noexcept {
    FILE* fp = fopen("/proc/loadavg", "r");
    if (!fp) return 0;
    
    float load_1min = 0;
    if (fscanf(fp, "%f", &load_1min) == 1) {
        fclose(fp);
        // 转换为整数 (0~32 scale)
        return static_cast<uint32_t>(load_1min * 4);
    }
    
    fclose(fp);
    return 0;
}

// ────────── 唤醒次数读取 ──────────
static uint32_t read_wakeups() noexcept {
    // 读取 /proc/stat 中的 ctxt (上下文切换) 或 intr (中断)
    FILE* fp = fopen("/proc/stat", "r");
    if (!fp) return 0;
    
    char line[256] = {0};
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "ctxt", 4) == 0) {
            fclose(fp);
            
            uint64_t ctxt = 0;
            if (sscanf(line, "ctxt %llu", &ctxt) == 1) {
                uint32_t diff = static_cast<uint32_t>(ctxt - last_wakeups_);
                last_wakeups_ = static_cast<uint32_t>(ctxt);
                return std::min(diff, static_cast<uint32_t>(1000));
            }
            break;
        }
    }
    
    fclose(fp);
    return 0;
}
// ────────── 触摸采样率读取 ──────────
static uint32_t read_touch_rate() noexcept {
    // 读取 /dev/input/event* 的触摸事件 (简化版)
    // 实际项目中可集成 InputReader 或监听 getevent
    
    // 兜底：基于时间间隔估算
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        uint64_t now = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
        
        if (last_touch_time_ > 0) {
            uint64_t delta = now - last_touch_time_;
            if (delta < 100000) {  // 100ms 内
                touch_count_++;
            } else {
                touch_count_ = 0;
            }
        }
        
        last_touch_time_ = now;
        return std::min(touch_count_, static_cast<uint32_t>(200));
    }
    
    return 0;
}

// ────────── 温控余量读取 ──────────
static int32_t read_thermal_margin() noexcept {
    // 读取 thermal zone 温度
    const char* thermal_paths[] = {
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/class/thermal/thermal_zone1/temp",
        "/sys/class/thermal/thermal_zone2/temp",
        "/sys/devices/virtual/thermal/thermal_zone0/temp"
    };
    
    int32_t current_temp = 35;  // 默认 35°C
    
    for (auto path : thermal_paths) {
        FILE* fp = fopen(path, "r");
        if (fp) {
            int32_t temp = 0;
            if (fscanf(fp, "%d", &temp) == 1) {
                // 温度单位可能是 m°C 或 °C
                if (temp > 1000) {
                    temp /= 1000;
                }
                if (temp > 20 && temp < 100) {
                    current_temp = temp;
                    fclose(fp);                    break;
                }
            }
            fclose(fp);
        }
    }
    
    // 温控余量 = 温控线 - 当前温度
    // 默认温控线 85°C
    int32_t margin = 85 - current_temp;
    return std::max(0, std::min(margin, static_cast<int32_t>(60)));
}

// ────────── 电量读取 ──────────
static int32_t read_battery_level() noexcept {
    FILE* fp = fopen("/sys/class/power_supply/battery/capacity", "r");
    if (!fp) {
        // 尝试备用路径
        fp = fopen("/sys/class/power_supply/bq27541/capacity", "r");
    }
    
    if (fp) {
        int32_t level = 0;
        if (fscanf(fp, "%d", &level) == 1) {
            fclose(fp);
            return std::clamp(level, 0, 100);
        }
        fclose(fp);
    }
    
    return 100;  // 默认满电
}

} // namespace hp::core