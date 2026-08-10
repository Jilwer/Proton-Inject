#pragma once

#include <string>
#include <vector>

enum class InjectionMode { Steam, NonSteam };

struct InjectionConfig {
    InjectionMode mode = InjectionMode::Steam;
    std::string app_id;
    std::string game_name;
    std::string proton_path;
    std::string exe_path;
    std::vector<std::string> dll_paths;
    bool use_loader = false;
    std::string method = "crt";
    int sleep_ms = 0;
    std::string wine_prefix;
    std::string game_id;
};
