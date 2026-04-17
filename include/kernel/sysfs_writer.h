#pragma once
#include <vector>
#include <utility>
#include <cstdint>
#include <string>
#include "core/types.h"

namespace hp::kernel {

enum class Backend { UCLAMP, CGROUPS, FREQ, NONE };

class SysfsWriter {
    Backend bk_{Backend::NONE};
    std::string cg_root_;
    
    // 使用 int FD 提升性能并兼容 POSIX open/write
    struct Fds { int mn{-1}, mx{-1}, um{-1}, ux{-1}; } fds_[8];
    
public:
    SysfsWriter();
    ~SysfsWriter();
    Backend backend() const noexcept { return bk_; }
    bool apply(const std::vector<std::pair<int, FreqConfig>>& b) noexcept;
    
private:
    void detect() noexcept;
    bool open(int c) noexcept;
    bool wf(int c, uint32_t mn, uint32_t mx) noexcept;
    bool wu(int c, uint8_t mn, uint8_t mx) noexcept;
    bool wc(int c, uint8_t pct) noexcept;
};

} // namespace hp::kernel