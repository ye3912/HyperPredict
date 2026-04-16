#include "core/system_collector.h"
#include "core/frame_pacer.h"
#include "core/logger.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <cinttypes>

namespace hp {

// 文件作用域静态变量 (保持状态)
static uint64_t last_cpu_time_[2] = {0, 0};
static uint64_t last_cpu_idle_[2] = {0, 0};
static uint32_t last_wakeups_ = 0;
static uint64_t last_touch_time = 0;
static uint32_t touch_count = 0;

// ✅ 新增：构造函数实现
SystemCollector::SystemCollector() {}

LoadFeature SystemCollector::collect() noexcept {
    LoadFeature f;
    
    // ✅ 调用成员函数
    f.cpu_util = read_cpu_util();
    f.run_queue_len = read_run_queue();
    f.wakeups_100ms = read_wakeups();
    
    static core::FramePacer pacer;
    static bool inited = false;
    if (!inited) {
        pacer.init();
        inited = true;
    }
    
    uint64_t interval = pacer.collect();
    if (interval > 0) {
        f.frame_interval_us = static_cast<uint32_t>(interval);
    } else {
        f.frame_interval_us = pacer.get_smooth_interval_us();
    }
    
    f.is_gaming = pacer.is_high_refresh() && pacer.is_stable();
    f.touch_rate_100ms = read_touch_rate();
    f.thermal_margin = read_thermal_margin();
    f.battery_level = read_battery_level();
    
    return f;}

// ✅ 修复：去掉 static，加上 SystemCollector:: 作用域
uint32_t SystemCollector::read_cpu_util() noexcept {
    FILE* fp = fopen("/proc/stat", "r");
    if (!fp) return 512;
    
    char line[256] = {0};
    unsigned long user, nice, system, idle, iowait, irq, softirq, steal;
    
    if (fgets(line, sizeof(line), fp)) {
        fclose(fp);
        
        int n = sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
                       &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
        if (n < 4) return 512;
        
        uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
        uint64_t idle_total = idle + iowait;
        
        uint64_t total_diff = total - last_cpu_time_[0];
        uint64_t idle_diff = idle_total - last_cpu_idle_[0];
        
        last_cpu_time_[0] = total;
        last_cpu_idle_[0] = idle_total;
        
        if (total_diff > 0) {
            uint32_t util = static_cast<uint32_t>((total_diff - idle_diff) * 1024 / total_diff);
            return std::min(util, static_cast<uint32_t>(1024));
        }
    } else {
        fclose(fp);
    }
    
    return 512;
}

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
uint32_t SystemCollector::read_wakeups() noexcept {
    FILE* fp = fopen("/proc/stat", "r");
    if (!fp) return 0;
    
    char line[256] = {0};
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "ctxt", 4) == 0) {
            fclose(fp);
            
            unsigned long ctxt = 0;
            if (sscanf(line, "ctxt %lu", &ctxt) == 1) {
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

uint32_t SystemCollector::read_touch_rate() noexcept {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        uint64_t now = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
        
        if (last_touch_time > 0) {
            uint64_t delta = now - last_touch_time;
            if (delta < 100000) {
                touch_count++;
            } else {
                touch_count = 0;
            }
        }
        
        last_touch_time = now;
        return std::min(touch_count, static_cast<uint32_t>(200));
    }
    
    return 0;
}

int32_t SystemCollector::read_thermal_margin() noexcept {
    const char* thermal_paths[] = {
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/class/thermal/thermal_zone1/temp",
        "/sys/class/thermal/thermal_zone2/temp",        "/sys/devices/virtual/thermal/thermal_zone0/temp"
    };
    
    int32_t current_temp = 35;
    
    for (auto path : thermal_paths) {
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
    return std::max(0, std::min(margin, 60));
}

int32_t SystemCollector::read_battery_level() noexcept {
    FILE* fp = fopen("/sys/class/power_supply/battery/capacity", "r");
    if (!fp) fp = fopen("/sys/class/power_supply/bq27541/capacity", "r");
    
    if (fp) {
        int32_t level = 0;
        if (fscanf(fp, "%d", &level) == 1) {
            fclose(fp);
            return std::clamp(level, 0, 100);
        }
        fclose(fp);
    }
    
    return 100;
}

} // namespace hp