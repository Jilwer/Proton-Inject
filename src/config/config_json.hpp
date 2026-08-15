#pragma once

#include "config/config.hpp"

#include <nlohmann/json.hpp>

// kept out of config.hpp so the JSON library only reaches the two files that serialize.
namespace proton_inject {

[[nodiscard]] nlohmann::json config_to_json(const AppConfig& config);
[[nodiscard]] AppConfig config_from_json(const nlohmann::json& json);

}  // namespace proton_inject
