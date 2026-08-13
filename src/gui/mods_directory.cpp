#include "mods_directory.hpp"

#include "gui_util.hpp"

#include "utils/utils.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

// the loader writes into the prefix's Documents folder, so the path is the same whether the
// prefix came from Steam's compatdata or from umu.
constexpr const char* k_mods_subpath = "drive_c/users/steamuser/Documents/proton-inject-mods";

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

std::string default_prefix() {
    return gui_util::home_dir() + "/.proton-injector/pfx";
}

std::string mods_path_in(const std::string& prefix) {
    return fs::is_directory(prefix) ? prefix + "/" + k_mods_subpath : std::string{};
}

}  // namespace

std::string ModsDirectory::for_app_id(const std::string& app_id) {
    const std::string trimmed = gui_util::trim(app_id);
    // an existing folder wins: it says which library the game was actually installed into.
    if (const std::string existing = proton_inject::mods_dir_for_app_id(trimmed);
        !existing.empty()) {
        return existing;
    }
    const std::string compat_data = proton_inject::compat_data_path(trimmed);
    return compat_data.empty() ? std::string{} : mods_path_in(compat_data + "/pfx");
}

std::string ModsDirectory::for_prefix(const std::string& wine_prefix) {
    return mods_path_in(wine_prefix.empty() ? default_prefix() : wine_prefix);
}

std::vector<std::string> ModsDirectory::list_dlls(const std::string& mods_dir) {
    if (mods_dir.empty() || !fs::exists(mods_dir)) {
        return {};
    }

    std::vector<std::string> names;
    for (const auto& entry : fs::recursive_directory_iterator(
             mods_dir, fs::directory_options::skip_permission_denied)) {
        if (entry.is_regular_file() && lowercase(entry.path().extension().string()) == ".dll") {
            names.push_back(entry.path().lexically_relative(mods_dir).string());
        }
    }

    std::ranges::sort(names, {}, [](const std::string& name) { return lowercase(name); });
    return names;
}

// mods are copied in rather than linked, so moving the source afterwards cannot break the
// game's prefix.
std::expected<void, std::string> ModsDirectory::add(const std::string& mods_dir,
                                                    const std::string& dll_path) {
    std::error_code ec;
    const fs::path destination = fs::path(mods_dir) / fs::path(dll_path).filename();
    if (fs::equivalent(dll_path, destination, ec)) {
        return {};
    }

    fs::create_directories(mods_dir, ec);
    if (ec) {
        return std::unexpected("Could not create " + mods_dir + ": " + ec.message());
    }

    fs::copy_file(dll_path, destination, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return std::unexpected("Could not copy " + dll_path + ": " + ec.message());
    }
    return {};
}

std::expected<void, std::string> ModsDirectory::remove(const std::string& mods_dir,
                                                       const std::string& name) {
    std::error_code ec;
    const fs::path path = fs::path(mods_dir) / name;
    if (!fs::remove(path, ec)) {
        return std::unexpected("Could not remove " + path.string() +
                               (ec ? ": " + ec.message() : ": file is already gone"));
    }
    return {};
}
