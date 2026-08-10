#pragma once

#include <QProcess>
#include <QString>
#include <QStringList>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace gui_util {

inline std::string trim(const std::string& text) {
    const auto start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

inline std::string home_dir() {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return home;
    }
    return {};
}

inline std::string env_or_empty(const char* name) {
    if (const char* value = std::getenv(name); value != nullptr) {
        return value;
    }
    return {};
}

inline std::string config_dir() {
    const std::string xdg = env_or_empty("XDG_CONFIG_HOME");
    if (!xdg.empty()) {
        return xdg + "/proton-inject";
    }
    return home_dir() + "/.config/proton-inject";
}

inline std::vector<std::string> split(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : text) {
        if (ch == delimiter) {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += ch;
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

inline std::string join(const std::vector<std::string>& parts, const std::string& sep) {
    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            result += sep;
        }
        result += parts[i];
    }
    return result;
}

inline bool path_exists(const std::string& path) {
    return !path.empty() && std::filesystem::exists(path);
}

// spawned as an argv rather than a shell line: game and profile paths routinely contain
// spaces.
inline bool open_path(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    return QProcess::startDetached(QStringLiteral("xdg-open"), {QString::fromStdString(path)});
}

inline std::string find_executable(const std::string& name) {
    if (const char* path_env = std::getenv("PATH"); path_env != nullptr) {
        for (const std::string& entry : split(path_env, ':')) {
            const std::filesystem::path candidate = std::filesystem::path(entry) / name;
            if (std::filesystem::exists(candidate)) {
                return candidate.string();
            }
        }
    }
    return {};
}

}  // namespace gui_util
