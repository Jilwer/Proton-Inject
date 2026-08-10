#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace proton_inject {

struct AppConfig {
    std::optional<std::string> app_id;
    std::optional<std::string> target_exe;
    std::optional<std::string> dll_path;
    std::optional<bool> use_loader;
    std::optional<std::string> method;
    std::optional<bool> non_steam;
    std::optional<std::string> proton_path;
    std::optional<std::string> wine_prefix;
    std::optional<std::string> game_id;
    std::optional<std::uint32_t> sleep_ms;

    [[nodiscard]] bool use_loader_or_default() const;
};

struct ProfileEntry {
    std::string name;
    AppConfig config;
};

class ConfigStore {
public:
    static std::expected<ConfigStore, std::string> create();

    [[nodiscard]] std::expected<AppConfig, std::string> load(const std::string* profile_name) const;
    [[nodiscard]] std::expected<void, std::string> save(const AppConfig& config,
                                                        const std::string* profile_name) const;
    [[nodiscard]] std::expected<void, std::string> create_profile(
        const std::string& name, const std::string* app_id, const std::string& exe,
        const std::string* dll, const std::optional<bool>& use_loader) const;
    [[nodiscard]] std::expected<std::vector<std::string>, std::string> list_profiles() const;
    [[nodiscard]] std::expected<std::vector<ProfileEntry>, std::string> list_profiles_with_config()
        const;
    [[nodiscard]] std::expected<void, std::string> delete_profile(const std::string& name) const;

    [[nodiscard]] const std::string& config_dir() const { return config_dir_; }

private:
    explicit ConfigStore(std::string config_dir, std::string profiles_dir);

    [[nodiscard]] std::expected<AppConfig, std::string> load_file(const std::string& path) const;

    std::string config_dir_;
    std::string profiles_dir_;
};

}  // namespace proton_inject
