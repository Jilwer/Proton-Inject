#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace proton_inject {

// everything an install writes, all under $HOME so the command never needs root.
struct InstallPaths {
    std::filesystem::path binary;
    std::filesystem::path desktop_entry;
    std::filesystem::path icons_root;
};

[[nodiscard]] InstallPaths install_paths();

// the icon shipped beside the binary, falling back to the build tree's copy. Empty when the
// running binary has neither.
[[nodiscard]] std::filesystem::path bundled_icon_path();

// the theme icon an install wrote, which only exists once --install has run.
[[nodiscard]] std::filesystem::path installed_icon_path();

// a copy in a directory the shell cannot reach still works from the menu, so this only
// decides whether the install is worth a warning.
[[nodiscard]] bool binary_dir_on_path();

// copies the running binary to its permanent home and registers it with the desktop.
[[nodiscard]] std::expected<void, std::string> install_app();

// removes what install_app wrote, leaving profiles and settings alone.
[[nodiscard]] std::expected<void, std::string> uninstall_app();

}  // namespace proton_inject
