#pragma once
#include "device/cpu_topology.h"
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace hp::device {

// =============================================================================
// 单簇策略 — 每个频率域一个策略实例
// =============================================================================
struct ClusterPolicy {
    int first_cpu{-1};          // 该簇起始 CPU 编号
    int num_cores{0};           // 簇内核心数
    uint32_t max_freq_khz{0};   // 最大频率
    uint32_t min_freq_khz{0};   // 最小频率
    uint32_t capacity{0};       // sched domain capacity (0-1024)
    float power_coeff{1.0f};    // 相对功耗系数 (little=1.0)

    // 迁移阈值 (基于 capacity_ratio 自适应计算)
    uint8_t migrate_out_pct{70};  // 迁出阈值 (0-100% of 1024)
    uint8_t migrate_in_pct{25};   // 迁回阈值
    uint8_t overload_pct{75};     // 过载强制迁出阈值

    bool is_valid() const noexcept { return num_cores > 0 && max_freq_khz > 0; }
};

// =============================================================================
// 簇拓扑 — 描述设备的完整簇结构
// =============================================================================
class ClusterTopology {
public:
    // 从 sysfs 检测簇拓扑
    bool detect() noexcept {
        CpuTopology topo;
        if (!topo.detect()) return false;

        clusters_.clear();
        for (const auto& d : topo.get_domains()) {
            if (d.cpus.empty()) continue;
            ClusterPolicy cp;
            cp.first_cpu = d.cpus[0];
            cp.num_cores = static_cast<int>(d.cpus.size());
            cp.max_freq_khz = d.max_freq;
            cp.min_freq_khz = d.min_freq > 0 ? d.min_freq : d.max_freq / 4;
            cp.capacity = d.capacity;
            clusters_.push_back(cp);
        }

        if (clusters_.empty()) return false;

        // 计算各簇的相对功耗系数 (以最弱簇为基准 1.0)
        uint32_t base_cap = clusters_.back().capacity;  // 最弱簇
        if (base_cap == 0) base_cap = clusters_.back().max_freq_khz / 1000;
        for (auto& c : clusters_) {
            uint32_t cap = c.capacity > 0 ? c.capacity : c.max_freq_khz / 1000;
            c.power_coeff = base_cap > 0 ?
                std::max(1.0f, static_cast<float>(cap) / static_cast<float>(base_cap)) :
                1.0f;
        }

        // 计算自适应迁移阈值
        compute_adaptive_thresholds();

        return true;
    }

    // 获取所有簇策略（按性能降序）
    const std::vector<ClusterPolicy>& policies() const noexcept { return clusters_; }

    // 获取指定 CPU 所在的簇索引
    int cluster_for_cpu(int cpu) const noexcept {
        for (size_t i = 0; i < clusters_.size(); i++) {
            if (clusters_[i].is_valid() &&
                cpu >= clusters_[i].first_cpu &&
                cpu < clusters_[i].first_cpu + clusters_[i].num_cores) {
                return static_cast<int>(i);
            }
        }
        return 0;  // fallback to first cluster
    }

    // 返回总核心数
    int total_cores() const noexcept {
        int sum = 0;
        for (const auto& c : clusters_) sum += c.num_cores;
        return sum;
    }

    // 最高性能簇索引 (第 0 个)
    int perf_cluster() const noexcept { return 0; }

    // 最高能效簇索引 (最后一个)
    int efficiency_cluster() const noexcept {
        return static_cast<int>(clusters_.size()) - 1;
    }

    // 簇数量
    int count() const noexcept { return static_cast<int>(clusters_.size()); }

    // 清空
    void reset() noexcept { clusters_.clear(); }

private:
    std::vector<ClusterPolicy> clusters_;

    // 基于 capacity 比例计算各簇的迁移阈值
    void compute_adaptive_thresholds() noexcept {
        if (clusters_.size() < 2) return;  // 单簇无需迁移

        // 基准：以次强簇与最强簇的 capacity 比例决定阈值
        for (size_t i = 0; i < clusters_.size(); i++) {
            auto& cluster = clusters_[i];
            float base_cap = static_cast<float>(cluster.capacity > 0 ?
                cluster.capacity : cluster.max_freq_khz / 1000);

            // 寻找更强的簇 (更低的索引 = 更高性能)
            if (i > 0) {
                // 存在更高的簇, 高簇 capacity_factor = higher_cap / base_cap
                uint32_t higher_cap = clusters_[i - 1].capacity > 0 ?
                    clusters_[i - 1].capacity : clusters_[i - 1].max_freq_khz / 1000;
                float cap_ratio = base_cap > 0 ? static_cast<float>(higher_cap) / base_cap : 1.0f;

                // 迁出阈值 = 70% / cap_ratio (加速比越大的高簇, 越早迁入)
                cluster.migrate_out_pct = static_cast<uint8_t>(
                    std::clamp(70.0f / cap_ratio, 20.0f, 85.0f));
            }

            // 寻找更弱的簇 (更高的索引 = 更高效)
            if (i < clusters_.size() - 1) {
                uint32_t lower_cap = clusters_[i + 1].capacity > 0 ?
                    clusters_[i + 1].capacity : clusters_[i + 1].max_freq_khz / 1000;
                float cap_ratio = lower_cap > 0 ? base_cap / lower_cap : 1.0f;

                // 迁回阈值 = 25% / cap_ratio (加速比越大的簇, 利用率越低就迁回)
                cluster.migrate_in_pct = static_cast<uint8_t>(
                    std::clamp(25.0f / cap_ratio, 10.0f, 50.0f));
            }

            // 过载阈值: 所有簇相同
            cluster.overload_pct = 75;
        }
    }
};

} // namespace hp::device
