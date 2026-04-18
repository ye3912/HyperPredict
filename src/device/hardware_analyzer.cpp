#include "device/hardware_analyzer.h"
#include "device/soc_database.h"
#include "device/cpu_topology.h"
#include "core/logger.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>

namespace hp::device {

bool HardwareAnalyzer::analyze() noexcept {
    // 1. 初始化默认配置 (mig_threshold 使用 0-1024 归一化值)
    prof_.soc_name = "Unknown";
    prof_.total_cores = 0;
    prof_.is_all_big = false;
    prof_.enable_lb = true;
    prof_.mig_threshold = 700;  // 默认 70% 利用率触发迁移
    prof_.thermal_limit = 90;
    prof_.fas_sensitivity = 1.0f;
    prof_.min_freq_khz = 300000;  // 默认最低频率 300MHz
    prof_.sched_cpu = 4;
    std::fill(prof_.roles.begin(), prof_.roles.end(), CoreRole::LITTLE);

    // 2. 获取 SoC 标识符
    char buf[128] = {0};
    const char* props[] = {
        "ro.soc.model",
        "ro.board.platform",
        "ro.mediatek.platform",
        "ro.hardware",
        "ro.chipname"
    };

    for (const char* prop : props) {
        std::string cmd = "getprop " + std::string(prop) + " 2>/dev/null";
        FILE* fp = popen(cmd.c_str(), "r");
        if (fp) {
            if (fgets(buf, sizeof(buf), fp)) {
                buf[strcspn(buf, "\r\n")] = 0;
                pclose(fp);
                if (std::strlen(buf) > 2) break;
            } else {
                pclose(fp);
            }
        }
    }

    std::string id(buf);    LOGI("HW Probe: %s", id.c_str());

    // 3. 匹配数据库配置
    const SoCProfile* soc = SoCDatabase::find(id);
    if (soc) {
        prof_.soc_name = soc->name;
        prof_.is_all_big = soc->is_all_big;
        prof_.mig_threshold = soc->mig_threshold;
        prof_.thermal_limit = soc->thermal_limit;
        prof_.fas_sensitivity = soc->fas_sensitivity;
        prof_.min_freq_khz = soc->min_freq_khz;  // ✅ 从 SoCProfile 获取最低频率
        LOGI("DB Match: %s | FAS=%.2f | Mig=%u | Therm=%d°C | MinFreq=%u kHz | LB=%s",
             soc->name.c_str(), soc->fas_sensitivity, soc->mig_threshold,
             soc->thermal_limit, soc->min_freq_khz, soc->is_all_big ? "OFF" : "ON");
    } else {
        LOGW("SoC not in DB, applying safe fallback.");
    }

    // 4. 拓扑动态校准
    CpuTopology topo;
    if (topo.detect()) {
        prof_.total_cores = topo.get_total_cpus();
        const auto& domains = topo.get_domains();

        if (!domains.empty()) {
            // 按频率降序排序
            std::vector<const CpuTopology::Domain*> sorted;
            for (const auto& d : domains) {
                sorted.push_back(&d);
            }
            std::sort(sorted.begin(), sorted.end(), [](const CpuTopology::Domain* a, const CpuTopology::Domain* b) {
                return a->max_freq > b->max_freq;
            });

            // 分配核心角色
            int rank = 0;
            for (const auto* d : sorted) {
                for (int cpu : d->cpus) {
                    if (cpu >= 0 && cpu < 8) {
                        prof_.roles[cpu] = (rank == 0) ? CoreRole::PRIME :
                                           (rank == 1) ? CoreRole::BIG :
                                           (rank == 2) ? CoreRole::MID : CoreRole::LITTLE;
                    }
                }
                rank++;
            }

            // ✅ 改进的全大核策略：启用轻量级负载均衡
            bool dyn_all_big = (sorted.size() <= 2 && 
                                sorted.front()->max_freq > 0 &&
                                sorted.back()->max_freq > (sorted.front()->max_freq * 82 / 100));
            if (prof_.is_all_big || dyn_all_big) {
                prof_.is_all_big = true;
                prof_.enable_lb = true;  // ✅ 启用 LB，但通过较低的 mig_threshold 控制
                prof_.mig_threshold = std::min(650u, prof_.mig_threshold);
                prof_.sched_cpu = sorted.front()->cpus[0];
                LOGI("All-Big Optimized: LB=ON(Light) | SchedCPU=%d | MigThresh=%u", 
                     prof_.sched_cpu, prof_.mig_threshold);
            } else {
                prof_.sched_cpu = sorted.front()->cpus[0];
            }
        }
    }

    LOGI("HW Ready: %s | Cores=%d", prof_.soc_name.c_str(), prof_.total_cores);
    return true;
}

} // namespace hp::device