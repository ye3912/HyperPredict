#include "device/migration_engine.h"
#include "device/hardware_analyzer.h"
#include "core/logger.h"
#include <algorithm>

namespace hp::device {

void MigrationEngine::update(int cpu, uint32_t util, uint32_t rq) noexcept {
    if (cpu < 0 || cpu >= 8) return;
    auto& l = loads_[cpu];
    // EMA 更新：3/4 衰减 + 1/4 新值
    l.u = l.u * 3 / 4 + util / 4;
    l.r = l.r * 3 / 4 + rq / 4;
}

MigResult MigrationEngine::decide(int cur, uint32_t therm, bool game) noexcept {
    MigResult r;
    r.target = cur;
    r.go = false;
    r.thermal = false;
    
    // 1️⃣ 温控紧急降级 (最高优先级)
    if (therm < 5) {
        r.thermal = true;
        r.go = true;
        // 优先迁移到能效核 (MID/LITTLE)
        for (int i = 0; i < 8; ++i) {
            if (prof_.roles[i] <= CoreRole::MID) {
                r.target = i;
                break;
            }
        }
        // 如果全是 PRIME/BIG，选频率最低的
        if (r.target == cur) {
            uint32_t min_freq = ~0u;
            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] < CoreRole::PRIME) {
                    // 这里可以调用 freq_manager 获取实时频率
                    if (min_freq > 0) { // 简化：假设索引小=频率低
                        min_freq = i;
                        r.target = i;
                    }
                }
            }
        }
        cool_ = 4; // 温控后缩短冷却期
        return r;
    }
    
    // 2️⃣ 冷却期检查    if (cool_ > 0) {
        cool_--;
        return r;
    }
    
    // 3️⃣ 获取当前负载 (归一化 0.0~1.0)
    float util_norm = static_cast<float>(loads_[cur].u) / 1024.f;
    uint32_t rq = loads_[cur].r;
    
    // 4️⃣ 读取硬件配置
    const auto& prof = hw_.profile();
    bool enable_lb = prof.enable_lb;
    uint32_t mig_thresh = prof.mig_threshold; // 0~1024 scale
    
    // 5️⃣ ✅ 分级迁移策略
    // ── 轻负载 (< 40% 或 rq=0) ──
    if (util_norm < 0.40f && rq == 0) {
        if (enable_lb) {
            // 优先留在/迁移到能效核 (MID/LITTLE)
            if (prof_.roles[cur] >= CoreRole::BIG) {
                for (int i = 0; i < 8; ++i) {
                    if (prof_.roles[i] <= CoreRole::MID && loads_[i].r < 2) {
                        r.target = i;
                        r.go = true;
                        break;
                    }
                }
            }
        }
    }
    // ── 中负载 (40%~70%) ──
    else if (util_norm < 0.70f) {
        if (enable_lb) {
            // 在 BIG/MID 之间均衡，避免频繁迁移
            if (rq > 2 || util_norm > 0.60f) {
                // 找负载较轻的同级或更高一级核心
                for (int i = 0; i < 8; ++i) {
                    if (i == cur) continue;
                    if (prof_.roles[i] >= prof_.roles[cur] && loads_[i].r < rq) {
                        r.target = i;
                        r.go = true;
                        break;
                    }
                }
            }
        }
    }
    // ── 重负载 (> 70%) 或游戏场景 ──
    else if (util_norm >= 0.70f || game || rq >= 3) {
        // 强制迁移到 PRIME/BIG 核心        if (prof_.roles[cur] < CoreRole::BIG) {
            for (int i = 7; i >= 0; --i) {
                if (prof_.roles[i] >= CoreRole::BIG && loads_[i].r < 3) {
                    r.target = i;
                    r.go = true;
                    break;
                }
            }
        }
    }
    
    // 6️⃣ 全大核架构特殊处理 (如 SM8850/9300)
    if (prof.is_all_big && enable_lb) {
        // 降低迁移门槛，但保持能效导向
        if (util_norm < 0.30f && prof_.roles[cur] == CoreRole::PRIME) {
            // 轻负载从超大核迁移到性能核
            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] == CoreRole::BIG && loads_[i].r < 2) {
                    r.target = i;
                    r.go = true;
                    break;
                }
            }
        }
    }
    
    // 7️⃣ 迁移后设置冷却期 (动态调整)
    if (r.go) {
        // 全大核架构冷却期稍短，允许更灵活的调度
        cool_ = prof.is_all_big ? 6 : 8;
        
        // 日志 (每 20 次输出一次)
        static int log_cnt = 0;
        if (++log_cnt % 20 == 0) {
            LOGD("Mig: CPU%d→%d | Util=%.1f%% | RQ=%u | Therm=%u | Game=%d",
                 cur, r.target, util_norm * 100.f, rq, therm, game ? 1 : 0);
        }
    }
    
    return r;
}

} // namespace hp::device