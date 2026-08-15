#include "core/console_mode.hpp"

#include "utils/utils.hpp"

namespace proton_inject {

std::string normalize_console_mode(const std::string_view mode) {
    const auto normalized = to_lower(mode);
    return normalized.empty() ? std::string(kConsoleAlloc) : normalized;
}

bool valid_console_mode(const std::string_view mode) {
    const auto normalized = normalize_console_mode(mode);
    return normalized == kConsoleAlloc || normalized == kConsoleAttach ||
           normalized == kConsoleNone;
}

}  // namespace proton_inject
