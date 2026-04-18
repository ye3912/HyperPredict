#include "device/migration_engine.h"
#include "core/logger.h"
#include <algorithm>
#include <cstring>

namespace hp::device {

// 智能迁移引擎 - 动态负载均衡 + 开销保护 + 老旧设备优化

// 老旧设备 (865及以前) 的迁移阈值
namespace LegacyThresh {
    static constexpr uint32_t LITTLE_TO_MID_UTIL = 320;   // 小核→中核阈值 (31%)
    static constexpr uint32_t MID_TO_LITTLE_UTIL = 192;   // 中核→小核阈值 (19%)
    static constexpr uint32_t MID_TO_BIG_UTIL = 512;      // 中核→大核阈值 (50%)
    static constexpr uint32_t LITTLE_COOL = 8;            // 冷却期延长
    static constexpr uint32_t MID_COOL = 6;
}

// 全大核设备 (8 Elite, 9400等) 的迁移阈值
namespace AllBigThresh {
    static constexpr uint32_t LOW_UTIL = 256;     // 低负载阈值 (25%)
    static constexpr uint32_t HIGH_UTIL = 512;    // 高负载阈值 (50%)
    static constexpr uint32_t RQ_THRESHOLD = 2;   // 运行队列阈值
    static constexpr uint32_t MIGRATION_COOL = 4; // 冷却期 (更短，更灵活)
}

// 现代设备的迁移阈值
namespace ModernThresh {
    static constexpr uint32_t LITTLE_TO_MID_UTIL = 384;   // 小核→中核阈值
    static constexpr uint32_t MID_TO_LITTLE_UTIL = 128;   // 中核→小核阈值
    static constexpr uint32_t MID_TO_BIG_UTIL = 512;      // 中核→大核阈值
    static constexpr uint32_t LITTLE_COOL = 6;
    static constexpr uint32_t MID_COOL = 4;
}

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
        cool_ = LegacyThresh::LITTLE_COOL;
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
    CoreRole cur_role = prof_.roles[cur];
    
    // 计算程序开销阈值
    static constexpr uint32_t MIGRATION_COST_US = 500;
    
    // ================== 4. 游戏模式: 直接绑定大核 ==================
    if (is_game) {
        if (cur_role < CoreRole::BIG) {
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
    
    // ================== 5. 老旧设备优化 (865及以前) ==================
    // 老旧设备没有超大核，中核就是高性能核
    // 策略: 小核↔中核迁移，避免频繁唤醒大核
    if (is_legacy_) {
        // --- 老旧设备: 小核 → 中核 (轻负载时) ---
        if (cur_role == CoreRole::LITTLE && util > LegacyThresh::LITTLE_TO_MID_UTIL) {
            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] == CoreRole::MID && 
                    loads_[i].util < util &&
                    loads_[i].run_queue < run_queue + 2) {
                    uint32_t save = estimate_power_savings(cur, i, util);
                    if (save > MIGRATION_COST_US || util > LegacyThresh::LITTLE_TO_MID_UTIL + 64) {
                        r.target = i;
                        r.go = true;
                        cool_ = LegacyThresh::LITTLE_COOL;
                        LOGD("Mig[Legacy]: LITTLE->MID CPU%d->%d util=%u", cur, i, util);
                        return r;
                    }
                }
            }
        }
        
        // --- 老旧设备: 中核 → 小核 (负载降低时) ---
        if (cur_role == CoreRole::MID && util < LegacyThresh::MID_TO_LITTLE_UTIL && run_queue < 2) {
            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] == CoreRole::LITTLE && 
                    loads_[i].util < 128 &&
                    loads_[i].run_queue < 2) {
                    r.target = i;
                    r.go = true;
                    cool_ = LegacyThresh::MID_COOL;
                    LOGD("Mig[Legacy]: MID->LITTLE CPU%d->%d util=%u", cur, i, util);
                    return r;
                }
            }
        }
        
        // --- 老旧设备: 中核 → 大核 (高负载时) ---
        if (cur_role == CoreRole::MID && util > LegacyThresh::MID_TO_BIG_UTIL) {
            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] >= CoreRole::BIG && loads_[i].util < util) {
                    r.target = i;
                    r.go = true;
                    cool_ = 4;
                    return r;
                }
            }
        }
    }
    
    // ================== 6. 全大核设备优化 (8 Elite, 9400等) ==================
    // 全大核没有小核，所有核心都是高性能核心
    // 策略: 更激进的负载均衡，充分利用所有核心
    if (is_all_big_) {
        // 轻负载: 允许任何核心处理
        if (util < AllBigThresh::LOW_UTIL && run_queue < AllBigThresh::RQ_THRESHOLD) {
            // 不迁移，让调度器自由选择
            return r;
        }
        
        // 高负载: 负载均衡
        if (util > AllBigThresh::HIGH_UTIL || run_queue > AllBigThresh::RQ_THRESHOLD) {
            // 找负载最轻的核心
            uint32_t min_load = util;
            int target_cpu = cur;
            for (int i = 0; i < 8; ++i) {
                if (i == cur) continue;
                uint32_t total_load = loads_[i].util + loads_[i].run_queue * 128;
                if (total_load < min_load) {
                    min_load = total_load;
                    target_cpu = i;
                }
            }
            if (target_cpu != cur) {
                r.target = target_cpu;
                r.go = true;
                cool_ = AllBigThresh::MIGRATION_COOL;
                LOGD("Mig[AllBig]: CPU%d->%d util=%u", cur, target_cpu, util);
                return r;
            }
        }
    }
    
    // ================== 6. 现代设备: 轻负载保护 ==================
    // 轻负载判断: util < 128 (12.5%) 且 run_queue < 2
    if (util < 128 && run_queue < 2 && cur_role <= CoreRole::MID) {
        return r;
    }
    
    // ================== 7. 现代设备: 大核下沉 ==================
    // 如果当前在大核且负载不高，检查是否应该下沉到中核
    if (cur_role >= CoreRole::BIG && util < 384) {
        for (int i = 0; i < 8; ++i) {
            if (prof_.roles[i] < CoreRole::BIG && 
                loads_[i].util < util &&
                loads_[i].run_queue < run_queue) {
                uint32_t estimated_save = estimate_power_savings(cur, i, util);
                if (estimated_save > MIGRATION_COST_US) {
                    r.target = i;
                    r.go = true;
                    cool_ = 6;
                    return r;
                }
            }
        }
    }
    
    // ================== 8. 现代设备: 小核上浮 ==================
    // 小核负载过高，上浮到中核
    if (cur_role <= CoreRole::MID && util > 384) {
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
    
    // ================== 9. 高负载: 负载均衡 ==================
    if (run_queue > 3) {
        for (int i = 0; i < 8; ++i) {
            if (i == cur) continue;
            // 优先同级核心
            if (prof_.roles[i] == cur_role && 
                loads_[i].run_queue < run_queue) {
                r.target = i;
                r.go = true;
                break;
            }
        }
    }
    
    // ================== 10. 设置冷却期 ==================
    if (r.go) {
        cool_ = is_legacy_ ? LegacyThresh::LITTLE_COOL : 6;
        static int log_cnt = 0;
        if (++log_cnt % 30 == 0) {
            LOGD("Mig: CPU%d→%d | Util=%u | RQ=%u | Legacy=%s",
                 cur, r.target, util, run_queue, is_legacy_ ? "true" : "false");
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