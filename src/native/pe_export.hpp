#pragma once

#include "native/proc_mem.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace proton_inject {

struct SectionCave {
    std::uintptr_t addr = 0;
    std::size_t size = 0;
    bool writable = false;
    bool executable = false;
};

struct IatSlot {
    std::uintptr_t slot = 0;
    std::uintptr_t original = 0;
};

[[nodiscard]] std::expected<std::uintptr_t, std::string> find_pe_export(
    const ProcMem& mem, std::uintptr_t image_base, std::string_view function_name);

[[nodiscard]] std::optional<IatSlot> find_iat_slot(const ProcMem& mem, std::uintptr_t image_base,
                                                   std::string_view function_name);

[[nodiscard]] std::expected<std::vector<SectionCave>, std::string> find_pe_caves(
    const ProcMem& mem, std::uintptr_t image_base);

}  // namespace proton_inject
