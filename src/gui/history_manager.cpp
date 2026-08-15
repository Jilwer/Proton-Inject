#include "history_manager.hpp"

#include "config/config_json.hpp"
#include "utils/utils.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxHistory = 20;

std::string history_path() {
    const auto state = proton_inject::state_dir();
    return state.empty() ? std::string{} : state + "/history.json";
}

// pipe-delimited file written by releases up to 1.3.2, read once so the Recent list survives
// the upgrade. The next save writes history.json instead. Remove after 1.4.
std::string legacy_history_path() {
    const auto state = proton_inject::state_dir();
    return state.empty() ? std::string{} : state + "/history";
}

std::string current_timestamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_r(&now, &local);
    std::ostringstream stream;
    stream << std::put_time(&local, "%Y-%m-%d %H:%M");
    return stream.str();
}

std::string label_for(const std::string& timestamp, const std::string& game_name,
                      const InjectionConfig& config) {
    const std::string dll = config.use_loader ? "(mod loader)"
                            : config.dll_paths.empty()
                                ? std::string{}
                                : fs::path(config.dll_paths.front()).filename().string();
    return timestamp + " | " + game_name + " | " + dll;
}

nlohmann::json read_json_array(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        return nlohmann::json::array();
    }
    nlohmann::json doc;
    try {
        input >> doc;
    } catch (const nlohmann::json::exception&) {
        return nlohmann::json::array();
    }
    return doc.is_array() ? doc : nlohmann::json::array();
}

nlohmann::json read_legacy_history() {
    const auto path = legacy_history_path();
    if (path.empty()) {
        return nlohmann::json::array();
    }
    std::ifstream input(path);
    if (!input) {
        return nlohmann::json::array();
    }

    // timestamp|mode|appid|game|proton|exe|dlls|method|sleep|prefix|use_loader|console
    enum Field {
        Timestamp,
        Mode,
        AppId,
        Game,
        Proton,
        Exe,
        Dlls,
        Method,
        Sleep,
        Prefix,
        Loader,
        Console
    };

    nlohmann::json entries = nlohmann::json::array();
    for (std::string line; std::getline(input, line);) {
        // positional, so empty fields have to survive the split.
        const auto parts = proton_inject::split_fields(proton_inject::trim(line), '|');
        if (parts.size() <= Sleep) {
            continue;
        }
        const auto at = [&parts](Field field) {
            const auto index = static_cast<std::size_t>(field);
            return index < parts.size() ? parts[index] : std::string{};
        };

        const bool use_loader = at(Loader) == "true";
        proton_inject::AppConfig config;
        config.target_exe = at(Exe);
        config.use_loader = use_loader;
        if (!at(AppId).empty()) {
            config.app_id = at(AppId);
        }
        if (at(Mode).starts_with("Non-steam")) {
            config.non_steam = true;
        }
        if (!at(Proton).empty()) {
            config.proton_path = at(Proton);
        }
        if (!at(Prefix).empty()) {
            config.wine_prefix = at(Prefix);
        }
        if (!at(Method).empty()) {
            config.method = at(Method);
        }
        if (!at(Console).empty()) {
            config.loader_console = at(Console);
        }
        if (!use_loader) {
            if (const auto dlls = proton_inject::split(at(Dlls), ','); !dlls.empty()) {
                config.dll_path = dlls.front();
            }
        }
        if (const auto ms = std::atoi(at(Sleep).c_str()); ms > 0) {
            config.sleep_ms = static_cast<std::uint32_t>(ms);
        }

        nlohmann::json entry = nlohmann::json::object();
        entry["timestamp"] = at(Timestamp);
        entry["game_name"] = at(Game);
        entry["config"] = proton_inject::config_to_json(config);
        entries.push_back(std::move(entry));
    }
    return entries;
}

nlohmann::json load_all() {
    const auto path = history_path();
    if (path.empty()) {
        return nlohmann::json::array();
    }
    return fs::exists(path) ? read_json_array(path) : read_legacy_history();
}

}  // namespace

std::vector<HistoryEntry> HistoryManager::entries() const {
    std::vector<HistoryEntry> result;
    for (const auto& raw : load_all()) {
        if (!raw.is_object() || !raw.contains("config") || !raw["config"].is_object()) {
            continue;
        }
        HistoryEntry entry;
        entry.config = from_app_config(proton_inject::config_from_json(raw["config"]));
        entry.config.game_name = raw.value("game_name", "");
        entry.label = label_for(raw.value("timestamp", ""), entry.config.game_name, entry.config);
        result.push_back(std::move(entry));
    }
    return result;
}

std::optional<InjectionConfig> HistoryManager::entry_at(std::size_t index) const {
    const auto list = entries();
    if (index >= list.size()) {
        return std::nullopt;
    }
    return list[index].config;
}

void HistoryManager::save(const InjectionConfig& config) {
    const auto path = history_path();
    if (path.empty()) {
        return;
    }

    nlohmann::json entry = nlohmann::json::object();
    entry["timestamp"] = current_timestamp();
    entry["game_name"] = config.game_name;
    entry["config"] = proton_inject::config_to_json(to_app_config(config));

    nlohmann::json all = load_all();
    all.insert(all.begin(), std::move(entry));
    if (all.size() > kMaxHistory) {
        all.erase(all.begin() + kMaxHistory, all.end());
    }

    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        return;
    }
    output << all.dump(2) << '\n';
}
