#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace proton_inject {

struct InjectOptions {
    std::string app_id;
    std::string proton_path;
    std::string wine_prefix;
    std::string game_id;
    std::string target_exe;
    std::string dll_path;
    std::string method;
    std::string loader_console;
    std::vector<std::string> target_args;
    bool non_steam = false;
    bool use_loader = false;
    std::uint32_t sleep_ms = 0;
};

}  // namespace proton_inject
