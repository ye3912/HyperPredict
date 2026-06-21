#include <csignal>
#include <cstring>
#include <thread>
#include <atomic>
#include <cstdio>
#include "core/event_loop.h"
#include "core/logger.h"

hp::EventLoop* g_loop = nullptr;
static std::atomic<bool> g_stop{false};

void handler(int) {
    g_stop.store(true);
}

int main(int argc, char* argv[]) {
    // 解析命令行参数
    const char* mod_dir = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--mod-dir") == 0 && i + 1 < argc) {
            mod_dir = argv[i + 1];
            ++i;
        }
    }

    // 构建日志文件路径
    char log_path[256];
    if (mod_dir) {
        snprintf(log_path, sizeof(log_path), "%s/logs/hp.log", mod_dir);
    } else {
        // 默认使用 /data/local/tmp，日志写入问题少
        snprintf(log_path, sizeof(log_path), "/data/local/tmp/hp.log");
    }

    // 输出调试信息到 stderr（最可靠的输出方式）
    fprintf(stderr, "[HyperPredict] Starting daemon...\n");
    fprintf(stderr, "[HyperPredict] mod_dir: %s\n", mod_dir ? mod_dir : "(null)");
    fprintf(stderr, "[HyperPredict] log_path: %s\n", log_path);
    fprintf(stderr, "[HyperPredict] pid: %d\n", getpid());
    fflush(stderr);

    hp::init_logger("HyperPredict", hp::LogLevel::INFO, log_path);
    LOGI("=== HyperPredict Daemon Starting (pid=%d) ===", getpid());
    LOGI("mod_dir: %s", mod_dir ? mod_dir : "(null)");
    LOGI("log_path: %s", log_path);

    signal(SIGTERM, handler);
    signal(SIGINT, handler);
    signal(SIGUSR1, handler);

    hp::EventLoop loop;
    g_loop = &loop;

    // 信号监控线程：安全地将信号转发给事件循环
    std::thread signal_monitor([&loop]() {
        while (!g_stop.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        LOGI("Signal received, stopping event loop...");
        loop.stop();
    });

    fprintf(stderr, "[HyperPredict] Starting event loop...\n");
    fflush(stderr);

    loop.start();

    // 如果 start() 返回，说明初始化失败或循环退出
    fprintf(stderr, "[HyperPredict] Event loop exited, shutting down\n");
    fflush(stderr);

    if (signal_monitor.joinable()) {
        signal_monitor.join();
    }

    LOGI("=== HyperPredict Daemon Stopped ===");
    hp::close_logger();
    return 0;
}