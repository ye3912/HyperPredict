#pragma once
#include <cstdint>

namespace hp::predict {

// =============================================================================
// 场景类型 - 类比 CNN 论文中针对 H2P 的专项优化
// =============================================================================
enum class SchedScene : uint8_t {
    IDLE        = 0,  // 待机
    LIGHT       = 1,  // 轻度负载 (浏览/社交)
    MEDIUM      = 2,  // 中度负载 (音乐)
    VIDEO       = 3,  // 视频播放 (抖音/视频软件)
    HEAVY       = 4,  // 重度负载 (游戏)
    BOOST       = 5,  // 紧急 boost (触摸/唤醒)
    IO_WAIT     = 6,  // IO 密集型
    SCENE_COUNT = 7
};

} // namespace hp::predict
