#include "device/energy_model.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <unordered_map>
#include <string>

namespace hp::device {

// Snapdragon 8 Elite Gen 5: Prime 2x @4.6GHz + Performance 6x @3.62GHz
static const CorePowerBudget BUDGET_SM8850 = {7000, 10800, 0, 18000};
static const CorePowerBudget BUDGET_SM8750 = {6500, 10800, 0, 17000};
static const CorePowerBudget BUDGET_SM8650 = {5500, 7500, 2500, 15000};
static const CorePowerBudget BUDGET_MT6985 = {0, 14000, 0, 15000};
static const CorePowerBudget BUDGET_MT6991 = {0, 16000, 0, 17000};
static const CorePowerBudget BUDGET_DEFAULT = {6000, 10000, 3000, 16000};

static const std::unordered_map<std::string, CorePowerBudget> power_db = {
    {"SM8850", BUDGET_SM8850},
    {"SM8750", BUDGET_SM8750},
    {"SM8650", BUDGET_SM8650},
    {"MT6985", BUDGET_MT6985},
    {"MT6991", BUDGET_MT6991},
    {"DEFAULT", BUDGET_DEFAULT},
};

static std::string toUpper(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::toupper);
    return r;
}

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

const CorePowerBudget* find_power_budget(const char* soc_id) noexcept {
    if (!soc_id || soc_id[0] == '\0') {
        return &power_db.at("DEFAULT");
    }
    std::string id = toUpper(trim(soc_id));
    auto it = power_db.find(id);
    if (it != power_db.end()) {
        return &it->second;
    }
    // 前缀匹配
    for (const auto& [key, val] : power_db) {
        if (id.find(key) == 0) {
            return &val;
        }
    }
    return &power_db.at("DEFAULT");
}

float calc_edp_cost(uint32_t power_mw, float fps, float target_fps) noexcept {
    if (fps <= 0.0f || power_mw == 0) {
        return 1e9f;  // 无效返回极大值
    }
    float norm_fps = fps / target_fps;
    return static_cast<float>(power_mw) / (norm_fps * norm_fps);
}

} // namespace hp::device