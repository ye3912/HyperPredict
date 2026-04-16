cmake_minimum_required(VERSION 3.10)
project(HyperPredict LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${CMAKE_SOURCE_DIR}/include)

if(ANDROID)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -fPIC -fvisibility=hidden")
endif()

# ================= 单一静态库（避免跨库依赖问题）=================
add_library(hyperpredict_lib STATIC
    # Core
    src/core/logger.cpp
    src/core/lockfree_queue.cpp
    src/core/boot_calibrator.cpp
    src/core/system_collector.cpp
    src/core/event_loop.cpp
    
    # Device
    src/device/cpu_topology.cpp
    src/device/cpu_freq_manager.cpp
    src/device/hardware_analyzer.cpp
    src/device/soc_database.cpp
    src/device/migration_engine.cpp
    src/device/core_binder.cpp
    
    # Sched
    src/sched/policy_engine.cpp
    
    # Predict
    src/predict/predictor.cpp
    src/predict/feature_extractor.cpp
    src/predict/fallback_manager.cpp
    
    # Cache
    src/cache/lru_cache.cpp
    
    # Kernel
    src/kernel/sysfs_writer.cpp
)

# ================= 可执行文件 =================
add_executable(hyperpredictd src/main.cpp)

target_link_libraries(hyperpredictd
    hyperpredict_lib
    log
    c++_static
)

# 设置入口点
set_target_properties(hyperpredictd PROPERTIES
    OUTPUT_NAME "hyperpredictd"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)