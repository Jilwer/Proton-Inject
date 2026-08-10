#include "proton/proton.hpp"

#include "proton/vdf.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <sstream>

namespace fs = std::filesystem;

namespace proton_inject {

std::string ProtonInstall::script_path() const {
    return (fs::path(path) / "proton").string();
}

namespace {

bool is_proton_dir(const std::string& dir) {
    if (dir.empty()) {
        return false;
    }
    std::error_code ec;
    return fs::is_regular_file(fs::path(dir) / "proton", ec);
}

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string proton_from_config_info(const fs::path& path) {
    std::ifstream input(path);
    if (!input) {
        return {};
    }

    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (!line.starts_with('/')) {
            continue;
        }
        auto dir = fs::path(line).lexically_normal().string();
        for (int depth = 0; depth < 6; ++depth) {
            if (is_proton_dir(dir)) {
                return dir;
            }
            const auto parent = fs::path(dir).parent_path().string();
            if (parent == dir) {
                break;
            }
            dir = parent;
        }
    }
    return {};
}

std::string compat_tool_name(const std::string& app_id) {
    for (const auto& root : steam_library_roots()) {
        const auto config = parse_vdf_file((fs::path(root) / "config" / "config.vdf").string());
        const auto* mapping = vdf_block(
            config, {"InstallConfigStore", "Software", "Valve", "Steam", "CompatToolMapping"});
        if (mapping == nullptr) {
            continue;
        }
        for (const auto& key : {app_id, std::string("0")}) {
            const auto* entry_value = vdf_lookup(*mapping, key);
            if (entry_value == nullptr) {
                continue;
            }
            if (const auto* entry = std::get_if<VdfObject>(entry_value)) {
                const auto name = trim(vdf_string(*entry, "name"));
                if (!name.empty()) {
                    return name;
                }
            }
        }
    }
    return {};
}

std::vector<std::string> compat_tool_dirs() {
    std::vector<std::string> dirs;
    std::string seen;

    const auto add = [&](const std::string& path) {
        if (path.empty() || seen.find(path) != std::string::npos) {
            return;
        }
        seen += path + '\n';
        dirs.push_back(path);
    };

    if (const char* extra = std::getenv("STEAM_EXTRA_COMPAT_TOOLS_PATHS"); extra != nullptr) {
        std::stringstream stream(extra);
        std::string item;
        while (std::getline(stream, item, ':')) {
            add(trim(item));
        }
    }

    for (const auto& root : steam_library_roots()) {
        add((fs::path(root) / "compatibilitytools.d").string());
    }
    add("/usr/share/steam/compatibilitytools.d");
    add("/usr/local/share/steam/compatibilitytools.d");
    if (const char* home = std::getenv("HOME"); home != nullptr) {
        add((fs::path(home) / ".steam" / "compatibilitytools.d").string());
    }
    return dirs;
}

std::pair<std::string, std::string> read_compat_tool_manifest(const fs::path& tool_dir) {
    const auto manifest = parse_vdf_file((tool_dir / "compatibilitytool.vdf").string());
    const auto* tools = vdf_block(manifest, {"compatibilitytools", "compat_tools"});
    if (tools == nullptr) {
        return {"", ""};
    }

    for (const auto& [name, value] : *tools) {
        if (const auto* entry = std::get_if<VdfObject>(&value)) {
            auto install_path = vdf_string(*entry, "install_path");
            if (install_path.empty() || install_path == ".") {
                install_path = tool_dir.string();
            } else if (!fs::path(install_path).is_absolute()) {
                install_path = (tool_dir / install_path).string();
            }
            return {name, install_path};
        }
    }
    return {"", ""};
}

// "Proton 9.0" and "Proton-9.0 (Beta)" both map to the compat tool id "proton_9".
constexpr std::array<std::pair<std::string_view, std::string_view>, 2> k_name_substitutions{
    {{"(Beta)", ""}, {"-", " "}}};

std::string official_proton_name(std::string dir_name);

std::string find_compat_tool(const std::string& name) {
    for (const auto& dir : compat_tool_dirs()) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) {
                break;
            }
            const auto tool_dir = entry.path().string();
            if (entry.path().filename() == name && is_proton_dir(tool_dir)) {
                return tool_dir;
            }
            const auto [declared, install_path] = read_compat_tool_manifest(entry.path());
            if (declared == name && is_proton_dir(install_path)) {
                return install_path;
            }
        }
    }

    for (const auto& root : steam_library_roots()) {
        const fs::path common = fs::path(root) / "steamapps" / "common";
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(common, ec)) {
            if (ec) {
                break;
            }
            const auto tool_dir = entry.path().string();
            if (official_proton_name(entry.path().filename().string()) == name &&
                is_proton_dir(tool_dir)) {
                return tool_dir;
            }
        }
    }
    return {};
}

std::string official_proton_name(std::string dir_name) {
    if (!dir_name.starts_with("Proton")) {
        return {};
    }
    dir_name.erase(0, 6);
    dir_name = trim(dir_name);
    for (const auto& [token, replacement] : k_name_substitutions) {
        for (auto pos = dir_name.find(token); pos != std::string::npos;
             pos = dir_name.find(token, pos)) {
            dir_name.replace(pos, token.size(), replacement);
        }
    }
    dir_name = trim(dir_name);
    if (dir_name.empty()) {
        return {};
    }

    const auto dot = dir_name.find('.');
    if (dot == std::string::npos) {
        std::transform(dir_name.begin(), dir_name.end(), dir_name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return "proton_" + dir_name;
    }

    const auto major = dir_name.substr(0, dot);
    const auto minor = dir_name.substr(dot + 1);
    if (minor == "0") {
        return "proton_" + major;
    }
    return "proton_" + major + minor;
}

std::string resolve_error(const std::string& app_id, const std::vector<std::string>& tried) {
    std::ostringstream message;
    message << "Could not determine which Proton build app " << app_id << " uses (checked: ";
    for (std::size_t i = 0; i < tried.size(); ++i) {
        if (i > 0) {
            message << ", ";
        }
        message << tried[i];
    }
    message << "); launch the game through Steam at least once, or set PROTON_PATH to the Proton "
               "directory";
    return message.str();
}

}  // namespace

std::expected<ProtonInstall, std::string> resolve_proton(const std::string& app_id) {
    std::vector<std::string> tried;

    if (const char* env = std::getenv("PROTON_PATH"); env != nullptr && *env != '\0') {
        auto dir = expand_path(env);
        if (fs::path(dir).filename() == "proton") {
            dir = fs::path(dir).parent_path().string();
        }
        if (!is_proton_dir(dir)) {
            return std::unexpected("PROTON_PATH=" + std::string(env) +
                                   " does not contain a `proton` script");
        }
        debug("Using Proton from PROTON_PATH: " + dir);
        return ProtonInstall{fs::path(dir).filename().string(), dir};
    }

    if (const auto compat_data = compat_data_path(app_id); !compat_data.empty()) {
        if (const auto dir = proton_from_config_info(fs::path(compat_data) / "config_info");
            !dir.empty()) {
            debug("Resolved Proton from prefix config_info: " + dir);
            return ProtonInstall{fs::path(dir).filename().string(), dir};
        }
        tried.emplace_back("prefix config_info");
    }

    const auto tool_name = compat_tool_name(app_id);
    if (tool_name.empty()) {
        tried.emplace_back("config.vdf compat tool mapping");
        return std::unexpected(resolve_error(app_id, tried));
    }

    debug("Steam maps app " + app_id + " to compat tool \"" + tool_name + "\"");

    if (const auto dir = find_compat_tool(tool_name); !dir.empty()) {
        debug("Resolved Proton from compat tool search: " + dir);
        return ProtonInstall{tool_name, dir};
    }

    tried.push_back("compat tool \"" + tool_name + "\" on disk");
    return std::unexpected(resolve_error(app_id, tried));
}

}  // namespace proton_inject
