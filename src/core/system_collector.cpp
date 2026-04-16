#include "core/system_collector.h"
#include "core/frame_pacer.h"
#include "core/logger.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>

namespace hp {

// ────────── 构造函数 ──────────
SystemCollector::SystemCollector()
    : proc_stat_path_("/proc/stat")
    , thermal_base_path_("/sys/class/thermal") {
}

// ────────── 主采集函数 ──────────
LoadFeature SystemCollector::collect() noexcept {
    LoadFeature f;
    
    // 1. CPU 利用率
    f.cpu_util = read_cpu_util();
    
    // 2. 运行队列长度
    f.run_queue_len = read_run_queue();
    
    // 3. 唤醒次数
    f.wakeups_100ms = read_wakeups();
    
    // 4. 真实帧间隔 (FramePacer)
    static FramePacer pacer;
    static bool inited = false;
    if (!inited) {
        pacer.init();
        inited = true;
        LOGI("FramePacer initialized");
    }
    
    uint64_t interval = pacer.collect();
    f.frame_interval_us = (interval > 0) 
        ? static_cast<uint32_t>(interval) 
        : pacer.get_smooth_interval_us();
    
    f.is_gaming = pacer.is_high_refresh() && pacer.is_stable();
    
    // 5. 触摸率
    f.touch_rate_100ms = read_touch_rate();
    
    // 6. 温控余量    f.thermal_margin = read_thermal_margin();
    
    // 7. 电量
    f.battery_level = read_battery_level();
    
    return f;
}

// ────────── 游戏场景判断 ──────────
bool SystemCollector::is_gaming_scene() noexcept {
    // 基于帧率和触摸率综合判断
    static FramePacer pacer;
    float fps = pacer.get_instant_fps();
    
    // 高刷 (>90fps) 或高触摸率 (>50/100ms) 视为游戏
    return (fps > 90.0f) || (read_touch_rate() > 50);
}

// ────────── CPU 利用率读取 ──────────
uint32_t SystemCollector::read_cpu_util() noexcept {
    FILE* fp = fopen(proc_stat_path_.c_str(), "r");
    if (!fp) return 512;
    
    char line[256] = {0};
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
    
    if (fgets(line, sizeof(line), fp)) {
        fclose(fp);
        
        int n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
        if (n < 4) return 512;
        
        uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
        uint64_t idle_total = idle + iowait;
        
        static uint64_t last_total = 0;
        static uint64_t last_idle = 0;
        
        uint64_t total_diff = (last_total > 0) ? (total - last_total) : 0;
        uint64_t idle_diff = (last_idle > 0) ? (idle_total - last_idle) : 0;
        
        last_total = total;
        last_idle = idle_total;
        
        if (total_diff > 0) {
            uint32_t util = static_cast<uint32_t>((total_diff - idle_diff) * 1024 / total_diff);
            return std::min(util, static_cast<uint32_t>(1024));
        }
    } else {        fclose(fp);
    }
    
    return 512;
}

// ────────── 运行队列读取 ──────────
uint32_t SystemCollector::read_run_queue() noexcept {
    FILE* fp = fopen("/proc/loadavg", "r");
    if (!fp) return 0;
    
    float load_1min = 0;
    if (fscanf(fp, "%f", &load_1min) == 1) {
        fclose(fp);
        return static_cast<uint32_t>(load_1min * 4);
    }
    
    fclose(fp);
    return 0;
}

// ────────── 唤醒次数读取 ──────────
uint32_t SystemCollector::read_wakeups() noexcept {
    FILE* fp = fopen(proc_stat_path_.c_str(), "r");
    if (!fp) return 0;
    
    char line[256] = {0};
    static uint64_t last_ctxt = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "ctxt", 4) == 0) {
            fclose(fp);
            
            uint64_t ctxt = 0;
            if (sscanf(line, "ctxt %llu", &ctxt) == 1) {
                uint32_t diff = static_cast<uint32_t>(ctxt - last_ctxt);
                last_ctxt = ctxt;
                return std::min(diff, static_cast<uint32_t>(1000));
            }
            break;
        }
    }
    
    fclose(fp);
    return 0;
}

// ────────── 温控余量读取 ──────────
int8_t SystemCollector::read_thermal_margin() noexcept {
    const char* thermal_zones[] = {        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/class/thermal/thermal_zone1/temp",
        "/sys/class/thermal/thermal_zone2/temp",
        "/sys/devices/virtual/thermal/thermal_zone0/temp"
    };
    
    int32_t current_temp = 35;
    
    for (auto path : thermal_zones) {
        FILE* fp = fopen(path, "r");
        if (fp) {
            int32_t temp = 0;
            if (fscanf(fp, "%d", &temp) == 1) {
                if (temp > 1000) temp /= 1000;
                if (temp > 20 && temp < 100) {
                    current_temp = temp;
                    fclose(fp);
                    break;
                }
            }
            fclose(fp);
        }
    }
    
    int32_t margin = 85 - current_temp;
    return static_cast<int8_t>(std::max(0, std::min(margin, 60)));
}

// ────────── 电量读取 ──────────
uint8_t SystemCollector::read_battery_level() noexcept {
    FILE* fp = fopen("/sys/class/power_supply/battery/capacity", "r");
    if (!fp) {
        fp = fopen("/sys/class/power_supply/bq27541/capacity", "r");
    }
    
    if (fp) {
        int32_t level = 0;
        if (fscanf(fp, "%d", &level) == 1) {
            fclose(fp);
            return static_cast<uint8_t>(std::clamp(level, 0, 100));
        }
        fclose(fp);
    }
    
    return 100;
}

// ────────── 触摸率读取 ──────────
uint32_t SystemCollector::read_touch_rate() noexcept {
    static uint64_t last_time = 0;    static uint32_t count = 0;
    
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        uint64_t now = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
        
        if (last_time > 0) {
            uint64_t delta = now - last_time;
            if (delta < 100000) {
                count++;
            } else {
                count = 0;
            }
        }
        
        last_time = now;
        return std::min(count, static_cast<uint32_t>(200));
    }
    
    return 0;
}

// ────────── 帧间隔读取 (已废弃，由 FramePacer 接管) ──────────
uint32_t SystemCollector::read_frame_interval() noexcept {
    // 此方法已被 FramePacer 替代，保留仅为兼容
    return 16666;  // 默认 60fps
}

} // namespace hp