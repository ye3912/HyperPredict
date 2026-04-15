// src/device/hardware_analyzer.cpp
#include "device/hardware_analyzer.h"
#include "core/logger.h"
#include <fstream>
#include <cstring>
namespace hp::device {
bool HardwareAnalyzer::analyze() noexcept {
    CpuTopology topo;
    if(!topo.detect()){LOGW("Topology fallback");return false;}
    prof_.total_cores=topo.get_total_cpus();
    char buf[128]={0}; FILE*p=popen("getprop ro.product.board 2>/dev/null;getprop ro.soc.model 2>/dev/null","r");
    if(p){fgets(buf,sizeof(buf),p);pclose(p);}
    prof_.soc_name=strstr(buf,"SM8650")?"Snapdragon 8 Gen 3":strstr(buf,"SM8550")?"Snapdragon 8 Gen 2":strstr(buf,"MT6985")?"Dimensity 9300":buf;
    const auto&doms=topo.get_domains(); int rank=static_cast<int>(doms.size())-1;
    for(const auto&d:doms){for(int c:d.cpus)if(c>=0&&c<8)prof_.roles[c]=(rank>=2)?CoreRole::PRIME:(rank==1)?CoreRole::BIG:(rank==0&&doms.size()>=3)?CoreRole::MID:CoreRole::LITTLE; rank--;}
    prof_.is_all_big=(prof_.roles[0]>=CoreRole::BIG);
    prof_.enable_lb=prof_.is_all_big||prof_.total_cores>=8;
    prof_.mig_threshold=prof_.is_all_big?60:75;
    prof_.sched_cpu=prof_.is_all_big?4:(doms.size()>=2?doms.back().cpus[0]:0);
    LOGI("HW: %s | Cores=%d | SchedCPU=%d | LB=%s",prof_.soc_name.c_str(),prof_.total_cores,prof_.sched_cpu,prof_.enable_lb?"ON":"OFF");
    return true;
}
}