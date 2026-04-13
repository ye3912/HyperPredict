#pragma once
#include <vector>
#include <array>
#include "core/types.h"

namespace hp::kernel {
class SysfsWriter {
    struct Cpu { int min_fd = -1, max_fd = -1; char buf[32]; };
    std::array<Cpu, 8> cpus_;
public:
    SysfsWriter() noexcept;
    ~SysfsWriter() noexcept;
    bool set_batch(const std::vector<std::pair<int, FreqConfig>>& cfgs) noexcept;
};
} // namespace hp::kernel