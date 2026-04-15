<<<<<<< HEAD
// include/kernel/sysfs_writer.h
#pragma once
=======
#pragma once
#include <string>
>>>>>>> 0c63a1712e0bb23f2a76ffafe2e7a2000ce80799
#include <vector>
#include <utility>
#include <cstdint>
#include "core/types.h"
<<<<<<< HEAD
namespace hp::kernel {
enum class Backend { UCLAMP, CGROUPS, FREQ, NONE };
class SysfsWriter {
    Backend bk_{Backend::NONE}; std::string cg_root_;
    struct Fds { int mn{-1}, mx{-1}, um{-1}, ux{-1}; } fds_[8];
public:
    SysfsWriter(); ~SysfsWriter();
    Backend backend() const noexcept { return bk_; }
    bool apply(const std::vector<std::pair<int, FreqConfig>>& b) noexcept;
private:
    void detect() noexcept; bool open(int c) noexcept;
    bool wf(int c, uint32_t mn, uint32_t mx) noexcept;
    bool wu(int c, uint8_t mn, uint8_t mx) noexcept;
    bool wc(int c, uint8_t pct) noexcept;
};
}

=======

namespace hp::kernel {

class SysfsWriter {
    bool uclamp_supported_{false};
    bool cgroups_supported_{false};
    std::string cgroup_path_;
    
    struct CpuData {
        int min_fd{-1};
        int max_fd{-1};
        char buf[32];
    };
    CpuData cpus_[8]{};
    
public:
    SysfsWriter();
    ~SysfsWriter();
    
    bool set_batch(const std::vector<std::pair<int, FreqConfig>>& batch) noexcept;
    bool set_frequency(int cpu, uint32_t freq) noexcept;
    bool set_uclamp(int cpu, uint8_t min, uint8_t max) noexcept;
    bool set_cpu_shares(int cpu, uint16_t shares) noexcept;
    bool uclamp_supported() const noexcept { return uclamp_supported_; }
    bool cgroups_supported() const noexcept { return cgroups_supported_; }
    
private:
    bool write_sysfs(const std::string& path, const std::string& value) noexcept;
    bool check_uclamp_support() noexcept;
    bool check_cgroups_support() noexcept;
    uint16_t uclamp_to_shares(uint8_t uclamp) noexcept;
};

} // namespace hp::kernel
>>>>>>> 0c63a1712e0bb23f2a76ffafe2e7a2000ce80799
