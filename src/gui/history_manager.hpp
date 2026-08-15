#pragma once

#include "injection_config.hpp"

#include <optional>
#include <string>
#include <vector>

struct HistoryEntry {
    std::string label;
    InjectionConfig config;
};

// recently injected configurations, newest first.
class HistoryManager {
public:
    [[nodiscard]] std::vector<HistoryEntry> entries() const;
    [[nodiscard]] std::optional<InjectionConfig> entry_at(std::size_t index) const;
    void save(const InjectionConfig& config);
};
