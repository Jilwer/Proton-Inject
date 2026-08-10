#include "utils/utils.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace proton_inject {

std::function<void(const std::string&)> g_log_callback;

namespace {

std::string home_directory() {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return home;
    }
    return {};
}

std::vector<std::string> steam_root_candidates() {
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("STEAM_COMPAT_CLIENT_INSTALL_PATH");
        env != nullptr && *env != '\0') {
        candidates.emplace_back(env);
    }

    const auto home = home_directory();
    if (home.empty()) {
        return candidates;
    }

    candidates.emplace_back(home + "/.steam/root");
    candidates.emplace_back(home + "/.local/share/Steam");
    candidates.emplace_back(home + "/.steam/steam");
    candidates.emplace_back(home + "/.steam/debian-installation");
    candidates.emplace_back(home + "/.var/app/com.valvesoftware.Steam/data/Steam");
    candidates.emplace_back(home + "/.var/app/com.valvesoftware.Steam/.steam/steam");
    return candidates;
}

std::vector<std::string> parse_library_folders_vdf(const fs::path& path) {
    std::ifstream input(path);
    if (!input) {
        return {};
    }

    const std::string content((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
    static const std::regex path_re(R"rx("path"\s+"([^"]+)")rx");
    std::vector<std::string> paths;
    std::string seen;

    for (std::sregex_iterator it(content.begin(), content.end(), path_re), end; it != end; ++it) {
        const std::string value = (*it)[1].str();
        if (value.empty() || seen.find(value) != std::string::npos) {
            continue;
        }
        seen += value + '\n';
        paths.push_back(value);
    }
    return paths;
}

std::string capitalize_first(std::string message) {
    if (!message.empty() && std::islower(static_cast<unsigned char>(message.front()))) {
        message.front() =
            static_cast<char>(std::toupper(static_cast<unsigned char>(message.front())));
    }
    return message;
}

}  // namespace

std::string expand_path(std::string path) {
    while (!path.empty() && std::isspace(static_cast<unsigned char>(path.back()))) {
        path.pop_back();
    }
    while (!path.empty() && std::isspace(static_cast<unsigned char>(path.front()))) {
        path.erase(path.begin());
    }
    if (path.empty()) {
        return path;
    }
    if (path == "~") {
        return home_directory();
    }
    if (path.starts_with("~/")) {
        return home_directory() + path.substr(1);
    }
    return path;
}

std::string steam_root() {
    for (const auto& candidate : steam_root_candidates()) {
        if (fs::is_directory(candidate)) {
            return candidate;
        }
    }
    return {};
}

std::vector<std::string> steam_library_roots() {
    std::vector<std::string> roots;
    std::string seen;

    const auto add_root = [&](const std::string& root) {
        if (root.empty() || seen.find(root) != std::string::npos) {
            return;
        }
        seen += root + '\n';
        roots.push_back(root);
    };

    for (const auto& root : steam_root_candidates()) {
        if (!fs::is_directory(root)) {
            continue;
        }
        add_root(root);
        for (const auto& rel : {fs::path("steamapps") / "libraryfolders.vdf",
                                fs::path("config") / "libraryfolders.vdf"}) {
            for (const auto& library : parse_library_folders_vdf(fs::path(root) / rel)) {
                add_root(library);
            }
        }
    }
    return roots;
}

namespace {

std::vector<std::string> compat_data_candidates(const std::string& app_id) {
    std::vector<std::string> candidates;
    std::string seen;
    const auto trimmed = app_id;
    if (trimmed.empty()) {
        return candidates;
    }

    for (const auto& library : steam_library_roots()) {
        const auto path = (fs::path(library) / "steamapps" / "compatdata" / trimmed).string();
        if (seen.find(path) == std::string::npos) {
            seen += path + '\n';
            candidates.push_back(path);
        }
    }
    return candidates;
}

}  // namespace

std::string compat_data_path(const std::string& app_id) {
    if (const char* env = std::getenv("STEAM_COMPAT_DATA_PATH"); env != nullptr && *env != '\0') {
        return env;
    }
    for (const auto& candidate : compat_data_candidates(app_id)) {
        if (fs::is_directory(candidate)) {
            return candidate;
        }
    }
    return {};
}

std::string mods_dir_for_app_id(const std::string& app_id) {
    const fs::path suffix =
        fs::path("pfx") / "drive_c" / "users" / "steamuser" / "Documents" / "proton-inject-mods";

    auto candidates = compat_data_candidates(app_id);
    if (const auto preferred = compat_data_path(app_id); !preferred.empty()) {
        candidates.insert(candidates.begin(), preferred);
    }

    std::string seen;
    for (const auto& compat_root : candidates) {
        if (seen.find(compat_root) != std::string::npos) {
            continue;
        }
        seen += compat_root + '\n';
        const auto mods_path = fs::path(compat_root) / suffix;
        if (fs::exists(mods_path)) {
            return mods_path.string();
        }
    }
    return {};
}

std::string to_windows_path(const std::string& path) {
    std::error_code ec;
    const auto absolute = fs::absolute(path, ec);
    const auto& resolved = ec ? path : absolute.string();
    std::string wine_path = resolved;
    std::replace(wine_path.begin(), wine_path.end(), '/', '\\');
    return "Z:" + wine_path;
}

void debug(const std::string& message) {
    const auto formatted = capitalize_first(message);
    std::cerr << "DEBUG: " << formatted << '\n';
    if (g_log_callback) {
        g_log_callback("DEBUG: " + formatted);
    }
}

}  // namespace proton_inject
