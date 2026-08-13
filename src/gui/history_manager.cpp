#include "history_manager.hpp"

#include "gui_util.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {

constexpr int k_max_history = 20;

std::string history_file_path() {
    return gui_util::home_dir() + "/.proton-injector/history";
}

std::string mode_to_string(InjectionMode mode) {
    if (mode == InjectionMode::NonSteam) {
        return "Non-steam Game (requires umu-run)";
    }
    return "Steam Game";
}

InjectionMode mode_from_string(const std::string& mode) {
    if (mode.rfind("Non-steam", 0) == 0) {
        return InjectionMode::NonSteam;
    }
    return InjectionMode::Steam;
}

std::string display_name_for(const InjectionConfig& config) {
    if (config.mode == InjectionMode::Steam) {
        return config.game_name;
    }
    const std::filesystem::path exe(config.exe_path);
    return exe.stem().string();
}

std::string current_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&time, &tm);
    std::ostringstream stream;
    stream << std::put_time(&tm, "%Y-%m-%d %H:%M");
    return stream.str();
}

}  // namespace

std::vector<HistoryEntry> HistoryManager::entries() const {
    std::vector<HistoryEntry> result;
    std::ifstream input(history_file_path());
    if (!input) {
        return result;
    }

    std::string line;
    while (std::getline(input, line)) {
        line = gui_util::trim(line);
        if (line.empty()) {
            continue;
        }

        const std::vector<std::string> parts = gui_util::split(line, '|');
        if (parts.size() < 9) {
            continue;
        }

        HistoryEntry entry;
        const bool use_loader = parts.size() > 10 ? parts[10] == "true" : false;
        const std::string& dll_field = parts[6];
        const std::string dll_label = use_loader ? "(mod loader)" : dll_field;
        entry.label = parts[0] + " | " + parts[3] + " | " + dll_label;

        InjectionConfig config;
        config.mode = mode_from_string(parts[1]);
        config.app_id = parts[2];
        config.game_name = parts[3];
        config.proton_path = parts[4];
        config.exe_path = parts[5];
        config.dll_paths = gui_util::split(dll_field, ',');
        config.use_loader = use_loader;
        config.method = parts[7];
        try {
            config.sleep_ms = std::stoi(parts[8]);
        } catch (const std::exception&) {
            config.sleep_ms = 0;
        }
        if (parts.size() > 9) {
            config.wine_prefix = parts[9];
        }
        if (parts.size() > 11 && !parts[11].empty()) {
            config.loader_console = parts[11];
        }

        entry.config = config;
        result.push_back(std::move(entry));
    }

    return result;
}

bool HistoryManager::load_entry(int index, InjectionConfig* config) const {
    const std::vector<HistoryEntry> list = entries();
    if (index < 0 || index >= static_cast<int>(list.size()) || config == nullptr) {
        return false;
    }
    *config = list[static_cast<std::size_t>(index)].config;
    return true;
}

void HistoryManager::save(const InjectionConfig& config) {
    const std::string timestamp = current_timestamp();
    const std::string dll_csv = gui_util::join(config.dll_paths, ",");

    const std::vector<std::string> fields = {
        timestamp,
        mode_to_string(config.mode),
        config.app_id,
        display_name_for(config),
        config.proton_path,
        config.exe_path,
        dll_csv,
        config.method,
        std::to_string(config.sleep_ms),
        config.wine_prefix,
        config.use_loader ? "true" : "false",
        config.loader_console,
    };

    std::vector<std::string> lines{gui_util::join(fields, "|")};

    std::ifstream existing(history_file_path());
    if (existing) {
        std::string line;
        while (std::getline(existing, line)) {
            line = gui_util::trim(line);
            if (!line.empty()) {
                lines.push_back(line);
            }
        }
    }

    while (static_cast<int>(lines.size()) > k_max_history) {
        lines.pop_back();
    }

    std::filesystem::create_directories(std::filesystem::path(history_file_path()).parent_path());
    std::ofstream output(history_file_path(), std::ios::trunc);
    if (!output) {
        return;
    }

    for (const std::string& entry : lines) {
        output << entry << '\n';
    }
}
