<<<<<<< HEAD
// include/device/cpu_topology.h
#pragma once
#include <vector>
#include <cstdint>
namespace hp::device {
class CpuTopology {
public:
    struct Domain { std::vector<int> cpus; uint32_t min_freq{0}, max_freq{0}; };
    bool detect() noexcept;
    const std::vector<Domain>& get_domains() const noexcept { return domains_; }
    int get_total_cpus() const noexcept { return total_; }
private:
    std::vector<Domain> domains_; int total_{0};
};
}

=======
#pragma once
#include <vector>
#include <cstdint>

namespace hp::device {

class CpuTopology {
public:
    struct Domain {
        std::vector<int> cpus;
        uint32_t min_freq{0};
        uint32_t max_freq{0};
    };

    bool detect() noexcept;
    const std::vector<Domain>& get_domains() const noexcept { return domains_; }
    int get_total_cpus() const noexcept { return total_cpus_; }

private:
    std::vector<Domain> domains_;
    int total_cpus_{0};
};

} // namespace hp::device
>>>>>>> 0c63a1712e0bb23f2a76ffafe2e7a2000ce80799
