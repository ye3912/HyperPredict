#include "device/soc_database.h"
#include <algorithm>
#include <cctype>

namespace hp::device {

std::unordered_map<std::string, SoCProfile> SoCDatabase::db;
bool SoCDatabase::loaded = false;

static std::string toUpper(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::toupper);
    return r;
}

bool SoCDatabase::load() noexcept {
    if (loaded) return true;

    // 参数: name, prime, big, little, max_freq(kHz), thermal_limit, fas_sensitivity, mig_threshold, is_all_big

    // ────────── Snapdragon 8 Elite Gen 5 / SM8850 ──────────
    // ✅ 优化：启用负载均衡，降低迁移阈值，让任务优先使用 6 个性能核
    db["SM8850"] = {"Snapdragon 8 Elite Gen 5", 2,6,0, 4600000, 88, 1.9f, 580, false};
    
    // ────────── Snapdragon 8 Elite / SM8750 ──────────
    db["SM8750"] = {"Snapdragon 8 Elite", 2,6,0, 4320000, 88, 1.7f, 600, false};

    // ────────── Snapdragon 8 Gen 3 / SM8650 ──────────
    db["SM8650"] = {"Snapdragon 8 Gen 3", 1,5,2, 3300000, 85, 1.4f, 600, false};
    db["SM8650-AB"] = {"Snapdragon 8 Gen 3 for Galaxy", 1,3,4, 3390000, 85, 1.5f, 610, false};

    // ────────── Snapdragon 8 Gen 2 / SM8550 ──────────
    db["SM8550"] = {"Snapdragon 8 Gen 2", 1,4,3, 3360000, 85, 1.3f, 580, false};

    // ────────── Snapdragon 8+ Gen 1 / SM8475 ──────────
    db["SM8475"] = {"Snapdragon 8+ Gen 1", 1,3,4, 3190000, 87, 1.2f, 550, false};

    // ────────── Snapdragon 8 Gen 1 / SM8450 ──────────
    db["SM8450"] = {"Snapdragon 8 Gen 1", 1,3,4, 3000000, 82, 1.1f, 540, false};

    // ────────── Snapdragon 888 / SM8350 ──────────
    db["SM8350"] = {"Snapdragon 888", 1,3,4, 2840000, 82, 1.0f, 520, false};

    // ────────── Snapdragon 865 / SM8250 ──────────
    db["SM8250"] = {"Snapdragon 865", 1,3,4, 2840000, 88, 0.9f, 500, false};

    // ────────── Snapdragon 855 / SM8150 ──────────
    db["SM8150"] = {"Snapdragon 855", 1,3,4, 2840000, 90, 0.8f, 480, false};

    // ────────── Snapdragon 7 Series ──────────
    db["SM7325"] = {"Snapdragon 778G / 782G", 1,3,4, 2400000, 87, 0.92f, 500, false};
    db["SM7450"] = {"Snapdragon 7+ Gen 2", 1,3,4, 2910000, 87, 1.15f, 540, false};
    db["SM7475"] = {"Snapdragon 7+ Gen 3", 1,3,4, 2800000, 87, 1.10f, 530, false};
    db["SM7635"] = {"Snapdragon 7s Gen 2", 0,4,4, 2400000, 88, 0.88f, 490, false};
    db["SM7675"] = {"Snapdragon 7 Gen 3", 1,3,4, 2630000, 87, 1.05f, 520, false};

    // ────────── Snapdragon 6 Series ──────────
    db["SM6375"] = {"Snapdragon 695", 2,6,0, 2200000, 88, 0.85f, 460, false};
    db["SM6450"] = {"Snapdragon 6 Gen 1", 0,4,4, 2200000, 87, 0.90f, 480, false};

    // ────────── MediaTek Dimensity ──────────
    // 天玑 8200/8300: 1+3+4 异构架构
    db["MT6891"] = {"Dimensity 8200 / 8300", 1,3,4, 3100000, 86, 1.25f, 560, false};
    // 天玑 9200/9200+: 1+3+4 异构架构
    db["MT6895"] = {"Dimensity 9200 / 9200+", 1,3,4, 3350000, 86, 1.35f, 590, false};
    // 天玑 9300/9300+: 全大核架构 (4x X4 + 4x A720, 无小核)
    // 注: is_all_big 由 hardware_analyzer 动态检测，静态配置仅供参考
    db["MT6985"] = {"Dimensity 9300 / 9300+", 0,4,4, 3250000, 85, 1.55f, 620, true};
    // 天玑 9400: 全大核架构 (1x X925 + 3x X4 + 4x A725, 无小核)
    db["MT6991"] = {"Dimensity 9400", 1,7,0, 3625000, 85, 1.70f, 650, true};

    // ────────── Huawei Kirin ──────────
    db["KIRIN9000"] = {"Kirin 9000 / 9000E", 1,3,4, 2840000, 86, 0.90f, 510, false};
    db["KIRIN9000S"] = {"Kirin 9000S", 1,3,4, 2620000, 85, 0.82f, 480, false};
    db["KIRIN9010"] = {"Kirin 9010", 1,3,4, 2750000, 85, 0.85f, 490, false};
    db["KIRIN8100"] = {"Kirin 8100", 0,4,4, 2750000, 86, 1.10f, 530, false};
    db["KIRIN8200"] = {"Kirin 8200", 1,3,4, 3100000, 86, 1.20f, 550, false};
    db["KIRIN8300"] = {"Kirin 8300", 0,4,4, 2750000, 86, 1.15f, 540, false};

    // ────────── Samsung Exynos & Google Tensor ──────────
    db["EXYNOS2100"] = {"Exynos 2100", 1,3,4, 2900000, 86, 0.95f, 520, false};
    db["EXYNOS2200"] = {"Exynos 2200", 1,3,4, 2800000, 85, 0.92f, 510, false};
    db["EXYNOS2400"] = {"Exynos 2400 / 2400b", 1,3,6, 3200000, 85, 1.15f, 580, false};
    
    db["TENSOR"] = {"Google Tensor G1", 2,2,4, 2800000, 86, 0.90f, 500, false};
    db["TENSOR2"] = {"Google Tensor G2", 2,2,4, 2850000, 86, 0.95f, 520, false};
    db["TENSOR3"] = {"Google Tensor G3", 1,4,4, 2910000, 86, 1.00f, 540, false};
    db["TENSOR4"] = {"Google Tensor G4", 1,3,4, 3100000, 86, 1.05f, 560, false};

    loaded = true;
    return true;
}

const SoCProfile* SoCDatabase::find(const std::string& id) noexcept {
    load();
    if (id.empty()) return nullptr;

    std::string cleanId = toUpper(id);
    size_t pos = cleanId.find('-');
    if (pos != std::string::npos) cleanId = cleanId.substr(0, pos);

    // 1. 精确匹配
    auto it = db.find(cleanId);
    if (it != db.end()) return &it->second;

    // 2. 前缀匹配
    for (const auto& [key, val] : db) {
        if (cleanId.find(toUpper(key)) == 0) return &val;
    }

    // 3. 关键词回退
    if (cleanId.find("KALAMA") != std::string::npos) return find("SM8650");
    if (cleanId.find("SUN") != std::string::npos) return find("SM8750");
    if (cleanId.find("DIAMOND") != std::string::npos) return find("SM8850");
    if (cleanId.find("MT689") != std::string::npos) return find("MT6895");
    if (cleanId.find("MT698") != std::string::npos) return find("MT6985");
    if (cleanId.find("MT699") != std::string::npos) return find("MT6991");
    if (cleanId.find("KIRIN") != std::string::npos) return find("KIRIN9000S");
    if (cleanId.find("TENSOR") != std::string::npos) return find("TENSOR3");

    return nullptr;
}

} // namespace hp::device