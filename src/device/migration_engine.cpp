#include "device/migration_engine.h"
#include <algorithm>

namespace hp::device {

void MigrationEngine::update(int cpu, uint32_t util, uint32_t rq) noexcept {
    if (cpu < 0 || cpu >= 8) return;
    auto& l = loads_[cpu];
    l.u = l.u * 3 / 4 + util / 4;
    l.r = l.r * 3 / 4 + rq / 4;
}

MigResult MigrationEngine::decide(int cur, uint32_t therm, bool game) noexcept {
    MigResult r;
    r.target = cur;
    
    if (therm < 5) {
        r.thermal = true;
        r.go = true;
        for (int i = 0; i < 8; ++i) {
            if (prof_.roles[i] < CoreRole::PRIME) {
                r.target = i;
                break;
            }
        }
        return r;
    }
    
    if (cool_ > 0) {
        cool_--;
        return r;
    }
    
    float u = static_cast<float>(loads_[cur].u) / 1024.f;
    
    if ((u > 0.75f || loads_[cur].r > 3) && !game) {
        for (int i = 7; i >= 0; --i) {
            if (prof_.roles[i] >= CoreRole::BIG && loads_[i].r < 2) {
                r.target = i;
                r.go = true;
                break;
            }
        }
    } else if (u < 0.20f && loads_[cur].r == 0 && !game) {
        for (int i = 0; i < 8; ++i) {
            if (prof_.roles[i] <= CoreRole::MID && loads_[i].r < 2) {
                r.target = i;
                r.go = true;
                break;
            }
        }
    }
    
    if (r.go) cool_ = 8;
    return r;
}

} // namespace hp::device