#pragma once

#include "config/config.hpp"

#include <string>
#include <vector>

enum class InjectionMode { Steam, NonSteam };

// what the Inject tab currently holds. Persisted through proton_inject::ConfigStore, which is
// the same reader and writer the CLI uses.
struct InjectionConfig {
    InjectionMode mode = InjectionMode::Steam;
    std::string app_id;
    std::string game_name;
    std::string proton_path;
    std::string exe_path;
    // only the first entry survives a save: the on-disk format the CLI shares holds one DLL.
    std::vector<std::string> dll_paths;
    bool use_loader = false;
    std::string loader_console = "alloc";
    std::string method = "crt";
    int sleep_ms = 0;
    std::string wine_prefix;
    std::string game_id;
};

[[nodiscard]] proton_inject::AppConfig to_app_config(const InjectionConfig& config);
[[nodiscard]] InjectionConfig from_app_config(const proton_inject::AppConfig& config);
