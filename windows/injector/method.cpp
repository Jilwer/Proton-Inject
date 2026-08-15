#include "method.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace injector {

// proton-inject always passes a canonical id; the aliases are here for running the injector
// by hand inside a prefix.
Method parse_method(const std::string_view value) {
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (normalized == "apc") {
        return Method::Apc;
    }
    if (normalized == "nt") {
        return Method::Nt;
    }
    return Method::Crt;
}

}  // namespace injector
