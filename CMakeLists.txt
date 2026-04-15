cmake_minimum_required(VERSION 3.20)
<<<<<<< HEAD
project(HyperPredict VERSION 4.0.0 LANGUAGES CXX C)
=======
project(HyperPredict VERSION 4.2.0 LANGUAGES CXX C)

>>>>>>> 0c63a1712e0bb23f2a76ffafe2e7a2000ce80799
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-O3 -flto=thin -fno-exceptions -fno-rtti -ffast-math -fno-unwind-tables)
add_link_options(-flto=thin -Wl,--gc-sections -s)

<<<<<<< HEAD
include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(hp_core STATIC 
    src/core/lockfree_queue.cpp src/core/logger.cpp src/core/boot_calibrator.cpp src/core/system_collector.cpp
    src/device/cpu_topology.cpp src/device/cpu_freq_manager.cpp 
    src/device/hardware_analyzer.cpp src/device/migration_engine.cpp src/device/core_binder.cpp
)
add_library(hp_predict STATIC src/predict/predictor.cpp src/predict/fallback_manager.cpp src/predict/feature_extractor.cpp)
=======
if(DEFINED ANDROID_NDK)
    add_definitions(-DPLATFORM_ANDROID)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(hp_core STATIC 
    src/core/lockfree_queue.cpp 
    src/core/logger.cpp 
    src/core/boot_calibrator.cpp 
    src/core/system_collector.cpp
    src/device/cpu_topology.cpp
    src/device/cpu_freq_manager.cpp
)

add_library(hp_predict STATIC 
    src/predict/predictor.cpp 
    src/predict/fallback_manager.cpp 
    src/predict/feature_extractor.cpp
)

>>>>>>> 0c63a1712e0bb23f2a76ffafe2e7a2000ce80799
add_library(hp_cache STATIC src/cache/lru_cache.cpp)
add_library(hp_sched STATIC src/sched/policy_engine.cpp)
add_library(hp_kernel STATIC src/kernel/sysfs_writer.cpp)

<<<<<<< HEAD
add_executable(hyperpredictd src/main.cpp src/core/event_loop.cpp)
target_link_libraries(hyperpredictd PRIVATE hp_kernel hp_sched hp_cache hp_predict hp_core log)

# 编译后自动复制 WebUI 到构建目录，方便打包
file(COPY ${CMAKE_SOURCE_DIR}/webui DESTINATION ${CMAKE_BINARY_DIR}/..)
file(COPY ${CMAKE_SOURCE_DIR}/module.prop ${CMAKE_SOURCE_DIR}/customize.sh ${CMAKE_SOURCE_DIR}/service.sh DESTINATION ${CMAKE_BINARY_DIR}/..)

install(TARGETS hyperpredictd DESTINATION ${CMAKE_SOURCE_DIR}/system/bin)
=======
add_executable(hyperpredictd 
    src/main.cpp
    src/core/event_loop.cpp
)

target_link_libraries(hyperpredictd PRIVATE 
    hp_kernel
    hp_sched
    hp_cache
    hp_predict
    hp_core
    log
)

install(TARGETS hyperpredictd DESTINATION ${CMAKE_SOURCE_DIR}/magisk_module/system/bin)
>>>>>>> 0c63a1712e0bb23f2a76ffafe2e7a2000ce80799
