#include <csignal>
#include <cstring>
#include "core/event_loop.h"
#include "core/logger.h"

hp::EventLoop* g_loop = nullptr;

void handler(int) {
    if (g_loop) g_loop->stop();
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
        // 如果没有指定模块目录，使用默认路径
        snprintf(log_path, sizeof(log_path), "/data/adb/modules/hyperpredict/logs/hp.log");
    }

    // 输出调试信息到 stderr
    fprintf(stderr, "[DEBUG] mod_dir: %s\n", mod_dir ? mod_dir : "null");
    fprintf(stderr, "[DEBUG] log_path: %s\n", log_path);

    hp::init_logger("HyperPredict", hp::LogLevel::INFO, log_path);
    signal(SIGTERM, handler);
    signal(SIGINT, handler);
    signal(SIGUSR1, handler);

    hp::EventLoop loop;
    g_loop = &loop;
    loop.start();

    hp::close_logger();
    return 0;
}