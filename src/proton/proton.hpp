#pragma once

#include <expected>
#include <string>

namespace proton_inject {

struct ProtonInstall {
    std::string name;
    std::string path;

    [[nodiscard]] std::string script_path() const;
};

// a Proton build is identified by the `proton` launch script sitting in its directory.
[[nodiscard]] bool is_proton_dir(const std::string& dir);
[[nodiscard]] std::expected<ProtonInstall, std::string> resolve_proton(const std::string& app_id);

}  // namespace proton_inject
