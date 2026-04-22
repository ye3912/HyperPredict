#pragma once
#include <cstdint>
#include <cstring>

namespace hp {
using Timestamp = uint64_t;

// Target FPS 枚举
enum class TargetFPS : uint8_t {
    FPS_30 = 30,
    FPS_60 = 60,
    FPS_90 = 90,
    FPS_120 = 120,
    FPS_144 = 144,
    FPS_165 = 165,
    UNKNOWN = 0
};

struct LoadFeature {
    // 基础指标
    uint32_t cpu_util{0};           // 0-1024
    uint32_t run_queue_len{0};
    uint32_t wakeups_100ms{0};
    uint32_t frame_interval_us{0};
    uint32_t touch_rate_100ms{0};
    int32_t thermal_margin{20};
    int32_t battery_level{100};
    bool is_gaming{false};

    // 应用信息
    char package_name[64]{0};       // 当前应用包名
    uint32_t target_fps{60};       // 目标帧率

    // 派生指标 (由 FeatureExtractor 计算)
    uint32_t current_fps{60};
    uint8_t load_intensity{0};      // 0-100
};

struct FreqConfig {
    uint32_t target_freq{0};
    uint32_t min_freq{0};
    uint8_t uclamp_min{0};
    uint8_t uclamp_max{100};
};

struct BaselinePolicy {
    FreqConfig big;
    FreqConfig mid;
    FreqConfig little;
};
constexpr inline uint8_t lookup_target_fps(const char* pkg) noexcept {
    if (!pkg) return 60;
    
    // 165 FPS 游戏
    if (strstr(pkg, "com.pubg.krmbx") ||     // PUBG Mobile
        strstr(pkg, "com.tencent.ig"))       // PUBG Global
        return 165;
    if (strstr(pkg, "com.miHoYo.hyperion"))  // 原神
        return 60;
    
    // 144 FPS 游戏
    if (strstr(pkg, "com.miHoYo.enterprise"))  // 崩坏星穹铁道
        return 144;
    if (strstr(pkg, "com.HSR.gitana"))        // 绝区零
        return 144;
    if (strstr(pkg, "com.supercell.clashofclans"))  // 部落冲突
        return 144;
    
    // 120 FPS 游戏
    if (strstr(pkg, "com.tencent.lolm"))      // LOL手游
        return 120;
    if (strstr(pkg, "com.tencent王者荣耀"))   // 王者荣耀
        return 120;
    if (strstr(pkg, "com.tencent.tmgp.sgame"))  // 使命召唤
        return 120;
    if (strstr(pkg, "com.innersect.tctw"))    // 蛋仔派对
        return 120;
    if (strstr(pkg, "com.four33company.google"))  // 皇室战争
        return 120;
    if (strstr(pkg, "com.supercell.brawlstars"))  // 荒野乱斗
        return 120;
    
    // 90 FPS 游戏
    if (strstr(pkg, "com.epiccodm"))           // COD Mobile
        return 90;
    
    // 默认 60 FPS
    return 60;
}

constexpr inline bool is_game_package(const char* pkg) noexcept {
    if (!pkg) return false;
    return lookup_target_fps(pkg) != 60;
}
}