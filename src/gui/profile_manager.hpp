#pragma once

#include "injection_config.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class ProfileManager {
public:
    static std::string config_directory();

    [[nodiscard]] std::vector<std::string> list_profiles() const;
    [[nodiscard]] bool profile_exists(const std::string& name) const;

    [[nodiscard]] bool load_profile(const std::string& name, InjectionConfig* config) const;
    bool save_profile(const std::string& name, const InjectionConfig& config);
    bool create_profile(const std::string& name, const InjectionConfig& config);
    bool delete_profile(const std::string& name);

private:
    static std::string profiles_directory();
    static InjectionConfig config_from_json(const nlohmann::json& obj);
    static nlohmann::json json_from_config(const InjectionConfig& config);
    [[nodiscard]] std::string profile_path(const std::string& name) const;
};
