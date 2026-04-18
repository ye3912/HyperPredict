#include "device/migration_engine.h"
#include "core/logger.h"
#include <algorithm>

namespace hp::device {

// 智能迁移引擎 - 动态负载均衡 + 开销保护
void MigrationEngine::update(int cpu, uint32_t util, uint32_t rq) noexcept {
    if (cpu < 0 || cpu >= 8) return;
    auto& l = loads_[cpu];
    // EMA 更新：3/4 历史 + 1/4 新值，平滑负载波动
    l.util = l.util * 3 / 4 + util / 4;
    l.run_queue = l.run_queue * 3 / 4 + rq / 4;
}

MigResult MigrationEngine::decide(int cur, uint32_t therm, bool is_game) noexcept {
    MigResult r;
    r.target = cur;
    r.go = false;
    r.thermal = false;
    
    // ================== 1. 温控紧急降级 (最高优先级) ==================
    if (therm < 5) {
        r.thermal = true;
        r.go = true;
        // 迁移到低功耗核心
        for (int i = 0; i < 8; ++i) {
            if (prof_.roles[i] <= CoreRole::MID) {
                r.target = i;
                break;
            }
        }
        if (r.target == cur) {
            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] < CoreRole::PRIME) {
                    r.target = i;
                    break;
                }
            }
        }
        cool_ = 4;
        return r;
    }
    
    // ================== 2. 冷却期检查 ==================
    if (cool_ > 0) {
        cool_--;
        return r;
    }
    
    // ================== 3. 获取当前状态 ==================
    float util_norm = static_cast<float>(loads_[cur].util) / 1024.f;
    uint32_t run_queue = loads_[cur].run_queue;
    uint32_t util = loads_[cur].util;
    
    // 计算程序开销阈值
    // 避免频繁迁移的开销: 迁移成本 ≈ 0.5ms
    // 如果轻负载运行在小核的开销 < 0.5ms，则不需要迁移
    static constexpr uint32_t MIGRATION_COST_US = 500;  // 迁移开销 (微秒)
    
    // ================== 4. 游戏模式: 直接绑定大核 ==================
    if (is_game) {
        // 游戏强制高性能模式，关闭迁移
        if (prof_.roles[cur] < CoreRole::BIG) {
            for (int i = 7; i >= 0; --i) {
                if (prof_.roles[i] >= CoreRole::BIG && loads_[i].run_queue < 4) {
                    r.target = i;
                    r.go = true;
                    break;
                }
            }
        }
        cool_ = is_game ? 2 : 6;
        return r;
    }
    
    // ================== 5. 轻负载保护 (避免小核开销) ==================
    // 轻负载判断: util < 128 (12.5%) 且 run_queue < 2
    // 这种情况下让调度器自由选择，不强制迁移
    if (util < 128 && run_queue < 2 && prof_.roles[cur] <= CoreRole::MID) {
        // 轻负载运行在中低核心是合理的，不需要迁移
        // 如果当前已经是 LITTLE/MID，直接返回
        return r;
    }
    
    // ================== 6. 中等负载动态调整 ==================
    // util ∈ [128, 512) 即 12.5% ~ 50%
    // 这个区间需要智能判断是否迁移
    
    // 如果当前在大核且负载不高，检查是否应该下沉
    if (prof_.roles[cur] >= CoreRole::BIG && util < 384) {
        // 大核利用不足，找一个更省电的核心
        for (int i = 0; i < 8; ++i) {
            if (prof_.roles[i] < CoreRole::BIG && 
                loads_[i].util < util &&
                loads_[i].run_queue < run_queue) {
                // 预估迁移收益
                uint32_t estimated_save = estimate_power_savings(cur, i, util);
                // 如果节省 > 迁移开销，则迁移
                if (estimated_save > MIGRATION_COST_US) {
                    r.target = i;
                    r.go = true;
                    cool_ = 6;
                    return r;
                }
            }
        }
    }
    
    // 小核负载过高，上浮到大核
    if (prof_.roles[cur] <= CoreRole::MID && util > 384) {
        for (int i = 0; i < 8; ++i) {
            if (prof_.roles[i] >= CoreRole::BIG && 
                loads_[i].util < util &&
                loads_[i].run_queue < run_queue + 2) {
                r.target = i;
                r.go = true;
                cool_ = 4;
                return r;
            }
        }
    }
    
    // ================== 7. 高负载: 负载均衡 ==================
    if (run_queue > 3) {
        // 查找负载最轻的同级核心
        for (int i = 0; i < 8; ++i) {
            if (i == cur) continue;
            // 优先同级核心，避免跨簇迁移
            if (prof_.roles[i] == prof_.roles[cur] && 
                loads_[i].run_queue < run_queue) {
                r.target = i;
                r.go = true;
                break;
            }
        }
    }
    
    // ================== 8. 设置冷却期 ==================
    if (r.go) {
        cool_ = 6;
        static int log_cnt = 0;
        if (++log_cnt % 30 == 0) {
            LOGD("Mig: CPU%d→%d | Util=%u(+%u) | RQ=%u | LB=%s",
                 cur, r.target, util, util_norm > 0.5f ? 1 : 0, run_queue,
                 prof_.enable_lb ? "ON" : "OFF");
        }
    }
    
    return r;
}

// 估算迁移节省的功耗 (微秒当量)
uint32_t MigrationEngine::estimate_power_savings(int from_cpu, int to_cpu, uint32_t util) noexcept {
    // 基于核心类型计算
    auto from_role = prof_.roles[from_cpu];
    auto to_role = prof_.roles[to_cpu];
    
    // 如果从省电核心迁移到费电核心，检查是否有收益
    if (from_role <= CoreRole::MID && to_role >= CoreRole::BIG) {
        // 这种情况可能更耗电，不迁移
        return 0;
    }
    
    // 大核→小核: 潜在省电
    if (from_role >= CoreRole::BIG && to_role <= CoreRole::MID) {
        // 根据负载预估节省
        // 重负载在大核更高效，轻负载在小核更省电
        if (util < 256) {
            return 1000;  // 轻负载迁移预估节省
        } else if (util < 512) {
            return 500;   // 中等负载
        }
    }
    
    return 0;
}

void MigrationEngine::reset_stats() noexcept {
    for (auto& t : trend_cache_) {
        t = {};
    }
}

} // namespace hp::device