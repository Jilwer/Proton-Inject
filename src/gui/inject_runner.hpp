#pragma once

#include "injection_config.hpp"

#include <functional>
#include <string>

class InjectRunner {
public:
    using LogCallback = std::function<void(const std::string&)>;

    [[nodiscard]] const std::string& last_error() const { return m_last_error; }

    bool run(const InjectionConfig& config, const LogCallback& on_output = {});

    static std::string resolve_proton_executable(const std::string& proton_path);
    static bool has_valid_proton_path(const std::string& proton_path);
    static std::string process_name_from_exe(const std::string& exe_path);
    static bool is_bare_exe_name(const std::string& exe_path);
    static std::string resolve_launch_target(const std::string& app_id,
                                             const std::string& exe_path);

private:
    bool inject_once(const InjectionConfig& config, const std::string& dll_path);

    std::string m_last_error;
};
