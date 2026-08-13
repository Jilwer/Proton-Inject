#include "core/console_mode.hpp"

#include <algorithm>

namespace proton_inject {

std::string normalize_console_mode(const std::string_view mode) {
    std::string normalized(mode);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (normalized.empty() || normalized == "alloc" || normalized == "allocate" ||
        normalized == "new") {
        return std::string(kConsoleAlloc);
    }
    if (normalized == "attach" || normalized == "existing" || normalized == "reuse") {
        return std::string(kConsoleAttach);
    }
    if (normalized == "none" || normalized == "off" || normalized == "disabled") {
        return std::string(kConsoleNone);
    }
    return normalized;
}

bool valid_console_mode(const std::string_view mode) {
    const auto normalized = normalize_console_mode(mode);
    return normalized == kConsoleAlloc || normalized == kConsoleAttach ||
           normalized == kConsoleNone;
}

}  // namespace proton_inject
