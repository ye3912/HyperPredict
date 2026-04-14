#include "core/system_collector.h"
#include <fstream>
#include <sstream>
#include <string>
#include <dirent.h>
#include <cstring>

namespace hp {

SystemCollector::SystemCollector() 
    : proc_stat_path_("/proc/stat"), thermal_base_path_("/sys/class/thermal") {}

bool SystemCollector::is_gaming_scene() noexcept {
    const char* gaming_paths[] = {
        "/data/data/com.tencent.tmgp.sgame",
        "/data/data/com.miHoYo.Yuanshen",
        "/data/data/com.tencent.tmgp.pubgmhd",
        "/data/data/com.tencent.lolm",
    };
    
    for(const auto& path : gaming_paths) {
        if(access(path, F_OK) == 0) return true;
    }
    return false;
}

uint32_t SystemCollector::read_frame_interval() noexcept {
    std::ifstream f("/sys/class/drm/card0/device/fps");
    if(f) {
        float fps = 0;
        f >> fps;
        if(fps > 0) return static_cast<uint32_t>(1000000 / fps);
    }
    return 16666;
}

uint32_t SystemCollector::read_touch_rate() noexcept {
    uint32_t rate = 0;
    DIR* dir = opendir("/dev/input");
    if(dir) {
        struct dirent* entry;
        while((entry = readdir(dir)) != nullptr) {
            if(strncmp(entry->d_name, "event", 5) == 0) rate++;
        }
        closedir(dir);
    }
    return rate;
}

LoadFeature SystemCollector::collect() noexcept {    LoadFeature f;
    f.cpu_util = read_cpu_util();
    f.run_queue_len = read_run_queue();
    f.wakeups_100ms = read_wakeups();
    f.thermal_margin = read_thermal_margin();
    f.battery_level = read_battery_level();
    f.frame_interval_us = read_frame_interval();
    f.touch_rate_100ms = read_touch_rate();
    f.is_gaming = is_gaming_scene();
    return f;
}

uint32_t SystemCollector::read_cpu_util() noexcept {
    std::ifstream f(proc_stat_path_);
    if(!f) return 0;
    
    std::string line;
    std::getline(f, line);
    
    std::istringstream iss(line);
    std::string cpu;
    uint64_t user, nice, system, idle, iowait, irq, softirq;
    
    iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq;
    
    uint64_t total = user + nice + system + idle + iowait + irq + softirq;
    uint64_t active = user + nice + system + irq + softirq;
    
    return static_cast<uint32_t>((active * 1024) / total);
}

uint32_t SystemCollector::read_run_queue() noexcept {
    std::ifstream f("/proc/loadavg");
    if(f) {
        float load = 0;
        f >> load;
        return static_cast<uint32_t>(load * 10);
    }
    return 0;
}

uint32_t SystemCollector::read_wakeups() noexcept {
    return 0;
}

int8_t SystemCollector::read_thermal_margin() noexcept {
    int max_temp = 0;
    for(int i = 0; i < 10; ++i) {
        std::string path = thermal_base_path_ + "/thermal_zone" + std::to_string(i) + "/temp";
        std::ifstream f(path);        if(f) {
            int temp;
            f >> temp;
            if(temp > max_temp) max_temp = temp;
        }
    }
    int margin = 80000 - max_temp;
    return static_cast<int8_t>(margin / 1000);
}

uint8_t SystemCollector::read_battery_level() noexcept {
    std::ifstream f("/sys/class/power_supply/battery/capacity");
    if(f) {
        int level;
        f >> level;
        return static_cast<uint8_t>(level);
    }
    return 85;
}

} // namespace hp