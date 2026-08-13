#pragma once

#include <expected>
#include <string>
#include <vector>

// where the embedded loader looks for mods, and what is currently in there.
class ModsDirectory {
public:
    // the folder the loader reads, whether or not it has been created yet. Empty when the
    // game has no prefix, since there is nowhere to put mods until it does.
    [[nodiscard]] static std::string for_app_id(const std::string& app_id);
    [[nodiscard]] static std::string for_prefix(const std::string& wine_prefix);

    // paths relative to `mods_dir`, so a mod inside a subfolder stays addressable.
    [[nodiscard]] static std::vector<std::string> list_dlls(const std::string& mods_dir);

    [[nodiscard]] static std::expected<void, std::string> add(const std::string& mods_dir,
                                                              const std::string& dll_path);
    [[nodiscard]] static std::expected<void, std::string> remove(const std::string& mods_dir,
                                                                 const std::string& name);
};
