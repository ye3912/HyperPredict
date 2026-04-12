cmake_minimum_required(VERSION 3.20)
project(HyperPredict VERSION 2.0.0 LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-O3 -flto=thin -fno-exceptions -fno-rtti -ffast-math -fno-unwind-tables)
add_link_options(-flto=thin -Wl,--gc-sections -s)

if(DEFINED ANDROID_NDK)
    add_definitions(-DPLATFORM_ANDROID)
    link_libraries(log)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(hp_core STATIC src/core/lockfree_queue.cpp src/core/logger.cpp)
add_library(hp_predict STATIC src/predict/predictor.cpp src/predict/fallback_manager.cpp src/predict/feature_extractor.cpp)
add_library(hp_cache STATIC src/cache/lru_cache.cpp)
add_library(hp_sched STATIC src/sched/policy_engine.cpp)
add_library(hp_kernel STATIC src/kernel/sysfs_writer.cpp)
add_library(hp_boot STATIC src/core/boot_calibrator.cpp)

add_executable(hyperpredictd src/main.cpp src/core/event_loop.cpp)
target_link_libraries(hyperpredictd PRIVATE hp_core hp_predict hp_cache hp_sched hp_kernel hp_boot pthread)

install(TARGETS hyperpredictd DESTINATION /data/adb/hyperpredict/bin)