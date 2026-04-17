#include "device/migration_engine.h"
#include "core/logger.h"
#include <algorithm>

namespace hp::device {

void MigrationEngine::update(int cpu, uint32_t util, uint32_t rq) noexcept {
    if (cpu < 0 || cpu >= 8) return;
    auto& l = loads_[cpu];
    // EMA 更新：3/4 历史 + 1/4 新值，平滑负载波动
    l.u = l.u * 3 / 4 + util / 4;
    l.r = l.r * 3 / 4 + rq / 4;
}

MigResult MigrationEngine::decide(int cur, uint32_t therm, bool game) noexcept {
    MigResult r;
    r.target = cur;
    r.go = false;
    r.thermal = false;
    
    // ── 1️⃣ 温控紧急降级 (最高优先级) ──
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
        // 如果全是 PRIME/BIG，选索引最小的（通常频率较低）
        if (r.target == cur) {
            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] < CoreRole::PRIME) {
                    r.target = i;
                    break;
                }
            }
        }
        cool_ = 4; // 温控后缩短冷却期，快速响应温度变化
        return r;
    }
    
    // ── 2️⃣ 冷却期检查 ──
    if (cool_ > 0) {
        cool_--;
        return r;
    }
    
    // ── 3️⃣ 获取当前负载 (归一化) ──
    float util_norm = static_cast<float>(loads_[cur].u) / 1024.f;
    uint32_t rq = loads_[cur].r;
    
    // ── 4️⃣ 读取硬件配置 ──
    bool enable_lb = prof_.enable_lb;
    uint32_t mig_thresh = prof_.mig_threshold;  // 0~1024 scale
    
    // ── 5️⃣ 负载均衡触发检查 ──
    if (!enable_lb) {
        return r;
    }
    
    // ── 6️⃣ 全大核架构特殊处理 (如 SM8250/SM8350/MT6983) ──
    if (prof_.is_all_big) {
        // 轻负载 (< 30%): 迁移到低频核心省电
        if (util_norm < 0.30f && prof_.roles[cur] >= CoreRole::BIG) {
            for (int i = 7; i >= 0; --i) {
                if (prof_.roles[i] < prof_.roles[cur] && loads_[i].r < 2) {
                    r.target = i;
                    r.go = true;
                    break;
                }
            }
        }
        // 重负载 (> 75%): 迁移到高频核心
        else if (util_norm > 0.75f && prof_.roles[cur] < CoreRole::BIG) {
            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] > prof_.roles[cur] && loads_[i].r < 3) {
                    r.target = i;
                    r.go = true;
                    break;
                }
            }
        }
        // 中等负载: 同级均衡
        else if (rq > 2) {
            for (int i = 0; i < 8; ++i) {
                if (i == cur) continue;
                if (prof_.roles[i] == prof_.roles[cur] && loads_[i].r < rq) {
                    r.target = i;
                    r.go = true;
                    break;
                }
            }
        }
        
        if (r.go) {
            cool_ = 6;  // 全大核冷却期较短
        }
        return r;
    }
    
    // ── 7️⃣ 异构架构 (LITTLE/MID/BIG) 迁移策略 ──
    
    // ▸ 轻负载 (< 40% 或 rq=0): 优先能效核
    if (util_norm < 0.40f && rq < 2) {
        // 如果在 BIG/PRIME 上，尝试迁移到 MID/LITTLE
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
    // ▸ 中负载 (40%~70%): 同级均衡
    else if (util_norm < 0.70f) {
        if (rq > mig_thresh / 256) {  // 归一化阈值
            for (int i = 0; i < 8; ++i) {
                if (i == cur) continue;
                // 找同级或高一级核心，且负载更轻
                if (prof_.roles[i] >= prof_.roles[cur] && loads_[i].r < rq) {
                    r.target = i;
                    r.go = true;
                    break;
                }
            }
        }
    }
    // ▸ 重负载 (≥70%) 或游戏: 强制高性能核
    else if (util_norm >= 0.70f || game || rq >= 3) {
        if (prof_.roles[cur] < CoreRole::BIG) {
            for (int i = 7; i >= 0; --i) {
                if (prof_.roles[i] >= CoreRole::BIG && loads_[i].r < 3) {
                    r.target = i;
                    r.go = true;
                    break;
                }
            }
        }
    }
    
    // ── 8️⃣ 设置冷却期 ──
    if (r.go) {
        cool_ = 8;  // 普通架构冷却期
        
        // 调试日志 (每 20 次输出一次)
        static int log_cnt = 0;
        if (++log_cnt % 20 == 0) {
            LOGD("Mig: CPU%d→%d | Util=%.1f%% | RQ=%u | Therm=%u | Game=%d | LB=%s",
                 cur, r.target, util_norm * 100.f, rq, therm, game ? 1 : 0,
                 enable_lb ? "ON" : "OFF");
        }
    }
    
    return r;
}

} // namespace hp::device