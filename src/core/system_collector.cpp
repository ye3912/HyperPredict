#include "core/system_collector.h"
#include "core/frame_pacer.h"
#include "core/logger.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <cinttypes>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/connector.h>
#include <asm/types.h>

// Connector 进程事件定义
#define CN_IDX_PROC 0x1
#define CN_VAL_PROC 0x1

enum proc_cn_mcast_op {
    PROC_CN_MCAST_LISTEN = 1,
    PROC_CN_MCAST_IGNORE = 2
};

namespace hp {

// 文件作用域静态变量 (保持状态)
static uint64_t last_cpu_time_[2] = {0, 0};
static uint64_t last_cpu_idle_[2] = {0, 0};
static uint32_t last_wakeups_ = 0;
static uint64_t last_touch_time = 0;
static uint32_t touch_count = 0;

// 构造函数实现
SystemCollector::SystemCollector() {
    // 优化: 预打开 thermal 和 battery fd
    thermal_fds_[0] = ::open("/sys/class/thermal/thermal_zone0/temp", O_RDONLY | O_CLOEXEC);
    thermal_fds_[1] = ::open("/sys/class/thermal/thermal_zone1/temp", O_RDONLY | O_CLOEXEC);
    thermal_fds_[2] = ::open("/sys/class/thermal/thermal_zone2/temp", O_RDONLY | O_CLOEXEC);
    thermal_fds_[3] = ::open("/devices/virtual/thermal/thermal_zone0/temp", O_RDONLY | O_CLOEXEC);
    battery_fd_ = ::open("/sys/class/power_supply/battery/capacity", O_RDONLY | O_CLOEXEC);
    if (battery_fd_ < 0) {
        battery_fd_ = ::open("/sys/class/power_supply/bq27541/capacity", O_RDONLY | O_CLOEXEC);
    }

    // 优化: 预打开 proc_stat 和 proc_loadavg fd
    proc_stat_fd_ = ::open("/proc/stat", O_RDONLY | O_CLOEXEC);
    proc_loadavg_fd_ = ::open("/proc/loadavg", O_RDONLY | O_CLOEXEC);

    // 初始化 netlink（用于检测应用切换）
    init_netlink();
}

// 析构函数实现
SystemCollector::~SystemCollector() {
    // 关闭所有打开的 fd
    for (int i = 0; i < 4; i++) {
        if (thermal_fds_[i] >= 0) ::close(thermal_fds_[i]);
    }
    if (battery_fd_ >= 0) ::close(battery_fd_);
    if (proc_stat_fd_ >= 0) ::close(proc_stat_fd_);
    if (proc_loadavg_fd_ >= 0) ::close(proc_loadavg_fd_);

    // 清理 netlink
    cleanup_netlink();
}

LoadFeature SystemCollector::collect() noexcept {
    LoadFeature f;

    f.cpu_util = read_cpu_util();
    f.run_queue_len = read_run_queue();
    f.wakeups_100ms = read_wakeups();

    static core::FramePacer pacer;
    static bool inited = false;
    if (!inited) {
        pacer.init();
        inited = true;
    }

    uint64_t interval = pacer.collect();
    if (interval > 0) {
        f.frame_interval_us = static_cast<uint32_t>(interval);
    } else {
        f.frame_interval_us = pacer.get_smooth_interval_us();
    }

    f.is_gaming = pacer.is_high_refresh() && pacer.is_stable();
    f.touch_rate_100ms = read_touch_rate();
    f.thermal_margin = read_thermal_margin();
    f.battery_level = read_battery_level();

    // 使用 netlink 检测应用切换（最低开销）
    bool app_switched = check_netlink_events();

    // 如果 netlink 检测到应用切换，或者首次运行，读取包名
    if (app_switched || cached_package_name_[0] == '\0') {
        const char* package = read_package_name_cached();
        if (package) {
            strncpy(f.package_name, package, sizeof(f.package_name) - 1);
            f.package_name[sizeof(f.package_name) - 1] = '\0';
        }
    } else {
        // 使用缓存的包名
        if (cached_package_name_[0] != '\0') {
            strncpy(f.package_name, cached_package_name_, sizeof(f.package_name) - 1);
            f.package_name[sizeof(f.package_name) - 1] = '\0';
        }
    }

    return f;
}

// 成员函数，返回类型 uint32_t
uint32_t SystemCollector::read_cpu_util() noexcept {
    // 优化: 使用预打开的文件描述符
    if (proc_stat_fd_ < 0) {
        // 回退到 fopen
        FILE* fp = fopen("/proc/stat", "r");
        if (!fp) return 512;

        char line[256] = {0};
        unsigned long user, nice, system, idle, iowait, irq, softirq, steal;

        if (fgets(line, sizeof(line), fp)) {
            fclose(fp);

            int n = sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
                           &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
            if (n < 4) return 512;

            uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
            uint64_t idle_total = idle + iowait;

            uint64_t total_diff = total - last_cpu_time_[0];
            uint64_t idle_diff = idle_total - last_cpu_idle_[0];

            last_cpu_time_[0] = total;
            last_cpu_idle_[0] = idle_total;

            if (total_diff > 0) {
                uint32_t util = static_cast<uint32_t>((total_diff - idle_diff) * 1024 / total_diff);
                return std::min(util, static_cast<uint32_t>(1024));
            }
        } else {
            fclose(fp);
        }

        return 512;
    }

    // 使用预打开的文件描述符
    char line[256] = {0};
    unsigned long user, nice, system, idle, iowait, irq, softirq, steal;

    // 重置文件指针到开头
    lseek(proc_stat_fd_, 0, SEEK_SET);

    // 读取文件内容
    ssize_t bytes_read = ::read(proc_stat_fd_, line, sizeof(line) - 1);
    if (bytes_read <= 0) {
        return 512;
    }
    line[bytes_read] = '\0';

    int n = sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    if (n < 4) return 512;

    uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
    uint64_t idle_total = idle + iowait;

    uint64_t total_diff = total - last_cpu_time_[0];
    uint64_t idle_diff = idle_total - last_cpu_idle_[0];

    last_cpu_time_[0] = total;
    last_cpu_idle_[0] = idle_total;

    if (total_diff > 0) {
        uint32_t util = static_cast<uint32_t>((total_diff - idle_diff) * 1024 / total_diff);
        return std::min(util, static_cast<uint32_t>(1024));
    }

    return 512;
}

uint32_t SystemCollector::read_run_queue() noexcept {
    // 优化: 使用预打开的文件描述符
    if (proc_loadavg_fd_ < 0) {
        // 回退到 fopen
        FILE* fp = fopen("/proc/loadavg", "r");
        if (!fp) return 0;

        float load_1min = 0;
        if (fscanf(fp, "%f", &load_1min) == 1) {
            fclose(fp);
            return static_cast<uint32_t>(load_1min * 4);
        }

        fclose(fp);
        return 0;
    }

    // 使用预打开的文件描述符
    char line[64] = {0};

    // 重置文件指针到开头
    lseek(proc_loadavg_fd_, 0, SEEK_SET);

    // 读取文件内容
    ssize_t bytes_read = ::read(proc_loadavg_fd_, line, sizeof(line) - 1);
    if (bytes_read <= 0) {
        return 0;
    }
    line[bytes_read] = '\0';

    float load_1min = 0;
    if (sscanf(line, "%f", &load_1min) == 1) {
        return static_cast<uint32_t>(load_1min * 4);
    }

    return 0;
}

uint32_t SystemCollector::read_wakeups() noexcept {
    FILE* fp = fopen("/proc/stat", "r");
    if (!fp) return 0;
    
    char line[256] = {0};
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "ctxt", 4) == 0) {
            fclose(fp);
            
            unsigned long ctxt = 0;
            if (sscanf(line, "ctxt %lu", &ctxt) == 1) {
                uint32_t diff = static_cast<uint32_t>(ctxt - last_wakeups_);
                last_wakeups_ = static_cast<uint32_t>(ctxt);
                return std::min(diff, static_cast<uint32_t>(1000));
            }
            break;
        }
    }
    
    fclose(fp);
    return 0;
}

uint32_t SystemCollector::read_touch_rate() noexcept {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        uint64_t now = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
        
        if (last_touch_time > 0) {
            uint64_t delta = now - last_touch_time;
            if (delta < 100000) {
                touch_count++;
            } else {
                touch_count = 0;
            }
        }
        
        last_touch_time = now;
        return std::min(touch_count, static_cast<uint32_t>(200));
    }
    
    return 0;
}

// 成员函数实现
int8_t SystemCollector::read_thermal_margin() noexcept {
    int32_t current_temp = 35;
    
    // 优化: 使用预打开的 fd
    for (int i = 0; i < 4; ++i) {
        if (thermal_fds_[i] >= 0) {
            char buf[32] = {0};
            ssize_t n = pread(thermal_fds_[i], buf, sizeof(buf) - 1, 0);
            if (n > 0) {
                int32_t temp = atoi(buf);
                if (temp > 1000) temp /= 1000;
                if (temp > 20 && temp < 100) {
                    current_temp = temp;
                    break;
                }
            }
        }
    }
    
    int32_t margin = 85 - current_temp;
    return static_cast<int8_t>(std::max(0, std::min(margin, 60)));
}

uint8_t SystemCollector::read_battery_level() noexcept {
    if (battery_fd_ >= 0) {
        char buf[16] = {0};
        ssize_t n = pread(battery_fd_, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            int32_t level = atoi(buf);
            return static_cast<uint8_t>(std::clamp(level, 0, 100));
        }
    }
    return 100;
}

bool SystemCollector::is_gaming_scene() noexcept {
    LoadFeature f = collect();
    return f.is_gaming;
}

const char* SystemCollector::read_package_name_cached() noexcept {
    // 检查缓存是否过期
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - package_cache_time_).count();
    if (elapsed < PACKAGE_TTL_MS && cached_package_name_[0] != '\0') {
        return cached_package_name_;
    }

    // 缓存过期或无效，重新读取
    cached_package_name_[0] = '\0';

    // 方法 1: 使用 dumpsys activity top (最快，但需要 root)
    FILE* fp = popen("dumpsys activity top | grep ACTIVITY | head -1", "r");
    if (fp) {
        char line[256] = {0};
        if (fgets(line, sizeof(line), fp)) {
            // 解析包名: ACTIVITY com.ss.android.ugc.aweme/.MainActivity
            char* activity = strstr(line, "ACTIVITY ");
            if (activity) {
                activity += 9;  // 跳过 "ACTIVITY "
                char* slash = strchr(activity, '/');
                if (slash) {
                    *slash = '\0';
                    strncpy(cached_package_name_, activity, sizeof(cached_package_name_) - 1);
                    cached_package_name_[sizeof(cached_package_name_) - 1] = '\0';
                }
            }
        }
        pclose(fp);

        if (cached_package_name_[0] != '\0') {
            package_cache_time_ = now;
            return cached_package_name_;
        }
    }

    // 方法 2: 使用 /proc/<pid>/cgroup 识别前台应用 (更轻量级)
    DIR* proc_dir = opendir("/proc");
    if (!proc_dir) {
        package_cache_time_ = now;
        return nullptr;
    }

    struct dirent* entry;
    while ((entry = readdir(proc_dir)) != nullptr) {
        // 跳过非数字目录
        if (!isdigit(entry->d_name[0])) {
            continue;
        }

        // 读取 cgroup
        char cgroup_path[128];
        snprintf(cgroup_path, sizeof(cgroup_path), "/proc/%s/cgroup", entry->d_name);

        int cgroup_fd = open(cgroup_path, O_RDONLY | O_CLOEXEC);
        if (cgroup_fd < 0) {
            continue;
        }

        char cgroup_buf[512] = {0};
        ssize_t cgroup_n = read(cgroup_fd, cgroup_buf, sizeof(cgroup_buf) - 1);
        close(cgroup_fd);

        if (cgroup_n <= 0) {
            continue;
        }

        // 检查是否为前台应用 (cgroup 中包含 "top" 或 "foreground")
        if (strstr(cgroup_buf, "/top") || strstr(cgroup_buf, "/foreground")) {
            // 读取 cmdline
            char cmdline_path[128];
            snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", entry->d_name);

            int cmdline_fd = open(cmdline_path, O_RDONLY | O_CLOEXEC);
            if (cmdline_fd < 0) {
                continue;
            }

            char cmdline_buf[256] = {0};
            ssize_t cmdline_n = read(cmdline_fd, cmdline_buf, sizeof(cmdline_buf) - 1);
            close(cmdline_fd);

            if (cmdline_n <= 0) {
                continue;
            }

            // 提取包名（cmdline 的第一部分）
            char* newline = strchr(cmdline_buf, '\n');
            if (newline) {
                *newline = '\0';
            }

            // 检查是否为有效的包名（包含点号）
            if (strchr(cmdline_buf, '.')) {
                strncpy(cached_package_name_, cmdline_buf, sizeof(cached_package_name_) - 1);
                cached_package_name_[sizeof(cached_package_name_) - 1] = '\0';
                break;
            }
        }
    }

    closedir(proc_dir);
    package_cache_time_ = now;

    return cached_package_name_[0] != '\0' ? cached_package_name_ : nullptr;
}

void SystemCollector::init_netlink() noexcept {
    // 创建 netlink socket
    netlink_fd_ = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
    if (netlink_fd_ < 0) {
        LOGD("Failed to create netlink socket, falling back to /proc polling");
        netlink_enabled_ = false;
        return;
    }

    // 绑定 socket
    struct sockaddr_nl addr;
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = CN_IDX_PROC;
    addr.nl_pid = getpid();

    if (bind(netlink_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOGD("Failed to bind netlink socket, falling back to /proc polling");
        close(netlink_fd_);
        netlink_fd_ = -1;
        netlink_enabled_ = false;
        return;
    }

    // 订阅进程事件
    struct nlmsghdr nlh;
    memset(&nlh, 0, sizeof(nlh));
    nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct cn_msg));
    nlh.nlmsg_type = NLMSG_DONE;
    nlh.nlmsg_flags = 0;
    nlh.nlmsg_seq = 1;
    nlh.nlmsg_pid = getpid();

    struct cn_msg cn_msg;
    memset(&cn_msg, 0, sizeof(cn_msg));
    cn_msg.id.idx = CN_IDX_PROC;
    cn_msg.id.val = CN_VAL_PROC;
    cn_msg.len = sizeof(enum proc_cn_mcast_op);

    enum proc_cn_mcast_op op = PROC_CN_MCAST_LISTEN;
    memcpy(NLMSG_DATA(&nlh), &cn_msg, sizeof(cn_msg));
    memcpy((char*)NLMSG_DATA(&nlh) + sizeof(cn_msg), &op, sizeof(op));

    if (send(netlink_fd_, &nlh, nlh.nlmsg_len, 0) < 0) {
        LOGD("Failed to subscribe to process events, falling back to /proc polling");
        close(netlink_fd_);
        netlink_fd_ = -1;
        netlink_enabled_ = false;
        return;
    }

    // 设置为非阻塞模式
    int flags = fcntl(netlink_fd_, F_GETFL, 0);
    fcntl(netlink_fd_, F_SETFL, flags | O_NONBLOCK);

    netlink_enabled_ = true;
    LOGD("Netlink process monitoring enabled");
}

void SystemCollector::cleanup_netlink() noexcept {
    if (netlink_fd_ >= 0) {
        close(netlink_fd_);
        netlink_fd_ = -1;
    }
    netlink_enabled_ = false;
}

bool SystemCollector::check_netlink_events() noexcept {
    if (!netlink_enabled_ || netlink_fd_ < 0) {
        return false;
    }

    // 检查是否有 netlink 事件
    char buf[4096];
    ssize_t len = recv(netlink_fd_, buf, sizeof(buf), MSG_DONTWAIT);

    if (len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 没有事件
            return false;
        }
        // 错误，禁用 netlink
        netlink_enabled_ = false;
        return false;
    }

    if (len == 0) {
        return false;
    }

    // 解析 netlink 消息
    struct nlmsghdr* nlh = (struct nlmsghdr*)buf;
    while (NLMSG_OK(nlh, len)) {
        if (nlh->nlmsg_type == NLMSG_DONE) {
            struct cn_msg* cn_msg = (struct cn_msg*)NLMSG_DATA(nlh);
            if (cn_msg->id.idx == CN_IDX_PROC && cn_msg->id.val == CN_VAL_PROC) {
                // 进程事件，标记需要更新包名
                return true;
            }
        }
        nlh = NLMSG_NEXT(nlh, len);
    }

    return false;
}

} // namespace hp