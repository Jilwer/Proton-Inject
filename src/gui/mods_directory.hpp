#pragma once

#include <string>
#include <vector>

// where the embedded loader looks for mods, and what is currently in there.
class ModsDirectory {
public:
    [[nodiscard]] static std::string for_app_id(const std::string& app_id);
    [[nodiscard]] static std::string for_prefix(const std::string& wine_prefix);
    [[nodiscard]] static std::vector<std::string> list_dlls(const std::string& mods_dir);
};
