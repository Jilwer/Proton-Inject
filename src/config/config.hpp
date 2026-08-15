#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace proton_inject {

struct AppConfig {
    std::optional<std::string> app_id;
    std::optional<std::string> target_exe;
    std::optional<std::string> dll_path;
    std::optional<bool> use_loader;
    std::optional<std::string> loader_console;
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

// the CLI and the GUI share these files, so both go through this one reader and writer.
class ConfigStore {
public:
    [[nodiscard]] static std::expected<ConfigStore, std::string> create();

    [[nodiscard]] std::expected<AppConfig, std::string> load_default() const;
    [[nodiscard]] std::expected<AppConfig, std::string> load_profile(const std::string& name) const;

    [[nodiscard]] std::expected<void, std::string> save_default(const AppConfig& config) const;
    [[nodiscard]] std::expected<void, std::string> save_profile(const std::string& name,
                                                                const AppConfig& config) const;

    [[nodiscard]] std::expected<void, std::string> create_profile(const std::string& name,
                                                                  const AppConfig& config) const;
    [[nodiscard]] std::expected<void, std::string> delete_profile(const std::string& name) const;

    [[nodiscard]] bool profile_exists(const std::string& name) const;
    [[nodiscard]] std::expected<std::vector<std::string>, std::string> list_profiles() const;
    [[nodiscard]] std::expected<std::vector<ProfileEntry>, std::string> list_profiles_with_config()
        const;

    [[nodiscard]] const std::string& config_dir() const { return config_dir_; }

private:
    ConfigStore(std::string config_dir, std::string profiles_dir);

    [[nodiscard]] std::string profile_path(const std::string& name) const;
    [[nodiscard]] std::expected<AppConfig, std::string> load_file(const std::string& path) const;
    [[nodiscard]] std::expected<void, std::string> save_file(const std::string& path,
                                                             const AppConfig& config) const;

    std::string config_dir_;
    std::string profiles_dir_;
};

}  // namespace proton_inject
