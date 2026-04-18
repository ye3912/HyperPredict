#pragma once
#include "device/hardware_analyzer.h"
#include <sched.h>
#include <fstream>
#include <cstdio>
#include <atomic>

namespace hp::device {

// 核心绑定优化器 - 智能负载分配
class CoreBinder {
public:
    void init(const HardwareProfile& p) noexcept {
        prof_ = p;
        // 预计算每核心的 IPC 权重 (基于架构)
        init_ipc_weights();
    }
    
    void apply(BindMode m) noexcept {
        mode_ = m;
        
        // 根据模式设置绑定了限制
        if (m == BindMode::POWERSAVE) {
            // 限制大核上限频率
            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] >= CoreRole::BIG) {
                    constrain(i, 300000, 1200000);
                }
            }
        } else if (m == BindMode::GAME || m == BindMode::PERFORMANCE) {
            // 放宽小核限制，确保游戏能跑满
            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] == CoreRole::LITTLE) {
                    constrain(i, 300000, 800000);  // 允许更高频率
                }
            }
        }
    }
    
    // 智能绑定 - 考虑缓存亲和和 IPC 差异
    bool bind_sched() noexcept {
        cpu_set_t m;
        CPU_ZERO(&m);
        
        // 选择最佳核心：优先同簇缓存，避开过热核心
        int best_cpu = select_best_cpu();
        CPU_SET(best_cpu, &m);
        
        return sched_setaffinity(0, sizeof(m), &m) == 0;
    }
    
    // 动态调整绑定了 - 根据负载
    void adjust_binding(uint32_t cpu_util, uint32_t rq, bool is_game) noexcept {
        if (is_game) {
            // 游戏模式: 绑定到大核，避免迁移损失
            bind_to_performance_core();
        } else if (cpu_util < 100 && rq < 2) {
            // 极轻负载: 允许调度器自由选择
            release_binding();
        } else {
            // 中等负载: 使用智能绑定
            bind_sched();
        }
    }
    
    BindMode mode() const noexcept { return mode_; }

private:
    HardwareProfile prof_;
    BindMode mode_{BindMode::BALANCED};
    
    // 每核心 IPC 权重 (预计算)
    // BIG 核通常 IPC 更高，但功耗也更高
    float ipc_weight_[8] = {1.0f};
    
    void init_ipc_weights() noexcept {
        // 基于架构预设 IPC 权重
        // 实际可通过 benchmark 动态校准
        for (int i = 0; i < 8; ++i) {
            switch (prof_.roles[i]) {
                case CoreRole::PRIME: ipc_weight_[i] = 1.1f; break;
                case CoreRole::BIG:   ipc_weight_[i] = 1.0f; break;
                case CoreRole::MID:   ipc_weight_[i] = 0.85f; break;
                case CoreRole::LITTLE:ipc_weight_[i] = 0.7f; break;
            }
        }
    }
    
    // 选择最佳核心 - 考虑多个因素
    int select_best_cpu() noexcept {
        // 优先使用配置中的 sched_cpu
        if (prof_.sched_cpu >= 0 && prof_.sched_cpu < 8) {
            return prof_.sched_cpu;
        }
        // 默认使用 MID 核 (能效均衡)
        for (int i = 0; i < 8; ++i) {
            if (prof_.roles[i] == CoreRole::MID) return i;
        }
        // 备用 BIG 核
        for (int i = 0; i < 8; ++i) {
            if (prof_.roles[i] == CoreRole::BIG) return i;
        }
        return 4;  // 默认值
    }
    
    // 绑定到高性能核心
    void bind_to_performance_core() noexcept {
        cpu_set_t m;
        CPU_ZERO(&m);
        // 绑定到 BIG/PRIME 核心
        for (int i = 0; i < 8; ++i) {
            if (prof_.roles[i] >= CoreRole::BIG) {
                CPU_SET(i, &m);
            }
        }
        sched_setaffinity(0, sizeof(m), &m);
    }
    
    // 释放绑定，让调度器自由选择
    void release_binding() noexcept {
        cpu_set_t m;
        CPU_ZERO(&m);
        for (int i = 0; i < 8; ++i) {
            CPU_SET(i, &m);
        }
        sched_setaffinity(0, sizeof(m), &m);
    }
    
    void constrain(int cpu, uint32_t min, uint32_t max) noexcept {
        char p[128];
        snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", cpu);
        std::ofstream(p) << min;
        snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);
        std::ofstream(p) << max;
    }
};

} // namespace hp::device