#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace proton_inject {

extern std::function<void(const std::string&)> g_log_callback;

[[nodiscard]] std::string expand_path(std::string path);
[[nodiscard]] std::string steam_root();
[[nodiscard]] std::vector<std::string> steam_library_roots();
[[nodiscard]] std::string compat_data_path(const std::string& app_id);
[[nodiscard]] std::string mods_dir_for_app_id(const std::string& app_id);
[[nodiscard]] std::string to_windows_path(const std::string& path);
void debug(const std::string& message);

}  // namespace proton_inject
