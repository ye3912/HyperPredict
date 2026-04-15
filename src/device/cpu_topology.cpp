// src/device/cpu_topology.cpp
#include "device/cpu_topology.h"
#include <fstream>
#include <map>
#include <cstdio>
namespace hp::device {
bool CpuTopology::detect() noexcept {
    domains_.clear(); total_=0;
    std::map<uint32_t, std::vector<int>> g;
    for(int i=0;i<16;++i){
        char p[128]; snprintf(p,sizeof(p),"/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq",i);
        std::ifstream f(p); if(f){ uint32_t v; if(f>>v){g[v].push_back(i); total_++;} }
    }
    if(!total_) return false;
    for(auto&[mx,cpus]:g){
        Domain d; d.cpus=std::move(cpus); d.max_freq=mx;
        char mp[128]; snprintf(mp,sizeof(mp),"/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_min_freq",d.cpus[0]);
        std::ifstream mf(mp); if(mf) mf>>d.min_freq; else d.min_freq=mx/4;
        domains_.push_back(std::move(d));
    }
    return true;
}
}