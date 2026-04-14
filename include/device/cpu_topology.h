#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace hp::device {

struct CPUCore {
    int id;
    uint32_t max_freq;
    uint32_t min_freq;
    bool is_online;
};

class CpuTopology {
    std::vector<CPUCore> cores_;
    std::vector<int> big_cores_;
    std::vector<int> little_cores_;
    int highest_freq_core_{-1};
    
    uint32_t read_max_freq(int cpu) noexcept;
    bool is_cpu_online(int cpu) noexcept;
    void classify_cores() noexcept;
    
public:
    bool detect() noexcept;
    const std::vector<int>& get_big_cores() const noexcept { return big_cores_; }
    const std::vector<int>& get_little_cores() const noexcept { return little_cores_; }
    int get_highest_freq_core() const noexcept { return highest_freq_core_; }
    const std::vector<CPUCore>& get_all_cores() const noexcept { return cores_; }
    void print_info() const noexcept;
};

} // namespace hp::device