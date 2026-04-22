#include "device/soc_database.h"
#include <algorithm>
#include <cctype>

namespace hp::device {

std::unordered_map<std::string, SoCProfile> SoCDatabase::db;
std::unordered_map<std::string, std::string> SoCDatabase::device_map;
bool SoCDatabase::loaded = false;

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

bool SoCDatabase::load() noexcept {
    if (loaded) return true;

    // 参数: name, manufacturer, architecture, microarch, aliases, prime, big, little, max_freq(kHz), min_freq(kHz), thermal_limit, fas_sensitivity, mig_threshold, is_all_big

    // ────────── Snapdragon 8 Elite Gen 5 / SM8850 ──────────
    // 全大核架构: 2x Prime @ 4.6GHz + 6x Performance @ 3.62GHz, 无小核
    db["SM8850"] = {"Snapdragon 8 Elite Gen 5", "Qualcomm", "ARMv9", "Oryon", {"DIAMOND"}, 2,6,0, 4600000, 300000, 88, 1.9f, 580, true};

    // ────────── Snapdragon 8 Gen 5 / SM8845 ──────────
    // 全大核架构: 2x Oryon-L @ 3.8GHz + 6x Oryon-M @ 3.32GHz, 无小核
    db["SM8845"] = {"Snapdragon 8 Gen 5", "Qualcomm", "ARMv9", "Oryon (3rd Gen)", {}, 2,6,0, 3800000, 300000, 88, 1.8f, 590, true, MigrationConfig{0, 0, 560, 0, 6, 4, 256, 0.28f, 704, 5, 1800, 1000}};

    // ────────── Snapdragon 8 Elite / SM8750 ──────────
    // 全大核架构: 2x Prime @ 4.32GHz + 6x Performance @ 3.53GHz, 无小核
    db["SM8750"] = {"Snapdragon 8 Elite", "Qualcomm", "ARMv9", "Oryon", {"SUN"}, 2,6,0, 4320000, 300000, 88, 1.7f, 600, true};

    // ────────── Snapdragon 8 Gen 3 / SM8650 ──────────
    // 功耗优化: 降低阈值，让小核 early 迁移到中核，减少大核激活
    db["SM8650"] = {"Snapdragon 8 Gen 3", "Qualcomm", "ARMv9", "Cortex-X4/A720/A520", {"PINEAPPLE"}, 1,5,2, 3300000, 300000, 85, 0.5f, 600, false, MigrationConfig{184, 160, 576, 4, 4, 4, 192, 0.25f, 640, 4, 1500, 800}};
    db["SM8650-AB"] = {"Snapdragon 8 Gen 3 for Galaxy", "Qualcomm", "ARMv9", "Cortex-X4/A720/A520", {"PINEAPPLE"}, 1,3,4, 3390000, 300000, 85, 1.0f, 600, false, MigrationConfig{184, 160, 576, 4, 4, 4, 192, 0.25f, 640, 4, 1500, 800}};

    // ────────── Snapdragon 8 Gen 2 / SM8550 ──────────
    // 平衡策略: 中等优化
    db["SM8550"] = {"Snapdragon 8 Gen 2", "Qualcomm", "ARMv9", "Cortex-X3/A715/A510", {"KALAMA"}, 1,4,3, 3360000, 300000, 85, 1.1f, 580, false, MigrationConfig{256, 224, 576, 6, 6, 4, 256, 0.28f, 704, 5, 1800, 1000}};

    // ────────── Snapdragon 8+ Gen 1 / SM8475 ──────────
    db["SM8475"] = {"Snapdragon 8+ Gen 1", "Qualcomm", "ARMv9", "Cortex-X2/A710/A510", {"Waipio Plus", "Cape Cod"}, 1,3,4, 3190000, 300000, 87, 1.0f, 550, false, MigrationConfig{256, 224, 576, 6, 6, 4, 256, 0.28f, 704, 6, 1800, 1000}};

    // ────────── Snapdragon 8 Gen 1 / SM8450 ──────────
    db["SM8450"] = {"Snapdragon 8 Gen 1", "Qualcomm", "ARMv9", "Cortex-X2/A710/A510", {"Waipio"}, 1,3,4, 3000000, 300000, 82, 1.0f, 540, false, MigrationConfig{256, 224, 576, 6, 6, 4, 256, 0.28f, 704, 6, 1800, 1000}};

    // ────────── Snapdragon 888 / SM8350 ──────────
    // 火龙888保守策略: 极早迁移到中核，减少大核激活
    db["SM8350"] = {"Snapdragon 888", "Qualcomm", "ARMv8", "Cortex-X1/A78/A55", {"Lahaina"}, 1,3,4, 2840000, 300000, 78, 0.65f, 380, false, MigrationConfig{160, 128, 448, 4, 4, 4, 160, 0.20f, 512, 4, 1500, 800}};

    // ────────── Snapdragon 865 / SM8250 ──────────
    // 经典功耗优化: 保持默认配置
    db["SM8250"] = {"Snapdragon 865", "Qualcomm", "ARMv8", "Cortex-A77/A55", {"Kona"}, 1,3,4, 2840000, 300000, 82, 0.75f, 420, false, MigrationConfig{256, 240, 640, 6, 6, 4, 256, 0.30f, 768, 6, 2000, 1000}};

    // ────────── Snapdragon 855 / SM8150 ──────────
    db["SM8150"] = {"Snapdragon 855", "Qualcomm", "ARMv8", "Cortex-A76/A55", {}, 1,3,4, 2840000, 300000, 82, 0.70f, 400, false, MigrationConfig{272, 240, 608, 6, 6, 4, 256, 0.28f, 704, 5, 1800, 1000}};

    // ────────── Snapdragon 7 Series ──────────
    db["SM7325"] = {"Snapdragon 778G / 782G", "Qualcomm", "ARMv8", "Cortex-A78/A55", {}, 1,3,4, 2400000, 200000, 87, 0.92f, 500, false, MigrationConfig{288, 256, 640, 8, 8, 6, 320, 0.35f, 832, 8, 2200, 1200}};
    db["SM7450"] = {"Snapdragon 7+ Gen 2", "Qualcomm", "ARMv8", "Cortex-A710/A510", {}, 1,3,4, 2910000, 300000, 87, 1.15f, 540, false, MigrationConfig{272, 240, 608, 6, 6, 4, 288, 0.30f, 768, 6, 2000, 1100}};
    db["SM7475"] = {"Snapdragon 7+ Gen 3", "Qualcomm", "ARMv9", "Cortex-A715/A510", {}, 1,3,4, 2800000, 300000, 87, 1.10f, 530, false, MigrationConfig{256, 224, 576, 6, 6, 4, 272, 0.28f, 704, 5, 1800, 1000}};
    db["SM7635"] = {"Snapdragon 7s Gen 2", "Qualcomm", "ARMv8", "Cortex-A78/A55", {}, 0,4,4, 2400000, 200000, 88, 0.88f, 490, false, MigrationConfig{320, 288, 704, 10, 8, 6, 384, 0.40f, 896, 10, 2800, 1500}};
    db["SM7675"] = {"Snapdragon 7 Gen 3", "Qualcomm", "ARMv9", "Cortex-A720/A520", {}, 1,3,4, 2630000, 300000, 87, 1.05f, 520, false, MigrationConfig{256, 224, 576, 6, 6, 4, 272, 0.28f, 704, 5, 1800, 1000}};

    // ────────── Snapdragon 6 Series ──────────
    db["SM6375"] = {"Snapdragon 695", "Qualcomm", "ARMv8", "Cortex-A78/A55", {}, 2,6,0, 2200000, 200000, 88, 0.85f, 460, false};
    db["SM6450"] = {"Snapdragon 6 Gen 1", "Qualcomm", "ARMv8", "Cortex-A78/A55", {}, 0,4,4, 2200000, 200000, 87, 0.90f, 480, false};

    // ────────── MediaTek Dimensity ──────────
    // 天玑 8200/8300: 1+3+4 异构架构
    db["MT6891"] = {"Dimensity 8200 / 8300", "MediaTek", "ARMv8", "Cortex-A78/A55", {}, 1,3,4, 3100000, 300000, 86, 1.25f, 560, false};
    // 天玑 9200/9200+: 1+3+4 异构架构
    db["MT6895"] = {"Dimensity 9200 / 9200+", "MediaTek", "ARMv9", "Cortex-X2/A710/A510", {}, 1,3,4, 3350000, 300000, 86, 1.35f, 590, false};
    // 天玑 9300/9300+: 全大核架构 (4x X4 + 4x A720, 无小核)
    // 注: is_all_big 由 hardware_analyzer 动态检测，静态配置仅供参考
    db["MT6985"] = {"Dimensity 9300 / 9300+", "MediaTek", "ARMv9", "Cortex-X4/A720", {}, 0,4,4, 3250000, 300000, 85, 1.55f, 620, true};
    // 天玑 9400: 全大核架构 (1x X925 + 3x X4 + 4x A725, 无小核)
    db["MT6991"] = {"Dimensity 9400", "MediaTek", "ARMv9", "Cortex-X925/X4/A725", {}, 1,7,0, 3625000, 300000, 85, 1.70f, 650, true};

    // ────────── Huawei Kirin ──────────
    db["KIRIN9000"] = {"Kirin 9000 / 9000E", "Huawei", "ARMv8", "Cortex-A77/A55", {}, 1,3,4, 2840000, 300000, 86, 0.90f, 510, false};
    db["KIRIN9000S"] = {"Kirin 9000S", "Huawei", "ARMv8", "Cortex-A77/A55", {}, 1,3,4, 2620000, 300000, 85, 0.82f, 480, false};
    db["KIRIN9010"] = {"Kirin 9010", "Huawei", "ARMv8", "Cortex-A77/A55", {}, 1,3,4, 2750000, 300000, 85, 0.85f, 490, false};
    db["KIRIN8100"] = {"Kirin 8100", "Huawei", "ARMv8", "Cortex-A78/A55", {}, 0,4,4, 2750000, 300000, 86, 1.10f, 530, false};
    db["KIRIN8200"] = {"Kirin 8200", "Huawei", "ARMv9", "Cortex-A710/A510", {}, 1,3,4, 3100000, 300000, 86, 1.20f, 550, false};
    db["KIRIN8300"] = {"Kirin 8300", "Huawei", "ARMv9", "Cortex-A715/A510", {}, 0,4,4, 2750000, 300000, 86, 1.15f, 540, false};

    // ────────── Samsung Exynos & Google Tensor ──────────
    db["EXYNOS2100"] = {"Exynos 2100", "Samsung", "ARMv8", "Cortex-X1/A78/A55", {}, 1,3,4, 2900000, 300000, 86, 0.95f, 520, false};
    db["EXYNOS2200"] = {"Exynos 2200", "Samsung", "ARMv8", "Cortex-X2/A78/A55", {}, 1,3,4, 2800000, 300000, 85, 0.92f, 510, false};
    db["EXYNOS2400"] = {"Exynos 2400 / 2400b", "Samsung", "ARMv9", "Cortex-X4/A720/A520", {}, 1,3,6, 3200000, 300000, 85, 1.15f, 580, false};

    db["TENSOR"] = {"Google Tensor G1", "Google", "ARMv8", "Cortex-X1/A78/A55", {}, 2,2,4, 2800000, 300000, 86, 0.90f, 500, false};
    db["TENSOR2"] = {"Google Tensor G2", "Google", "ARMv8", "Cortex-X1/A78/A55", {}, 2,2,4, 2850000, 300000, 86, 0.95f, 520, false};
    db["TENSOR3"] = {"Google Tensor G3", "Google", "ARMv9", "Cortex-X3/A715/A510", {}, 1,4,4, 2910000, 300000, 86, 1.00f, 540, false};
    db["TENSOR4"] = {"Google Tensor G4", "Google", "ARMv9", "Cortex-X4/A720/A520", {}, 1,3,4, 3100000, 300000, 86, 1.05f, 560, false};

    // ────────── 设备型号映射 ──────────
    device_map["pixel"] = "TENSOR";
    device_map["pixel 7"] = "TENSOR2";
    device_map["pixel 7 pro"] = "TENSOR2";
    device_map["pixel 8"] = "TENSOR3";
    device_map["pixel 8 pro"] = "TENSOR3";
    device_map["pixel 9"] = "TENSOR4";
    device_map["pixel 9 pro"] = "TENSOR4";
    device_map["pixel 9 pro xl"] = "TENSOR4";
    device_map["s23"] = "SM8550";
    device_map["s23 ultra"] = "SM8550";
    device_map["s24"] = "SM8650";
    device_map["s24 ultra"] = "SM8650-AB";
    device_map["s25"] = "SM8750";
    device_map["s25 ultra"] = "SM8850";

    loaded = true;
    return true;
}

const SoCProfile* SoCDatabase::find(const std::string& id) noexcept {
    load();
    if (id.empty()) return nullptr;

    std::string cleanId = toUpper(trim(id));
    size_t pos = cleanId.find('-');
    if (pos != std::string::npos) cleanId = cleanId.substr(0, pos);

    // 1. 精确匹配
    auto it = db.find(cleanId);
    if (it != db.end()) return &it->second;

    // 2. 别名匹配
    for (const auto& [key, val] : db) {
        for (const auto& alias : val.aliases) {
            if (cleanId == toUpper(alias)) return &val;
        }
    }

    // 3. 前缀匹配
    for (const auto& [key, val] : db) {
        if (cleanId.find(toUpper(key)) == 0) return &val;
    }

    // 4. 关键词回退
    if (cleanId.find("PINEAPPLE") != std::string::npos) return find("SM8650");
    if (cleanId.find("KALAMA") != std::string::npos) return find("SM8550");
    if (cleanId.find("Waipio Plus") != std::string::npos) return find("SM8475");
    if (cleanId.find("Cape Cod") != std::string::npos) return find("SM8475");
    if (cleanId.find("Waipio") != std::string::npos) return find("SM8450");
    if (cleanId.find("Lahaina") != std::string::npos) return find("SM8350");
    if (cleanId.find("Kona") != std::string::npos) return find("SM8250");
    if (cleanId.find("SUN") != std::string::npos) return find("SM8750");
    if (cleanId.find("DIAMOND") != std::string::npos) return find("SM8850");
    if (cleanId.find("MT689") != std::string::npos) return find("MT6895");
    if (cleanId.find("MT698") != std::string::npos) return find("MT6985");
    if (cleanId.find("MT699") != std::string::npos) return find("MT6991");
    if (cleanId.find("KIRIN") != std::string::npos) return find("KIRIN9000S");
    if (cleanId.find("TENSOR") != std::string::npos) return find("TENSOR3");

    return nullptr;
}

const SoCProfile* SoCDatabase::findByDevice(const std::string& device) noexcept {
    load();
    if (device.empty()) return nullptr;

    std::string cleanDevice = toUpper(trim(device));

    // 查找设备型号映射
    for (const auto& [key, val] : device_map) {
        if (cleanDevice.find(toUpper(key)) != std::string::npos) {
            return find(val);
        }
    }

    return nullptr;
}

std::vector<std::string> SoCDatabase::getAllSoCs() noexcept {
    load();
    std::vector<std::string> result;
    for (const auto& [key, val] : db) {
        result.push_back(val.name);
    }
    return result;
}

// ────────── 应用 Target FPS 映射 ──────────
// MetaDVFS/E-Mapper 风格: 根据包名匹配 target_fps
static const std::unordered_map<std::string_view, uint32_t> app_target_fps = {
    // ===== 游戏 (高帧率) =====
    // RPG/ACT
    {"com.miHoYo.Genshin", 60},
    {"com.miHoYo.StarRail", 60},
    {"com.miHoYo.ZZZ", 60},
    {"com.supercell.clashroyale", 60},
    {"com.supercell.clashofclans", 60},
    {"com.supercell.brawlstars", 60},
    {"com.king.candycrushsaga", 60},
    {"com.king.candycrush", 60},
    {"com.rovio.battle", 60},
    {"com.netease.gl", 60},
    {"com.garena.game.codm", 120},
    {"com.garena.game.freefire", 60},
    {"com.tencent.legends", 120},
    {"com.riotgames.league.of.legends.wildrift", 60},
    {"com.blitz.blitzpvp", 60},
    {"com.mobile.legends", 60},
    // ===== 视频应用 (中帧率) =====
    {"com.ss.android.ugc.bytedance", 60},    // TikTok
    {"com.zhiliaoapp.musically", 60},       // TikTok CN
    {"com.kwai.video", 60},                // Kwai
    {"com.youku.phone", 60},                // YouTube CN
    {"com.tencent.qqlive", 60},              // QQ Video
    {"com.alibaba.igithub", 60},             // iQiyi
    {"com.baidu.searchbox", 60},           // Baidu
    {"com.baidu.wangwen", 60},             // iQiyi
    {"tv.danmaku.ijk", 60},                // Bilibili
    {"me.ijk", 60},                      // Bilibili
    {"tv.poke.app", 60},                 // Bilibili
    // ===== 浏览器 (中帧率) =====
    {"com.android.browser", 60},
    {"com.google.android.browser", 60},
    {"com.microsoft.edge", 60},
    {"com.opera.browser", 60},
    {"com.sec.android.app.sbrowser", 60},
    // ===== 社交/日常 (低帧率) =====
    {"com.tencent.mm", 30},              // WeChat
    {"com.tencent.mobileqq", 30},          // QQ
    {"com.whatsapp", 30},
    {"com.facebook.katana", 30},
    {"com.instagram.android", 30},
    {"com.twitter.android", 30},
    {"com.google.android.apps.maps", 30},
    // ===== 高刷新率游戏 ===== 
    {"com.activision.callofduty.mobile", 120},
    {"com.garena.game.codm", 120},
    {"com.pubgkmobile", 60},
    {"com.riotgames.valorant", 60},
    {"com.ea.gp.fifa16", 60},
    {"com.ea.gp.fifa17", 60},
    {"com.ea.gp.fifa18", 60},
    {"com.ea.gp.fifa19", 60},
    {"com.ea.gp.fifa20", 60},
    {"com.ea.gp.fifa21", 60},
    {"com.ea.gp.easportsfifa22", 60},
    {"com.ea.gp.easportsfifa23", 60},
};

// 搜索应用 target_fps (前缀匹配 + 关键词匹配)
uint32_t getAppTargetFps(std::string_view package) {
    if (package.empty()) return 0;
    
    // 1. 精确匹配
    auto it = app_target_fps.find(package);
    if (it != app_target_fps.end()) return it->second;
    
    // 2. 前缀匹配 (适配不同版本)
    for (const auto& [key, fps] : app_target_fps) {
        if (package.starts_with(key)) return fps;
    }
    
    // 3. 关键词匹配
    std::string upper = std::string(package);
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    
    if (upper.find("GAMENICK") != std::string::npos ||
        upper.find("GAME") != std::string::npos ||
        upper.find("MOBILELEGEND") != std::string::npos ||
        upper.find("GENSHIN") != std::string::npos ||
        upper.find("STARRAIL") != std::string::npos ||
        upper.find("BRAWL") != std::string::npos ||
        upper.find("CLASH") != std::string::npos) {
        return 60;
    }
    
    if (upper.find("TIKTOK") != std::string::npos ||
        upper.find("VIDEO") != std::string::npos ||
        upper.find("KWAII") != std::string::npos ||
        upper.find("BILIBILI") != std::string::npos ||
        upper.find("IQIYI") != std::string::npos ||
        upper.find("YOUTUBE") != std::string::npos ||
        upper.find("NETFLIX") != std::string::npos) {
        return 60;
    }
    
    return 0;  // 未知应用，不使用 FAS
}

} // namespace hp::device