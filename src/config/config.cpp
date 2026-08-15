#include "config/config.hpp"

#include "config/config_json.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace proton_inject {

bool AppConfig::use_loader_or_default() const {
    return !use_loader.has_value() || *use_loader;
}

namespace {

std::expected<std::string, std::string> default_config_dir() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::string(xdg) + "/proton-inject";
    }
    if (const auto home = home_dir(); !home.empty()) {
        return home + "/.config/proton-inject";
    }
    return std::unexpected("Failed to resolve home directory");
}

template<typename T>
void store(nlohmann::json& json, const char* key, const std::optional<T>& value) {
    if (value.has_value()) {
        json[key] = *value;
    }
}

template<typename T>
void load(const nlohmann::json& json, const char* key, std::optional<T>& value) {
    if (const auto it = json.find(key); it != json.end() && !it->is_null()) {
        value = it->get<T>();
    }
}

}  // namespace

nlohmann::json config_to_json(const AppConfig& config) {
    nlohmann::json json = nlohmann::json::object();
    store(json, "appid", config.app_id);
    store(json, "exe", config.target_exe);
    store(json, "dll_path", config.dll_path);
    store(json, "use_loader", config.use_loader);
    store(json, "loader_console", config.loader_console);
    store(json, "method", config.method);
    store(json, "non_steam", config.non_steam);
    store(json, "proton_path", config.proton_path);
    store(json, "wine_prefix", config.wine_prefix);
    store(json, "game_id", config.game_id);
    store(json, "sleep_ms", config.sleep_ms);
    return json;
}

AppConfig config_from_json(const nlohmann::json& json) {
    AppConfig config;
    load(json, "appid", config.app_id);
    load(json, "exe", config.target_exe);
    load(json, "dll_path", config.dll_path);
    load(json, "use_loader", config.use_loader);
    load(json, "loader_console", config.loader_console);
    load(json, "method", config.method);
    load(json, "non_steam", config.non_steam);
    load(json, "proton_path", config.proton_path);
    load(json, "wine_prefix", config.wine_prefix);
    load(json, "game_id", config.game_id);
    load(json, "sleep_ms", config.sleep_ms);

    if (config.wine_prefix.has_value()) {
        config.wine_prefix = relocate_legacy_state_path(*config.wine_prefix);
    }
    return config;
}

std::expected<ConfigStore, std::string> ConfigStore::create() {
    const auto config_dir = default_config_dir();
    if (!config_dir) {
        return std::unexpected(config_dir.error());
    }

    const auto profiles_dir = fs::path(*config_dir) / "profiles";
    std::error_code ec;
    fs::create_directories(profiles_dir, ec);
    if (ec) {
        return std::unexpected("Failed to create profiles directory " + profiles_dir.string() +
                               ": " + ec.message());
    }
    return ConfigStore(*config_dir, profiles_dir.string());
}

ConfigStore::ConfigStore(std::string config_dir, std::string profiles_dir)
    : config_dir_(std::move(config_dir)), profiles_dir_(std::move(profiles_dir)) {}

std::string ConfigStore::profile_path(const std::string& name) const {
    return (fs::path(profiles_dir_) / (name + ".json")).string();
}

std::expected<AppConfig, std::string> ConfigStore::load_file(const std::string& path) const {
    std::ifstream input(path);
    if (!input) {
        // a config that was never written is an empty one, not a failure.
        if (!fs::exists(path)) {
            return AppConfig{};
        }
        return std::unexpected("Failed to read config file " + path);
    }

    nlohmann::json json;
    try {
        input >> json;
    } catch (const nlohmann::json::exception& ex) {
        return std::unexpected("Failed to parse config file " + path + ": " + ex.what());
    }
    if (!json.is_object()) {
        return std::unexpected("Config file " + path + " is not a JSON object");
    }
    return config_from_json(json);
}

std::expected<void, std::string> ConfigStore::save_file(const std::string& path,
                                                        const AppConfig& config) const {
    std::ofstream output(path);
    if (!output) {
        return std::unexpected("Failed to write config file " + path);
    }
    output << config_to_json(config).dump(2) << '\n';
    if (!output) {
        return std::unexpected("Failed to write config file " + path);
    }
    return {};
}

std::expected<AppConfig, std::string> ConfigStore::load_default() const {
    return load_file((fs::path(config_dir_) / "config.json").string());
}

std::expected<AppConfig, std::string> ConfigStore::load_profile(const std::string& name) const {
    const auto path = profile_path(name);
    if (!fs::exists(path)) {
        return std::unexpected("Profile \"" + name +
                               "\" does not exist (use --profile-list to see available profiles)");
    }
    return load_file(path);
}

std::expected<void, std::string> ConfigStore::save_default(const AppConfig& config) const {
    return save_file((fs::path(config_dir_) / "config.json").string(), config);
}

std::expected<void, std::string> ConfigStore::save_profile(const std::string& name,
                                                           const AppConfig& config) const {
    return save_file(profile_path(name), config);
}

std::expected<void, std::string> ConfigStore::create_profile(const std::string& name,
                                                             const AppConfig& config) const {
    const auto trimmed = trim(name);
    if (trimmed.empty()) {
        return std::unexpected("Profile name cannot be empty");
    }
    if (!config.target_exe.has_value() || config.target_exe->empty()) {
        return std::unexpected("exe is required when creating a profile");
    }
    if (!config.use_loader_or_default() &&
        (!config.dll_path.has_value() || config.dll_path->empty())) {
        return std::unexpected("dll is required when not using embedded loader");
    }
    if (profile_exists(trimmed)) {
        return std::unexpected("Profile \"" + trimmed + "\" already exists");
    }
    return save_profile(trimmed, config);
}

std::expected<void, std::string> ConfigStore::delete_profile(const std::string& name) const {
    const auto path = profile_path(name);
    if (!fs::exists(path)) {
        return std::unexpected("Profile \"" + name + "\" does not exist");
    }
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) {
        return std::unexpected("Failed to delete profile: " + ec.message());
    }
    return {};
}

bool ConfigStore::profile_exists(const std::string& name) const {
    return fs::exists(profile_path(name));
}

std::expected<std::vector<std::string>, std::string> ConfigStore::list_profiles() const {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(profiles_dir_, ec)) {
        if (ec) {
            return std::unexpected("Failed to read profiles directory " + profiles_dir_ + ": " +
                                   ec.message());
        }
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::expected<std::vector<ProfileEntry>, std::string> ConfigStore::list_profiles_with_config()
    const {
    const auto names = list_profiles();
    if (!names) {
        return std::unexpected(names.error());
    }

    std::vector<ProfileEntry> entries;
    entries.reserve(names->size());
    for (const auto& name : *names) {
        // a profile that will not parse is skipped rather than hiding the rest of the list.
        if (const auto config = load_profile(name); config) {
            entries.push_back(ProfileEntry{name, *config});
        }
    }
    return entries;
}

}  // namespace proton_inject
