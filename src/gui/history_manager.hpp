#pragma once

#include "injection_config.hpp"

#include <string>
#include <vector>

struct HistoryEntry {
    std::string label;
    InjectionConfig config;
};

class HistoryManager {
public:
    [[nodiscard]] std::vector<HistoryEntry> entries() const;
    void save(const InjectionConfig& config);
    [[nodiscard]] bool load_entry(int index, InjectionConfig* config) const;
};
