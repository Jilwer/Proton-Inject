#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace proton_inject {

extern std::function<void(const std::string&)> g_log_callback;

[[nodiscard]] std::string trim(std::string_view value);
[[nodiscard]] std::string to_lower(std::string_view value);
// drops empty pieces: for lists where "a::b" means two entries, such as PATH.
[[nodiscard]] std::vector<std::string> split(std::string_view text, char delimiter);
// keeps empty pieces, so position still identifies the field in a delimited record.
[[nodiscard]] std::vector<std::string> split_fields(std::string_view text, char delimiter);

[[nodiscard]] std::string home_dir();
[[nodiscard]] std::string expand_path(std::string_view path);
[[nodiscard]] bool is_executable(const std::string& path);
[[nodiscard]] std::string find_in_path(std::string_view name);

[[nodiscard]] std::string steam_root();
[[nodiscard]] std::vector<std::string> steam_library_roots();
[[nodiscard]] std::string compat_data_path(const std::string& app_id);
[[nodiscard]] std::string mods_dir_for_app_id(const std::string& app_id);

// the loader writes into the prefix's Documents folder, so the tail is the same whether the
// prefix came from Steam's compatdata or from umu.
inline constexpr std::string_view kModsSubpath =
    "drive_c/users/steamuser/Documents/proton-inject-mods";

// non-Steam prefix and GUI state both live here, under the same name the config dir uses.
[[nodiscard]] std::string state_dir();
[[nodiscard]] std::string default_wine_prefix();

// releases up to 1.3.2 wrote the prefix and history under ~/.proton-injector, one letter off
// from every other path the project uses. Moves it once, at startup. Remove after 1.4.
void migrate_legacy_state_dir();

// saved configs still name the pre-1.3.3 directory; point them at where it was moved to.
// Remove after 1.4.
[[nodiscard]] std::string relocate_legacy_state_path(std::string path);

[[nodiscard]] std::string to_windows_path(const std::string& path);
void debug(const std::string& message);

}  // namespace proton_inject
