#include "method.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace injector {

Method parse_method(const std::string_view value) {
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (normalized.empty() || normalized == "crt" || normalized == "standard" ||
        normalized == "create_remote_thread") {
        return Method::Crt;
    }
    if (normalized == "apc") {
        return Method::Apc;
    }
    if (normalized == "nt" || normalized == "ntcreatethreadex") {
        return Method::Nt;
    }
    return Method::Crt;
}

const char* method_name(const Method method) {
    switch (method) {
        case Method::Apc:
            return "apc";
        case Method::Nt:
            return "nt";
        case Method::Crt:
        default:
            return "crt";
    }
}

}  // namespace injector
