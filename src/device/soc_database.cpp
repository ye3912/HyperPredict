#include "device/soc_database.h"
#include <algorithm>
#include <cctype>

namespace hp::device {

std::unordered_map<std::string, SoCProfile> SoCDatabase::db;
bool SoCDatabase::loaded = false;

// 辅助：转大写用于模糊匹配
static std::string toUpper(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::toupper);
    return r;
}

bool SoCDatabase::load() noexcept {
    if (loaded) return true;

    // 参数定义:
    // name, prime, big, little, max_freq(kHz), thermal_limit(°C), fas_sensitivity, mig_threshold, is_all_big
    // 调参逻辑: 制程越老/积热越严重 → thermal_limit越低, fas越保守; 全大核/高频 → mig阈值提高防震荡

    // ────────── Snapdragon 7 & 6 Series (2019-2024) ──────────
    db["SM7125"] = {"Snapdragon 730G", 1,1,6, 2200000, 88, 0.75f, 450, false};
    db["SM7150"] = {"Snapdragon 750G", 2,6,0, 2300000, 88, 0.78f, 460, false};
    db["SM7250"] = {"Snapdragon 765G", 1,1,6, 2400000, 88, 0.80f, 470, false};
    db["SM7325"] = {"Snapdragon 778G / 782G", 1,3,4, 2400000, 87, 0.92f, 500, false};
    db["SM7450"] = {"Snapdragon 7+ Gen 2", 1,3,4, 2910000, 87, 1.15f, 540, false};
    db["SM7475"] = {"Snapdragon 7+ Gen 3", 1,3,4, 2800000, 87, 1.10f, 530, false};
    db["SM7635"] = {"Snapdragon 7s Gen 2", 0,4,4, 2400000, 88, 0.88f, 490, false};
    db["SM7675"] = {"Snapdragon 7 Gen 3", 1,3,4, 2630000, 87, 1.05f, 520, false};

    db["SM6115"] = {"Snapdragon 662", 0,2,6, 2000000, 89, 0.65f, 400, false};
    db["SM6150"] = {"Snapdragon 675 / 678", 2,6,0, 2000000, 89, 0.68f, 410, false};
    db["SM6225"] = {"Snapdragon 680 / 685", 0,4,4, 2400000, 88, 0.72f, 430, false};
    db["SM6375"] = {"Snapdragon 695", 2,6,0, 2200000, 88, 0.85f, 460, false};
    db["SM6450"] = {"Snapdragon 6 Gen 1", 0,4,4, 2200000, 87, 0.90f, 480, false};
    db["SM6475"] = {"Snapdragon 6s Gen 3", 0,4,4, 2600000, 87, 1.00f, 510, false};

    // ────────── MediaTek Dimensity (2019-2024) ──────────
    db["MT6885"] = {"Dimensity 1000 / 1000+ / 1000L", 0,4,4, 2600000, 87, 0.95f, 500, false};
    db["MT6877"] = {"Dimensity 810", 0,2,6, 2400000, 88, 0.80f, 440, false};
    db["MT6833"] = {"Dimensity 720 / 700", 0,2,6, 2000000, 89, 0.70f, 420, false};
    db["MT6853"] = {"Dimensity 800 / 820", 0,4,4, 2600000, 88, 0.85f, 480, false};
    
    db["MT6891"] = {"Dimensity 8200 / 8200-Ultra / 8300", 1,3,4, 3100000, 86, 1.25f, 560, false};
    db["MT6893"] = {"Dimensity 900", 2,6,0, 2600000, 88, 0.90f, 510, false};
    db["MT6895"] = {"Dimensity 9200 / 9200+", 1,3,4, 3350000, 86, 1.35f, 590, false};
    db["MT6985"] = {"Dimensity 9300 / 9300+", 0,4,4, 3250000, 85, 1.55f, 680, true};
    db["MT6991"] = {"Dimensity 9400", 0,4,4, 3625000, 85, 1.70f, 720, true};
    
// ────────── Huawei Kirin (2019-2024) ──────────
    db["KIRIN990"]  = {"Kirin 990 5G", 2,2,4, 2860000, 87, 0.85f, 490, false};
    db["KIRIN985"]  = {"Kirin 985", 1,3,4, 2580000, 87, 0.80f, 470, false};
    db["KIRIN9000"] = {"Kirin 9000 / 9000E", 1,3,4, 2840000, 86, 0.90f, 510, false};
    db["KIRIN9000S"] = {"Kirin 9000S", 1,3,4, 2620000, 85, 0.82f, 480, false};
    db["KIRIN9010"]  = {"Kirin 9010", 1,3,4, 2750000, 85, 0.85f, 490, false};
    db["KIRIN810"]   = {"Kirin 810", 2,6,0, 2270000, 88, 0.88f, 460, false};
    db["KIRIN820"]   = {"Kirin 820", 1,3,4, 2360000, 88, 0.90f, 470, false};
    db["KIRIN8000"]  = {"Kirin 8000", 0,4,4, 2400000, 87, 0.92f, 480, false};
    db["KIRIN8100"]  = {"Kirin 8100", 0,4,4, 2750000, 86, 1.10f, 530, false};
    db["KIRIN8200"]  = {"Kirin 8200", 1,3,4, 3100000, 86, 1.20f, 550, false};
    db["KIRIN8300"]  = {"Kirin 8300", 0,4,4, 2750000, 86, 1.15f, 540, false};

    // ────────── Samsung Exynos & Google Tensor (2019-2024) ──────────
    db["EXYNOS980"]  = {"Exynos 980", 2,6,0, 2200000, 87, 0.78f, 450, false};
    db["EXYNOS9825"] = {"Exynos 9825", 2,2,4, 2730000, 86, 0.85f, 480, false};
    db["EXYNOS990"]  = {"Exynos 990", 2,2,4, 2730000, 84, 0.82f, 470, false};
    db["EXYNOS2100"] = {"Exynos 2100", 1,3,4, 2900000, 86, 0.95f, 520, false};
    db["EXYNOS2200"] = {"Exynos 2200", 1,3,4, 2800000, 85, 0.92f, 510, false};
    db["EXYNOS2400"] = {"Exynos 2400 / 2400b", 1,3,6, 3200000, 85, 1.15f, 580, false};
    db["EXYNOS2500"] = {"Exynos 2500", 1,3,4, 3200000, 84, 1.20f, 590, false};

    db["TENSOR"]     = {"Google Tensor G1", 2,2,4, 2800000, 86, 0.90f, 500, false};
    db["TENSOR2"]    = {"Google Tensor G2", 2,2,4, 2850000, 86, 0.95f, 520, false};
    db["TENSOR3"]    = {"Google Tensor G3", 1,4,4, 2910000, 86, 1.00f, 540, false};
    db["TENSOR4"]    = {"Google Tensor G4", 1,3,4, 3100000, 86, 1.05f, 560, false};

    loaded = true;
    return true;
}

const SoCProfile* SoCDatabase::find(const std::string& id) noexcept {
    load();
    if (id.empty()) return nullptr;

    std::string cleanId = toUpper(id);
    // 去除常见厂商后缀/前缀干扰
    size_t pos = cleanId.find('-');
    if (pos != std::string::npos) cleanId = cleanId.substr(0, pos);

    // 1. 精确匹配
    auto it = db.find(cleanId);
    if (it != db.end()) return &it->second;

    // 2. 前缀匹配 (处理 MT6895_TMT, SM7325P, EXYNOS2400B 等)
    for (const auto& [key, val] : db) {
        if (cleanId.find(toUpper(key)) == 0) return &val;
    }

    // 3. 关键词回退 (处理 ro.board.platform 返回的内部代号)
    if (cleanId.find("KALAMA") != std::string::npos) return find("SM8650");
    if (cleanId.find("SUN")    != std::string::npos) return find("SM8750");
    if (cleanId.find("DIAMOND")!= std::string::npos) return find("SM8850");
    if (cleanId.find("LAHAINA")!= std::string::npos) return find("SM8350");
    if (cleanId.find("TARO")   != std::string::npos) return find("SM8450");
    if (cleanId.find("MAJIN")  != std::string::npos) return find("SM8475");
    if (cleanId.find("CAPI")   != std::string::npos) return find("SM7325");
    if (cleanId.find("PUJI")   != std::string::npos) return find("SM7450");
    if (cleanId.find("PARROT") != std::string::npos) return find("SM7475");
    if (cleanId.find("MT689")  != std::string::npos) return find("MT6895"); 
    if (cleanId.find("MT698")  != std::string::npos) return find("MT6985"); 
    if (cleanId.find("MT699")  != std::string::npos) return find("MT6991"); 
    if (cleanId.find("KIRIN")  != std::string::npos) return find("KIRIN9000S"); 
    if (cleanId.find("TENSOR") != std::string::npos) return find("TENSOR3");

    return nullptr;
}

} // namespace hp::device