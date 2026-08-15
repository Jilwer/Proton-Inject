#include "native/process.hpp"

#include "native/proc_mem.hpp"
#include "utils/utils.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace proton_inject {

namespace {

// the game's own name can appear anywhere in the command line: Proton launches it through a
// chain of wrappers, so a match on any argument's basename is what identifies the process.
bool cmdline_mentions_exe(const fs::path& proc_entry, std::string_view wanted_lower) {
    std::ifstream cmdline(proc_entry / "cmdline", std::ios::binary);
    if (!cmdline) {
        return false;
    }
    std::string raw((std::istreambuf_iterator<char>(cmdline)), std::istreambuf_iterator<char>());
    for (char& ch : raw) {
        if (ch == '\0') {
            ch = ' ';
        }
    }

    std::istringstream fields(raw);
    std::string field;
    while (fields >> field) {
        if (to_lower(exe_basename(field)) == wanted_lower) {
            return true;
        }
    }
    return false;
}

std::vector<pid_t> pids_matching_exe(std::string_view process_name) {
    const auto wanted = to_lower(exe_basename(process_name));
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
        if (cmdline_mentions_exe(entry.path(), wanted)) {
            pids.push_back(static_cast<pid_t>(std::stoi(pid_name)));
        }
    }
    return pids;
}

}  // namespace

std::string exe_basename(std::string_view target) {
    while (!target.empty() && (target.back() == '/' || target.back() == '\\')) {
        target.remove_suffix(1);
    }
    const auto pos = target.find_last_of("/\\");
    if (pos != std::string_view::npos) {
        target.remove_prefix(pos + 1);
    }
    return std::string(target);
}

bool is_process_running(std::string_view process_name) {
    return !pids_matching_exe(process_name).empty();
}

std::expected<pid_t, std::string> find_wine_target_pid(std::string_view process_name) {
    const auto pids = pids_matching_exe(process_name);
    const auto want_exe = exe_basename(process_name);
    if (pids.empty()) {
        return std::unexpected("Game process " + want_exe + " not found");
    }

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
                           want_exe +
                           " but none have kernel32.dll mapped (not a Wine/Proton process yet?)");
}

}  // namespace proton_inject
