#include "steam_service.hpp"

#include "gui_util.hpp"

#include "utils/utils.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace {

// appmanifest_<id>.acf
std::string app_id_from_manifest(const std::string& filename) {
    static constexpr std::string_view kPrefix = "appmanifest_";
    static constexpr std::string_view kSuffix = ".acf";
    if (!filename.starts_with(kPrefix) || !filename.ends_with(kSuffix)) {
        return {};
    }
    return filename.substr(kPrefix.size(), filename.size() - kPrefix.size() - kSuffix.size());
}

std::string read_manifest_value(const fs::path& manifest, const std::regex& pattern) {
    std::ifstream input(manifest);
    if (!input) {
        return {};
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string content = buffer.str();

    std::smatch match;
    return std::regex_search(content, match, pattern) ? match[1].str() : std::string{};
}

}  // namespace

bool SteamService::detect_steam() {
    m_libraries = proton_inject::steam_library_roots();
    if (m_libraries.empty()) {
        m_error = "Steam installation not found.";
        return false;
    }
    return true;
}

std::vector<SteamGame> SteamService::installed_games() const {
    static const std::regex name_re(R"rx("name"\s+"([^"]+)")rx");

    std::vector<SteamGame> games;
    std::set<std::string> seen;

    for (const std::string& library : m_libraries) {
        const fs::path steamapps = fs::path(library) / "steamapps";
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(steamapps, ec)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const std::string app_id = app_id_from_manifest(entry.path().filename().string());
            if (app_id.empty() || !seen.insert(app_id).second) {
                continue;
            }

            const std::string name = read_manifest_value(entry.path(), name_re);
            games.push_back({app_id, name.empty() ? "Unknown" : name});
        }
    }

    std::ranges::sort(games, {}, &SteamGame::name);
    return games;
}

std::string SteamService::install_dir_for_app(const std::string& app_id) const {
    static const std::regex install_dir_re(R"rx("installdir"\s+"([^"]+)")rx", std::regex::icase);

    const std::string trimmed = proton_inject::trim(app_id);
    if (trimmed.empty()) {
        return {};
    }

    for (const std::string& library : m_libraries) {
        const fs::path steamapps = fs::path(library) / "steamapps";
        const std::string install_dir =
            read_manifest_value(steamapps / ("appmanifest_" + trimmed + ".acf"), install_dir_re);
        if (install_dir.empty()) {
            continue;
        }

        const fs::path game_dir = steamapps / "common" / install_dir;
        if (fs::exists(game_dir)) {
            return game_dir.string();
        }
    }

    return {};
}
