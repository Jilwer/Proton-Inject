#include "mod_loader.hpp"

#include "console.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <shlobj.h>
#include <string>
#include <string_view>
#include <thread>
#include <windows.h>

namespace fs = std::filesystem;

namespace loader {

namespace {

std::mutex g_loaded_mutex;
std::map<std::string, HMODULE> g_loaded_mods;

// the game process does not inherit our environment, so a file staged beside loader.dll is
// the only channel proton-inject has for handing options to the injected DLL.
fs::path settings_path(HINSTANCE instance) {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(instance, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return fs::path(buffer).parent_path() / "loader.cfg";
}

std::string trimmed(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return std::string(value);
}

std::string read_setting(const fs::path& path, const std::string_view key) {
    if (path.empty()) {
        return {};
    }
    std::ifstream input(path);
    if (!input) {
        return {};
    }
    for (std::string line; std::getline(input, line);) {
        const auto separator = line.find('=');
        if (separator == std::string::npos || line.starts_with("#")) {
            continue;
        }
        if (trimmed(std::string_view(line).substr(0, separator)) != key) {
            continue;
        }
        return trimmed(std::string_view(line).substr(separator + 1));
    }
    return {};
}

fs::path mods_directory() {
    wchar_t documents[MAX_PATH]{};
    if (SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents) != S_OK) {
        return {};
    }
    return fs::path(documents) / "proton-inject-mods";
}

void load_mod(const fs::path& dll_path) {
    const std::string key = dll_path.string();
    {
        std::lock_guard lock(g_loaded_mutex);
        if (g_loaded_mods.contains(key)) {
            return;
        }
    }

    console::write_line("Loading mod: " + key);
    const HMODULE module = LoadLibraryA(key.c_str());
    if (module == nullptr) {
        console::write_line("Failed to load mod: " + key);
        return;
    }

    std::lock_guard lock(g_loaded_mutex);
    g_loaded_mods[key] = module;
    console::write_line("Successfully loaded mod: " + key);
}

void unload_mod(const fs::path& dll_path) {
    const std::string key = dll_path.string();
    std::lock_guard lock(g_loaded_mutex);
    const auto it = g_loaded_mods.find(key);
    if (it == g_loaded_mods.end()) {
        return;
    }
    if (FreeLibrary(it->second)) {
        g_loaded_mods.erase(it);
        console::write_line("Successfully unloaded mod: " + key);
    }
}

void load_existing_mods(const fs::path& mods_dir) {
    if (!fs::exists(mods_dir)) {
        return;
    }
    for (const auto& entry : fs::recursive_directory_iterator(mods_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".dll") {
            load_mod(entry.path());
        }
    }
}

void watch_mods_directory(fs::path mods_dir) {
    std::thread([mods_dir = std::move(mods_dir)]() {
        const HANDLE directory =
            CreateFileW(mods_dir.wstring().c_str(), FILE_LIST_DIRECTORY,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (directory == INVALID_HANDLE_VALUE) {
            return;
        }

        char buffer[64 * 1024]{};
        while (true) {
            DWORD returned = 0;
            if (!ReadDirectoryChangesW(directory, reinterpret_cast<LPBYTE>(buffer), sizeof(buffer),
                                       TRUE, FILE_NOTIFY_CHANGE_FILE_NAME, &returned, nullptr,
                                       nullptr)) {
                break;
            }

            const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer);
            while (info != nullptr) {
                const std::wstring wide_name(info->FileName,
                                             info->FileNameLength / sizeof(wchar_t));
                const fs::path path = mods_dir / wide_name;
                if (path.extension() == ".dll") {
                    if (info->Action == FILE_ACTION_ADDED ||
                        info->Action == FILE_ACTION_RENAMED_NEW_NAME) {
                        Sleep(100);
                        load_mod(path);
                    } else if (info->Action == FILE_ACTION_REMOVED ||
                               info->Action == FILE_ACTION_RENAMED_OLD_NAME) {
                        unload_mod(path);
                    }
                }
                if (info->NextEntryOffset == 0) {
                    break;
                }
                info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                    reinterpret_cast<const char*>(info) + info->NextEntryOffset);
            }
        }
        CloseHandle(directory);
    }).detach();
}

}  // namespace

void attach(HINSTANCE instance) {
    console::init(console::parse_mode(read_setting(settings_path(instance), "console"),
                                      console::Mode::Alloc));
    console::write_line("Initializing mod system...");

    const fs::path mods_dir = mods_directory();
    if (mods_dir.empty()) {
        console::write_line("Failed to resolve Documents directory");
        return;
    }

    std::error_code ec;
    fs::create_directories(mods_dir, ec);
    load_existing_mods(mods_dir);
    watch_mods_directory(mods_dir);
    console::write_line("Started watching mods directory for changes...");
}

void detach() {
    MessageBoxA(nullptr, "Proton-Inject Loader Detached", "Loader", MB_OK);
}

}  // namespace loader
