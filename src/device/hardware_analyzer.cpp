#include "device/hardware_analyzer.h"
#include "device/soc_database.h"
#include "device/cpu_topology.h"
#include "core/logger.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace hp::device {

bool HardwareAnalyzer::analyze() noexcept {
    // 初始化默认值
    prof_.total_cores = 0;
    prof_.is_all_big = false;
    prof_.enable_lb = true;
    prof_.mig_threshold = 500;
    prof_.thermal_limit = 90;
    prof_.fas_sensitivity = 1.0f;
    prof_.sched_cpu = 4;
    std::fill(prof_.roles.begin(), prof_.roles.end(), CoreRole::LITTLE);

    // 1. 增强型 SoC 标识获取 (兼容 2019+ 所有主流平台)
    char buf[128] = {0};
    const char* props[] = {
        "ro.soc.model",           // 高通/部分联发科
        "ro.board.platform",      // 高通内部代号 / 部分老机型
        "ro.mediatek.platform",   // 联发科专用
        "ro.hardware",            // 通用兜底
        "ro.chipname"             // 部分老高通/麒麟
    };

    for (auto p : props) {
        std::string cmd = "getprop " + std::string(p) + " 2>/dev/null";
        FILE* fp = popen(cmd.c_str(), "r");
        if (fp) {
            if (fgets(buf, sizeof(buf), fp)) {
                buf[strcspn(buf, "\r\n")] = 0;
                pclose(fp);
                if (strlen(buf) > 2) break;
            }
            pclose(fp);
        }
    }

    std::string id(buf);
    LOGI("HW Probe: %s", id.c_str());

    // 2. 查库加载参数
    const SoCProfile* soc = SoCDatabase::find(id);
    if (soc) {        prof_.soc_name = soc->name;
        prof_.is_all_big = soc->is_all_big;
        prof_.mig_threshold = soc->mig_threshold;
        prof_.thermal_limit = soc->thermal_limit;
        prof_.fas_sensitivity = soc->fas_sensitivity;
        LOGI("DB Match: %s | FAS=%.2f | Mig=%u | Therm=%d°C", 
             soc->name.c_str(), soc->fas_sensitivity, soc->mig_threshold, soc->thermal_limit);
    } else {
        prof_.soc_name = "Unknown (" + id + ")";
        LOGW("SoC not in DB, applying safe fallback.");
    }

    // 3. 拓扑动态校准
    CpuTopology topo;
    if (topo.detect()) {
        prof_.total_cores = topo.get_total_cpus();
        const auto& domains = topo.get_domains();
        if (!domains.empty()) {
            std::vector<const CpuTopology::Domain*> sorted;
            for (auto& d : domains) sorted.push_back(&d);
            std::sort(sorted.begin(), sorted.end(), [](auto a, auto b) {
                return a->max_freq > b->max_freq;
            });

            // 角色分配
            int rank = 0;
            for (auto d : sorted) {
                for (int cpu : d->cpus) {
                    if (cpu >= 0 && cpu < 8) {
                        prof_.roles[cpu] = (rank == 0) ? CoreRole::PRIME :
                                           (rank == 1) ? CoreRole::BIG :
                                           (rank == 2) ? CoreRole::MID : CoreRole::LITTLE;
                    }
                }
                rank++;
            }

            // 架构策略判定
            bool dyn_all_big = (sorted.size() <= 2 && sorted.back()->max_freq > sorted.front()->max_freq * 0.82f);
            if (prof_.is_all_big || dyn_all_big) {
                prof_.is_all_big = true;
                prof_.enable_lb = false;
                prof_.mig_threshold = std::max(550u, prof_.mig_threshold);
                prof_.sched_cpu = sorted.front()->cpus[0];
                LOGI("All-Big Strategy: LB=OFF | SchedCPU=%d | MigThresh=%u", prof_.sched_cpu, prof_.mig_threshold);
            } else {
                prof_.sched_cpu = sorted.size() >= 2 ? sorted[0]->cpus[0] : 0;
            }
        }
    }
    LOGI("HW Ready: %s | Cores=%d", prof_.soc_name.c_str(), prof_.total_cores);
    return true;
}

} // namespace hp::device