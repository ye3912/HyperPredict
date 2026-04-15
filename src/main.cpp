#include <csignal>
#include "core/event_loop.h"
#include "core/logger.h"

hp::EventLoop* g_loop = nullptr;

void handler(int) {
    if (g_loop) g_loop->stop();
}

int main() {
    hp::init_logger("HyperPredict", hp::LogLevel::INFO);
    signal(SIGTERM, handler);
    signal(SIGINT, handler);
    signal(SIGUSR1, handler);
    
    hp::EventLoop loop;
    g_loop = &loop;
    loop.start();
    
    hp::close_logger();
    return 0;
}