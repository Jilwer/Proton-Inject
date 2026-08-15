#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <sys/types.h>

namespace proton_inject {

[[nodiscard]] std::string exe_basename(std::string_view target);
[[nodiscard]] bool is_process_running(std::string_view process_name);
[[nodiscard]] std::expected<pid_t, std::string> find_wine_target_pid(std::string_view process_name);

}  // namespace proton_inject
