#include "config/config.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>

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
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::string(home) + "/.config/proton-inject";
    }
    return std::unexpected("Failed to resolve home directory");
}

void to_json(nlohmann::json& json, const AppConfig& config) {
    json = nlohmann::json::object();
    if (config.app_id) {
        json["appid"] = *config.app_id;
    }
    if (config.target_exe) {
        json["exe"] = *config.target_exe;
    }
    if (config.dll_path) {
        json["dll_path"] = *config.dll_path;
    }
    if (config.use_loader) {
        json["use_loader"] = *config.use_loader;
    }
    if (config.loader_console) {
        json["loader_console"] = *config.loader_console;
    }
    if (config.method) {
        json["method"] = *config.method;
    }
    if (config.non_steam) {
        json["non_steam"] = *config.non_steam;
    }
    if (config.proton_path) {
        json["proton_path"] = *config.proton_path;
    }
    if (config.wine_prefix) {
        json["wine_prefix"] = *config.wine_prefix;
    }
    if (config.game_id) {
        json["game_id"] = *config.game_id;
    }
    if (config.sleep_ms) {
        json["sleep_ms"] = *config.sleep_ms;
    }
}

void from_json(const nlohmann::json& json, AppConfig& config) {
    if (json.contains("appid")) {
        config.app_id = json["appid"].get<std::string>();
    }
    if (json.contains("exe")) {
        config.target_exe = json["exe"].get<std::string>();
    }
    if (json.contains("dll_path")) {
        config.dll_path = json["dll_path"].get<std::string>();
    }
    if (json.contains("use_loader")) {
        config.use_loader = json["use_loader"].get<bool>();
    }
    if (json.contains("loader_console")) {
        config.loader_console = json["loader_console"].get<std::string>();
    }
    if (json.contains("method")) {
        config.method = json["method"].get<std::string>();
    }
    if (json.contains("non_steam")) {
        config.non_steam = json["non_steam"].get<bool>();
    }
    if (json.contains("proton_path")) {
        config.proton_path = json["proton_path"].get<std::string>();
    }
    if (json.contains("wine_prefix")) {
        config.wine_prefix = json["wine_prefix"].get<std::string>();
    }
    if (json.contains("game_id")) {
        config.game_id = json["game_id"].get<std::string>();
    }
    if (json.contains("sleep_ms")) {
        config.sleep_ms = json["sleep_ms"].get<std::uint32_t>();
    }
}

}  // namespace

std::expected<ConfigStore, std::string> ConfigStore::create() {
    const auto config_dir = default_config_dir();
    if (!config_dir) {
        return std::unexpected(config_dir.error());
    }

    const auto profiles_dir = fs::path(*config_dir) / "profiles";
    std::error_code ec;
    fs::create_directories(*config_dir, ec);
    if (ec) {
        return std::unexpected("Failed to create config directory " + *config_dir + ": " +
                               ec.message());
    }
    fs::create_directories(profiles_dir, ec);
    if (ec) {
        return std::unexpected("Failed to create profiles directory " + profiles_dir.string() +
                               ": " + ec.message());
    }
    return ConfigStore(*config_dir, profiles_dir.string());
}

ConfigStore::ConfigStore(std::string config_dir, std::string profiles_dir)
    : config_dir_(std::move(config_dir)), profiles_dir_(std::move(profiles_dir)) {}

std::expected<AppConfig, std::string> ConfigStore::load_file(const std::string& path) const {
    std::ifstream input(path);
    if (!input) {
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

    AppConfig config;
    from_json(json, config);
    return config;
}

std::expected<AppConfig, std::string> ConfigStore::load(const std::string* profile_name) const {
    if (profile_name != nullptr && !profile_name->empty()) {
        const auto path = fs::path(profiles_dir_) / (*profile_name + ".json");
        if (!fs::exists(path)) {
            return std::unexpected(
                "Profile \"" + *profile_name +
                "\" does not exist (use --profile-list to see available profiles)");
        }
        return load_file(path.string());
    }
    return load_file((fs::path(config_dir_) / "config.json").string());
}

std::expected<void, std::string> ConfigStore::save(const AppConfig& config,
                                                   const std::string* profile_name) const {
    const fs::path path = profile_name != nullptr && !profile_name->empty()
                              ? fs::path(profiles_dir_) / (*profile_name + ".json")
                              : fs::path(config_dir_) / "config.json";

    nlohmann::json json;
    to_json(json, config);

    std::ofstream output(path);
    if (!output) {
        return std::unexpected("Failed to write config file " + path.string());
    }
    output << json.dump(2) << '\n';
    return {};
}

std::expected<void, std::string> ConfigStore::create_profile(
    const std::string& name, const std::string* app_id, const std::string& exe,
    const std::string* dll, const std::optional<bool>& use_loader) const {
    std::string trimmed = name;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) {
        trimmed.erase(trimmed.begin());
    }
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) {
        trimmed.pop_back();
    }
    if (trimmed.empty()) {
        return std::unexpected("Profile name cannot be empty");
    }
    if (exe.empty()) {
        return std::unexpected("exe is required when creating a profile");
    }

    const auto profile_path = fs::path(profiles_dir_) / (trimmed + ".json");
    if (fs::exists(profile_path)) {
        return std::unexpected("Profile \"" + trimmed + "\" already exists");
    }

    const bool using_loader = use_loader.value_or(false);
    if (!using_loader && (dll == nullptr || dll->empty())) {
        return std::unexpected("dll is required when not using embedded loader");
    }

    AppConfig config;
    config.target_exe = exe;
    if (app_id != nullptr && !app_id->empty()) {
        config.app_id = *app_id;
    }
    if (use_loader.has_value()) {
        config.use_loader = use_loader;
    }
    if (using_loader) {
        config.dll_path = std::nullopt;
    } else {
        config.dll_path = *dll;
    }
    return save(config, &trimmed);
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
    for (const auto& name : *names) {
        const auto config = load(&name);
        if (!config) {
            continue;
        }
        entries.push_back(ProfileEntry{name, *config});
    }
    return entries;
}

std::expected<void, std::string> ConfigStore::delete_profile(const std::string& name) const {
    const auto profile_path = fs::path(profiles_dir_) / (name + ".json");
    if (!fs::exists(profile_path)) {
        return std::unexpected("Profile \"" + name + "\" does not exist");
    }
    std::error_code ec;
    fs::remove(profile_path, ec);
    if (ec) {
        return std::unexpected("Failed to delete profile: " + ec.message());
    }
    return {};
}

}  // namespace proton_inject
