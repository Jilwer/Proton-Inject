#include "core/method.hpp"

#include <algorithm>

namespace proton_inject {

std::string normalize_method(const std::string_view method) {
    std::string normalized(method);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (normalized.empty() || normalized == "crt" || normalized == "standard" ||
        normalized == "create_remote_thread") {
        return std::string(kMethodCrt);
    }
    if (normalized == "apc") {
        return std::string(kMethodApc);
    }
    if (normalized == "nt" || normalized == "ntcreatethreadex") {
        return std::string(kMethodNt);
    }
    if (normalized == "liatll" || normalized == "liat+ll" || normalized == "liat-ll" ||
        normalized == "liat" || normalized == "iatll" || normalized == "iat+ll" ||
        normalized == "iat-ll" || normalized == "iat" || normalized == "mem" ||
        normalized == "proc_mem" || normalized == "native" || normalized == "linux") {
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
