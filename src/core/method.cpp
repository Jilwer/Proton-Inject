#include "core/method.hpp"

#include "utils/utils.hpp"

namespace proton_inject {

// "liat+ll" is what the GUI shows in its method combo, so typing what you see on screen into
// --method works. Everything else is the canonical id.
std::string normalize_method(const std::string_view method) {
    const auto normalized = to_lower(method);
    if (normalized.empty()) {
        return std::string(kMethodCrt);
    }
    if (normalized == "liat+ll") {
        return std::string(kMethodLiatLl);
    }
    return normalized;
}

bool valid_method(const std::string_view method) {
    const auto normalized = normalize_method(method);
    return normalized == kMethodCrt || normalized == kMethodApc || normalized == kMethodNt ||
           normalized == kMethodLiatLl;
}

bool is_linux_iat_method(const std::string_view method) {
    return normalize_method(method) == kMethodLiatLl;
}

}  // namespace proton_inject
