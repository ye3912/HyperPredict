#pragma once
#include <vector>
#include <utility>
#include <cstdint>
#include <string>
#include <array>
#include <span>
#include <string_view>
#include <unistd.h>
#include "core/types.h"

namespace hp::kernel {

// Modern C++: 强类型枚举 + constexpr
enum class Backend : uint8_t { UCLAMP, CGROUPS, FREQ, NONE };

// RAII 文件描述符封装 - 避免资源泄漏
class FdGuard {
public:
    explicit FdGuard(int fd = -1) noexcept : fd_(fd) {}
    ~FdGuard() { reset(); }
    
    // Move semantics
    FdGuard(FdGuard&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    FdGuard& operator=(FdGuard&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }
    
    // 禁止拷贝
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;
    
    [[nodiscard]] int get() const noexcept { return fd_; }
    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }
    explicit operator bool() const noexcept { return fd_ >= 0; }

private:
    int fd_;
};

// 优化的 CPU FD 集合 - 使用 move-only RAII
struct CpuFds {
    FdGuard min_freq;
    FdGuard max_freq;
    FdGuard uclamp_min;
    FdGuard uclamp_max;
    
    // 移动语义
    CpuFds() = default;
    CpuFds(CpuFds&&) = default;
    CpuFds& operator=(CpuFds&&) = default;
    CpuFds(const CpuFds&) = delete;
    CpuFds& operator=(const CpuFds&) = delete;
};

class SysfsWriter {
public:
    static constexpr size_t MAX_CPUS = 8;
    static constexpr size_t BUF_SIZE = 16;
    
    SysfsWriter();
    ~SysfsWriter();
    
    // Move semantics
    SysfsWriter(SysfsWriter&&) noexcept;
    SysfsWriter& operator=(SysfsWriter&&) noexcept;
    SysfsWriter(const SysfsWriter&) = delete;
    SysfsWriter& operator=(const SysfsWriter&) = delete;
    
    [[nodiscard]] Backend backend() const noexcept { return bk_; }
    
    // 使用 vector 代替 span (NDK 兼容)
    [[nodiscard]] bool apply(const std::vector<std::pair<int, FreqConfig>>& b) noexcept;
    
    // 批量更新 - 返回成功数
    [[nodiscard]] size_t apply_batch(const std::vector<std::pair<int, FreqConfig>>& b) noexcept;
    
    // 调试: 获取 FD 状态
    [[nodiscard]] const std::array<CpuFds, MAX_CPUS>& fd_state() const noexcept { return fds_; }

private:
    Backend bk_{Backend::NONE};
    std::string cg_root_;
    std::array<CpuFds, MAX_CPUS> fds_;
    
    // 预分配的写入缓冲区 (thread-local)
    inline static thread_local char tl_min_buf_[BUF_SIZE];
    inline static thread_local char tl_max_buf_[BUF_SIZE];
    
    void detect() noexcept;
    bool open_cpu(int c) noexcept;
    
    // 写入辅助 - 内联优化
    [[nodiscard]] bool write_min(int fd, uint32_t val) noexcept;
    [[nodiscard]] bool write_max(int fd, uint32_t val) noexcept;
    [[nodiscard]] bool write_uclamp(int fd, uint8_t val) noexcept;
    [[nodiscard]] bool write_cgroup(int c, uint8_t pct) noexcept;
    
    // 静态工具函数
    static std::string_view detect_cg_root() noexcept;
};

} // namespace hp::kernel