#include "utils/utils.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <unistd.h>

namespace fs = std::filesystem;

namespace proton_inject {

std::function<void(const std::string&)> g_log_callback;

namespace {

std::vector<std::string> steam_root_candidates() {
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("STEAM_COMPAT_CLIENT_INSTALL_PATH");
        env != nullptr && *env != '\0') {
        candidates.emplace_back(env);
    }

    const auto home = home_dir();
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
    std::set<std::string> seen;

    for (std::sregex_iterator it(content.begin(), content.end(), path_re), end; it != end; ++it) {
        const std::string value = (*it)[1].str();
        if (value.empty() || !seen.insert(value).second) {
            continue;
        }
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

std::vector<std::string> compat_data_candidates(const std::string& app_id) {
    std::vector<std::string> candidates;
    if (app_id.empty()) {
        return candidates;
    }

    std::set<std::string> seen;
    for (const auto& library : steam_library_roots()) {
        auto path = (fs::path(library) / "steamapps" / "compatdata" / app_id).string();
        if (seen.insert(path).second) {
            candidates.push_back(std::move(path));
        }
    }
    return candidates;
}

}  // namespace

std::string trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return std::string(value);
}

std::string to_lower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::vector<std::string> split(std::string_view text, char delimiter) {
    std::vector<std::string> parts;
    for (auto& piece : split_fields(text, delimiter)) {
        if (!piece.empty()) {
            parts.push_back(std::move(piece));
        }
    }
    return parts;
}

std::vector<std::string> split_fields(std::string_view text, char delimiter) {
    std::vector<std::string> parts;
    while (true) {
        const auto pos = text.find(delimiter);
        parts.emplace_back(text.substr(0, pos));
        if (pos == std::string_view::npos) {
            return parts;
        }
        text.remove_prefix(pos + 1);
    }
}

std::string home_dir() {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return home;
    }
    return {};
}

std::string expand_path(std::string_view path) {
    const std::string trimmed = trim(path);
    if (trimmed.empty()) {
        return trimmed;
    }
    if (trimmed == "~") {
        return home_dir();
    }
    if (trimmed.starts_with("~/")) {
        return home_dir() + trimmed.substr(1);
    }
    return trimmed;
}

bool is_executable(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    return !fs::is_directory(path, ec) && ::access(path.c_str(), X_OK) == 0;
}

std::string find_in_path(std::string_view name) {
    if (is_executable(std::string(name))) {
        return std::string(name);
    }

    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return {};
    }
    for (const auto& directory : split(path_env, ':')) {
        if (const auto candidate = (fs::path(directory) / name).string();
            is_executable(candidate)) {
            return candidate;
        }
    }
    return {};
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
    std::set<std::string> seen;

    const auto add_root = [&](const std::string& root) {
        if (!root.empty() && seen.insert(root).second) {
            roots.push_back(root);
        }
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
    auto candidates = compat_data_candidates(app_id);
    if (const auto preferred = compat_data_path(app_id); !preferred.empty()) {
        candidates.insert(candidates.begin(), preferred);
    }

    std::set<std::string> seen;
    for (const auto& compat_root : candidates) {
        if (!seen.insert(compat_root).second) {
            continue;
        }
        const auto mods_path = fs::path(compat_root) / "pfx" / kModsSubpath;
        if (fs::exists(mods_path)) {
            return mods_path.string();
        }
    }
    return {};
}

std::string state_dir() {
    const auto home = home_dir();
    return home.empty() ? std::string{} : home + "/.proton-inject";
}

std::string default_wine_prefix() {
    const auto state = state_dir();
    return state.empty() ? std::string{} : state + "/pfx";
}

void migrate_legacy_state_dir() {
    const auto home = home_dir();
    if (home.empty()) {
        return;
    }

    const fs::path legacy = fs::path(home) / ".proton-injector";
    const fs::path current = fs::path(home) / ".proton-inject";
    std::error_code ec;
    if (!fs::is_directory(legacy, ec) || fs::exists(current, ec)) {
        return;
    }

    fs::rename(legacy, current, ec);
    if (ec) {
        debug("Could not move " + legacy.string() + " to " + current.string() + ": " +
              ec.message());
        return;
    }
    debug("Moved state directory " + legacy.string() + " to " + current.string());
}

std::string relocate_legacy_state_path(std::string path) {
    const auto home = home_dir();
    if (home.empty()) {
        return path;
    }
    const std::string legacy = home + "/.proton-injector";
    if (path.starts_with(legacy)) {
        path.replace(0, legacy.size(), home + "/.proton-inject");
    }
    return path;
}

std::string to_windows_path(const std::string& path) {
    std::error_code ec;
    const auto absolute = fs::absolute(path, ec);
    std::string wine_path = ec ? path : absolute.string();
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
