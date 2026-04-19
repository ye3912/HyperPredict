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

std::string HardwareAnalyzer::getSystemProperty(const char* prop) noexcept {
    char buf[128] = {0};
    std::string cmd = "getprop " + std::string(prop);
    FILE* fp = popen(cmd.c_str(), "r");
    if (fp) {
        if (fgets(buf, sizeof(buf), fp)) {
            buf[strcspn(buf, "\r\n")] = 0;
            pclose(fp);
            if (std::strlen(buf) > 2) return std::string(buf);
        } else {
            pclose(fp);
        }
    }
    return "";
}

std::string HardwareAnalyzer::detectDeviceModel() noexcept {
    // 尝试多个属性来获取设备型号
    const char* props[] = {
        "ro.product.model",
        "ro.product.device",
        "ro.build.product",
        "ro.vendor.product.model"
    };

    for (const char* prop : props) {
        std::string value = getSystemProperty(prop);
        if (!value.empty()) {
            return value;
        }
    }

    return "";
}

std::string HardwareAnalyzer::detectCpuArchitecture() noexcept {
    // 读取 /proc/cpuinfo 来检测 CPU 架构
    FILE* fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return "Unknown";

    char line[256];
    std::string arch = "Unknown";

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "CPU architecture")) {
            if (strstr(line, "ARMv8")) {
                arch = "ARMv8";
            } else if (strstr(line, "ARMv9")) {
                arch = "ARMv9";
            }
            break;
        }
    }

    fclose(fp);
    return arch;
}

std::string HardwareAnalyzer::detectCpuMicroarch() noexcept {
    // 读取 /proc/cpuinfo 来检测 CPU 微架构
    FILE* fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return "Unknown";

    char line[256];
    std::string microarch = "Unknown";

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "CPU part")) {
            // ARM CPU part numbers - 按编号唯一匹配
            // Cortex-X 系列 (大核)
            if (strstr(line, "0xd05")) {  // Cortex-A55 (需要先检查，因为编号可能部分匹配)
                microarch = "Cortex-A55";
            } else if (strstr(line, "0xd41")) {  // Cortex-A78
                microarch = "Cortex-A78";
            } else if (strstr(line, "0xd47")) {  // Cortex-A710
                microarch = "Cortex-A710";
            } else if (strstr(line, "0xd4d")) {  // Cortex-X4 (0xd4d 后检查避免被覆盖)
                microarch = "Cortex-X4";
            } else if (strstr(line, "0xd4e")) {  // Cortex-A720
                microarch = "Cortex-A720";
            } else if (strstr(line, "0xd4f")) {  // Cortex-A520
                microarch = "Cortex-A520";
            } else if (strstr(line, "0xd4b")) {  // Cortex-X3
                microarch = "Cortex-X3";
            } else if (strstr(line, "0xd49")) {  // Cortex-X2
                microarch = "Cortex-X2";
            } else if (strstr(line, "0xd46")) {  // Cortex-A510 (更新型号)
                microarch = "Cortex-A510";
            } else if (strstr(line, "0xd48")) {  // Cortex-A715 (修正编号)
                microarch = "Cortex-A715";
            } else if (strstr(line, "0xd45")) {  // Cortex-X1 (新增)
                microarch = "Cortex-X1";
            } else if (strstr(line, "0xd43")) {  // Cortex-A78 (更新编号)
                microarch = "Cortex-A78";
            } else if (strstr(line, "0xd0d")) {  // Cortex-A77
                microarch = "Cortex-A77";
            } else if (strstr(line, "0xd0a")) {  // Cortex-A76
                microarch = "Cortex-A76";
            } else if (strstr(line, "0xd07")) {  // Cortex-A76 (备用)
                microarch = "Cortex-A76";
            }
            break;
        }
    }

    fclose(fp);
    return microarch;
}

bool HardwareAnalyzer::analyze() noexcept {
    // 1. 初始化默认配置 (mig_threshold 使用 0-1024 归一化值)
    prof_.soc_name = "Unknown";
    prof_.manufacturer = "Unknown";
    prof_.architecture = "Unknown";
    prof_.microarch = "Unknown";
    prof_.device_model = "Unknown";
    prof_.total_cores = 0;
    prof_.is_all_big = false;
    prof_.enable_lb = true;
    prof_.mig_threshold = 700;  // 默认 70% 利用率触发迁移
    prof_.thermal_limit = 90;
    prof_.fas_sensitivity = 1.0f;
    prof_.min_freq_khz = 300000;  // 默认最低频率 300MHz
    prof_.sched_cpu = 4;
    std::fill(prof_.roles.begin(), prof_.roles.end(), CoreRole::LITTLE);

    // 2. 检测设备型号
    prof_.device_model = detectDeviceModel();
    LOGI("Device Model: %s", prof_.device_model.c_str());

    // 3. 检测 CPU 架构和微架构
    prof_.architecture = detectCpuArchitecture();
    prof_.microarch = detectCpuMicroarch();
    LOGI("CPU Architecture: %s, Microarch: %s", prof_.architecture.c_str(), prof_.microarch.c_str());

    // 4. 获取 SoC 标识符
    char buf[128] = {0};
    const char* props[] = {
        "ro.soc.model",
        "ro.board.platform",
        "ro.mediatek.platform",
        "ro.hardware",
        "ro.chipname",
        "ro.product.board"
    };

    for (const char* prop : props) {
        std::string value = getSystemProperty(prop);
        if (!value.empty()) {
            strncpy(buf, value.c_str(), sizeof(buf) - 1);
            break;
        }
    }

    std::string id(buf);
    LOGI("SoC ID: %s", id.c_str());

    // 5. 通过设备型号查找 SoC
    const SoCProfile* soc = nullptr;
    if (!prof_.device_model.empty()) {
        soc = SoCDatabase::findByDevice(prof_.device_model);
        if (soc) {
            LOGI("Device Match: %s -> %s", prof_.device_model.c_str(), soc->name.c_str());
        }
    }

    // 6. 如果设备型号匹配失败，尝试 SoC ID 匹配
    if (!soc && !id.empty()) {
        soc = SoCDatabase::find(id);
        if (soc) {
            LOGI("SoC Match: %s", soc->name.c_str());
        }
    }

    // 7. 应用 SoC 配置
    if (soc) {
        prof_.soc_name = soc->name;
        prof_.manufacturer = soc->manufacturer;
        prof_.architecture = soc->architecture;
        prof_.microarch = soc->microarch;
        prof_.is_all_big = soc->is_all_big;
        prof_.mig_threshold = soc->mig_threshold;
        prof_.thermal_limit = soc->thermal_limit;
        prof_.fas_sensitivity = soc->fas_sensitivity;
        prof_.min_freq_khz = soc->min_freq_khz;
        LOGI("DB Match: %s | Manufacturer: %s | Arch: %s | Microarch: %s | FAS=%.2f | Mig=%u | Therm=%d°C | MinFreq=%u kHz | LB=%s",
             soc->name.c_str(), soc->manufacturer.c_str(), soc->architecture.c_str(),
             soc->microarch.c_str(), soc->fas_sensitivity, soc->mig_threshold,
             soc->thermal_limit, soc->min_freq_khz, soc->is_all_big ? "OFF" : "ON");
    } else {
        LOGW("SoC not in DB, applying safe fallback.");
    }

    // 8. 拓扑动态校准
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

    LOGI("HW Ready: %s | Manufacturer: %s | Device: %s | Cores=%d",
         prof_.soc_name.c_str(), prof_.manufacturer.c_str(),
         prof_.device_model.c_str(), prof_.total_cores);
    return true;
}

} // namespace hp::device