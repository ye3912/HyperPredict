#include "core/system_collector.h"
#include "predict/feature_extractor.h"
#include "core/logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace hp {

SystemCollector::SystemCollector() noexcept {
    for(auto& s : prev_stat_) s = {};
}

bool SystemCollector::read_cpu_stat() {
    std::ifstream f("/proc/stat");
    if(!f) return false;
    
    std::string line;
    for(int i = 0; i < 8 && std::getline(f, line); ++i) {
        if(line.find("cpu" + std::to_string(i)) != 0) continue;
        
        std::istringstream iss(line);
        std::string label;
        CpuStat cur{};
        iss >> label >> cur.user >> cur.nice >> cur.system >> cur.idle 
            >> cur.iowait >> cur.irq >> cur.softirq;
        
        uint64_t active = cur.user + cur.nice + cur.system + cur.irq + cur.softirq;
        uint64_t total = active + cur.idle + cur.iowait;
        uint64_t prev_active = prev_stat_[i].user + prev_stat_[i].nice + 
                               prev_stat_[i].system + prev_stat_[i].irq + prev_stat_[i].softirq;
        uint64_t prev_total = prev_active + prev_stat_[i].idle + prev_stat_[i].iowait;
        
        if(total > prev_total) {
            util_[i] = static_cast<uint32_t>(
                (active - prev_active) * 1024 / (total - prev_total));
        }
        prev_stat_[i] = cur;
    }
    return true;
}

bool SystemCollector::read_run_queue() {
    std::ifstream f("/proc/loadavg");
    if(!f) return false;
    std::string line;
    if(std::getline(f, line)) {
        size_t pos = line.find('/');
        if(pos != std::string::npos) {
            size_t end = line.find(' ', pos);
            last_rq_ = std::stoul(line.substr(pos + 1, end - pos - 1));
        }
    }
    return true;
}

bool SystemCollector::read_thermal() {
    return true;
}

bool SystemCollector::read_battery() {
    return true;
}

LoadFeature SystemCollector::collect() noexcept {
    read_cpu_stat();
    read_run_queue();
    
    predict::FeatureExtractor fe;
    uint32_t avg_util = 0;
    for(int i = 4; i < 8; ++i) avg_util += util_[i];
    avg_util /= 4;
    
    int8_t thermal = 5;
    uint8_t battery = 85;
    uint32_t frame = 16666;
    uint32_t touch = 0;
    
    return fe.extract(avg_util, last_rq_, last_wake_, frame, touch, thermal, battery);
}

} // namespace hp