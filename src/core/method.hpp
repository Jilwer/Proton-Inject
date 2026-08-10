#pragma once

#include <string>
#include <string_view>

namespace proton_inject {

inline constexpr std::string_view kMethodCrt = "crt";
inline constexpr std::string_view kMethodApc = "apc";
inline constexpr std::string_view kMethodNt = "nt";

[[nodiscard]] std::string normalize_method(std::string_view method);
[[nodiscard]] bool valid_method(std::string_view method);

}  // namespace proton_inject
