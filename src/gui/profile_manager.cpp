#include "profile_manager.hpp"

#include "gui_util.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

std::string optional_string(const nlohmann::json& obj, const char* key) {
    if (!obj.contains(key) || !obj[key].is_string()) {
        return {};
    }
    return obj[key].get<std::string>();
}

bool optional_bool(const nlohmann::json& obj, const char* key, bool default_value) {
    if (!obj.contains(key)) {
        return default_value;
    }
    return obj[key].get<bool>();
}

int optional_int(const nlohmann::json& obj, const char* key, int default_value) {
    if (!obj.contains(key)) {
        return default_value;
    }
    return obj[key].get<int>();
}

}  // namespace

std::string ProfileManager::config_directory() {
    return gui_util::config_dir();
}

std::string ProfileManager::profiles_directory() {
    return config_directory() + "/profiles";
}

std::string ProfileManager::profile_path(const std::string& name) const {
    return profiles_directory() + "/" + name + ".json";
}

std::vector<std::string> ProfileManager::list_profiles() const {
    const fs::path dir(profiles_directory());
    if (!fs::exists(dir)) {
        return {};
    }

    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool ProfileManager::profile_exists(const std::string& name) const {
    return fs::exists(profile_path(name));
}

InjectionConfig ProfileManager::config_from_json(const nlohmann::json& obj) {
    InjectionConfig config;

    const bool non_steam = optional_bool(obj, "non_steam", false);
    config.mode = non_steam ? InjectionMode::NonSteam : InjectionMode::Steam;

    config.app_id = optional_string(obj, "appid");
    config.exe_path = optional_string(obj, "exe");
    config.use_loader = optional_bool(obj, "use_loader", true);

    config.loader_console = optional_string(obj, "loader_console");
    if (config.loader_console.empty()) {
        config.loader_console = "alloc";
    }

    const std::string dll_path = optional_string(obj, "dll_path");
    if (!dll_path.empty()) {
        config.dll_paths = {dll_path};
    }

    config.method = optional_string(obj, "method");
    if (config.method.empty()) {
        config.method = "crt";
    }

    config.proton_path = optional_string(obj, "proton_path");
    config.wine_prefix = optional_string(obj, "wine_prefix");
    config.game_id = optional_string(obj, "game_id");
    config.sleep_ms = optional_int(obj, "sleep_ms", 0);

    return config;
}

nlohmann::json ProfileManager::json_from_config(const InjectionConfig& config) {
    nlohmann::json obj = nlohmann::json::object();

    if (!config.app_id.empty()) {
        obj["appid"] = config.app_id;
    }
    if (!config.exe_path.empty()) {
        obj["exe"] = config.exe_path;
    }

    obj["use_loader"] = config.use_loader;

    if (config.use_loader && !config.loader_console.empty()) {
        obj["loader_console"] = config.loader_console;
    }

    if (!config.use_loader && !config.dll_paths.empty()) {
        obj["dll_path"] = config.dll_paths.front();
    }

    if (!config.method.empty()) {
        obj["method"] = config.method;
    }

    if (config.mode == InjectionMode::NonSteam) {
        obj["non_steam"] = true;
    }

    if (!config.proton_path.empty()) {
        obj["proton_path"] = config.proton_path;
    }
    if (!config.wine_prefix.empty()) {
        obj["wine_prefix"] = config.wine_prefix;
    }
    if (!config.game_id.empty()) {
        obj["game_id"] = config.game_id;
    }

    if (config.sleep_ms > 0) {
        obj["sleep_ms"] = config.sleep_ms;
    }

    return obj;
}

bool ProfileManager::load_profile(const std::string& name, InjectionConfig* config) const {
    if (config == nullptr) {
        return false;
    }

    std::ifstream input(profile_path(name));
    if (!input) {
        return false;
    }

    nlohmann::json doc;
    try {
        input >> doc;
    } catch (const nlohmann::json::exception&) {
        return false;
    }

    if (!doc.is_object()) {
        return false;
    }

    *config = config_from_json(doc);
    return true;
}

bool ProfileManager::save_profile(const std::string& name, const InjectionConfig& config) {
    fs::create_directories(profiles_directory());

    std::ofstream output(profile_path(name));
    if (!output) {
        return false;
    }

    output << json_from_config(config).dump(2);
    return static_cast<bool>(output);
}

bool ProfileManager::create_profile(const std::string& name, const InjectionConfig& config) {
    const std::string trimmed = gui_util::trim(name);
    if (trimmed.empty()) {
        return false;
    }
    if (config.exe_path.empty()) {
        return false;
    }
    if (!config.use_loader && config.dll_paths.empty()) {
        return false;
    }
    if (profile_exists(trimmed)) {
        return false;
    }

    return save_profile(trimmed, config);
}

bool ProfileManager::delete_profile(const std::string& name) {
    return fs::remove(profile_path(name));
}
