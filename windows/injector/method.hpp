#pragma once

#include <string_view>

namespace injector {

enum class Method { Crt, Apc, Nt };

[[nodiscard]] Method parse_method(std::string_view value);
[[nodiscard]] const char* method_name(Method method);

}  // namespace injector
