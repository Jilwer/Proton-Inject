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

}  // namespace

std::string ModsDirectory::for_app_id(const std::string& app_id) {
    return proton_inject::mods_dir_for_app_id(gui_util::trim(app_id));
}

std::string ModsDirectory::for_prefix(const std::string& wine_prefix) {
    const std::string prefix =
        wine_prefix.empty() ? gui_util::home_dir() + "/.proton-injector/pfx" : wine_prefix;
    const std::string path = prefix + "/" + k_mods_subpath;
    return fs::exists(path) ? path : std::string{};
}

std::vector<std::string> ModsDirectory::list_dlls(const std::string& mods_dir) {
    if (mods_dir.empty() || !fs::exists(mods_dir)) {
        return {};
    }

    std::vector<std::string> names;
    for (const auto& entry : fs::recursive_directory_iterator(
             mods_dir, fs::directory_options::skip_permission_denied)) {
        if (entry.is_regular_file() && lowercase(entry.path().extension().string()) == ".dll") {
            names.push_back(entry.path().filename().string());
        }
    }

    std::ranges::sort(names, {}, [](const std::string& name) { return lowercase(name); });
    return names;
}
