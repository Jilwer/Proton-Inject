#pragma once

#include <string>
#include <vector>

struct SteamGame {
    std::string app_id;
    std::string name;
};

// the installed-games view of the local Steam libraries.
class SteamService {
public:
    bool detect_steam();

    [[nodiscard]] std::vector<SteamGame> installed_games() const;
    [[nodiscard]] std::string install_dir_for_app(const std::string& app_id) const;

    [[nodiscard]] const std::string& error_message() const { return m_error; }

private:
    std::vector<std::string> m_libraries;
    std::string m_error;
};
