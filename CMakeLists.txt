cmake_minimum_required(VERSION 3.10)
project(HyperPredict VERSION 4.2.0 LANGUAGES CXX)

# ================= 编译标准 =================
# Modern C++: 默认使用 C++20，若 NDK 不支持则回退到 C++17
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 检测 NDK 版本，若低于 r26b 则回退
if(CMAKE_SYSTEM_VERSION VERSION_LESS 30)
    message(WARNING "NDK version too old, falling back to C++17")
    set(CMAKE_CXX_STANDARD 17)
endif()

# ================= 头文件搜索路径 =================
include_directories(${CMAKE_SOURCE_DIR}/include)

# ================= Android NDK 配置 =================
if(ANDROID)
    # 编译优化标志
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -fPIC -fvisibility=hidden")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffast-math -fno-exceptions -fno-rtti")
    
    # Release 模式额外优化
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -DNDEBUG")
    endif()
    
    # 链接器标志
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections -Wl,--strip-all")
endif()

# ================= 静态库：核心基础 =================
add_library(hp_core STATIC
    src/core/logger.cpp
    src/core/lockfree_queue.cpp
    src/core/boot_calibrator.cpp
    src/core/system_collector.cpp
    src/core/event_loop.cpp
    src/core/frame_pacer.cpp
)

# ================= 静态库：硬件设备 =================
add_library(hp_device STATIC
    src/device/cpu_topology.cpp
    src/device/cpu_freq_manager.cpp
    src/device/hardware_analyzer.cpp
    src/device/soc_database.cpp
    src/device/migration_engine.cpp
    src/device/core_binder.cpp
)

# ================= 静态库：调度策略 =================
add_library(hp_sched STATIC
    src/sched/policy_engine.cpp
)
# ================= 静态库：预测模型 =================
add_library(hp_predict STATIC
    src/predict/predictor.cpp
    src/predict/feature_extractor.cpp
    src/predict/fallback_manager.cpp
)

# ================= 静态库：缓存 =================
add_library(hp_cache STATIC
    src/cache/lru_cache.cpp
)

# ================= 静态库：网络服务 =================
add_library(hp_net STATIC
    src/net/web_server.cpp
)

# ================= 静态库：内核接口 =================
add_library(hp_kernel STATIC
    src/kernel/sysfs_writer.cpp
)

# ================= 可执行文件：守护进程 =================
add_executable(hyperpredictd
    src/main.cpp
)

# ================= 链接库 =================
# Android 需要 -llog，Linux 不需要
if(ANDROID)
    set(ANDROID_LIB log)
else()
    set(ANDROID_LIB)
endif()

target_link_libraries(hyperpredictd
    hp_core
    hp_device
    hp_sched
    hp_predict
    hp_cache
    hp_net
    hp_kernel
    m
    ${ANDROID_LIB}
)

# Linux 环境移除 Android 特定库
if(NOT ANDROID)
    target_compile_definitions(hyperpredictd PRIVATE HP_NO_ANDROID=1)
endif()

# ================= 输出配置 =================
set_target_properties(hyperpredictd PROPERTIES
    OUTPUT_NAME "hyperpredictd"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
)

# ================= 编译定义 =================
# Android NDK 编译时 NDK 会自动定义 __ANDROID__ 和 ANDROID
if(ANDROID)
    target_compile_definitions(hyperpredictd PRIVATE
        HP_VERSION="4.2.0"
        HP_BUILD_TYPE="$<CONFIG>"
        ANDROID=1
        __ANDROID__)  # 确保 NDK 编译时使用 android/log.h
endif()

# ================= 安装规则 (可选) =================
install(TARGETS hyperpredictd
    RUNTIME DESTINATION bin
)