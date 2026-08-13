#pragma once

#include <expected>
#include <string>
#include <sys/types.h>

namespace proton_inject {

[[nodiscard]] std::expected<void, std::string> inject_dll_via_iat(
    pid_t pid, const std::string& linux_dll_path);

}  // namespace proton_inject
