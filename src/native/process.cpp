#include "native/process.hpp"

#include "native/proc_mem.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace proton_inject {

namespace {

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::vector<pid_t> pids_matching_exe(std::string_view process_name) {
    const auto want = to_lower_copy(exe_basename(process_name));
    std::vector<pid_t> pids;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/proc", ec)) {
        if (ec || !entry.is_directory()) {
            continue;
        }
        const auto pid_name = entry.path().filename().string();
        if (pid_name.empty() || !std::isdigit(static_cast<unsigned char>(pid_name.front()))) {
            continue;
        }

        std::ifstream cmdline(entry.path() / "cmdline", std::ios::binary);
        if (!cmdline) {
            continue;
        }
        std::string raw((std::istreambuf_iterator<char>(cmdline)),
                        std::istreambuf_iterator<char>());
        for (char& ch : raw) {
            if (ch == '\0') {
                ch = ' ';
            }
        }
        std::istringstream fields(raw);
        std::string field;
        bool match = false;
        while (fields >> field) {
            if (to_lower_copy(exe_basename(field)) == want) {
                match = true;
                break;
            }
        }
        if (match) {
            pids.push_back(static_cast<pid_t>(std::stoi(pid_name)));
        }
    }
    return pids;
}

}  // namespace

std::string exe_basename(std::string_view target) {
    std::string value(target);
    while (!value.empty() && (value.back() == '/' || value.back() == '\\')) {
        value.pop_back();
    }
    const auto pos = value.find_last_of("/\\");
    if (pos != std::string::npos) {
        return value.substr(pos + 1);
    }
    return value;
}

std::expected<pid_t, std::string> find_wine_target_pid(std::string_view process_name) {
    const auto pids = pids_matching_exe(process_name);
    if (pids.empty()) {
        return std::unexpected("Game process " + exe_basename(process_name) + " not found");
    }

    const auto want_exe = exe_basename(process_name);
    pid_t kernel32_only = -1;
    for (const pid_t pid : pids) {
        const auto maps = parse_maps(pid);
        if (!maps || !maps_contain_module(*maps, "kernel32.dll")) {
            continue;
        }
        bool has_exe = false;
        for (const auto& mapping : *maps) {
            if (path_ends_with_ignore_case(mapping.path, want_exe)) {
                has_exe = true;
                break;
            }
        }
        if (has_exe) {
            return pid;
        }
        if (kernel32_only < 0) {
            kernel32_only = pid;
        }
    }
    if (kernel32_only >= 0) {
        return kernel32_only;
    }
    return std::unexpected("Found " + std::to_string(pids.size()) + " process(es) named " +
                           exe_basename(process_name) +
                           " but none have kernel32.dll mapped (not a Wine/Proton process yet?)");
}

}  // namespace proton_inject
