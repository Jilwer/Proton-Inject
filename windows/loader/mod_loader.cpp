#include "mod_loader.hpp"

#include <filesystem>
#include <map>
#include <mutex>
#include <shlobj.h>
#include <string>
#include <thread>
#include <windows.h>

namespace fs = std::filesystem;

namespace loader {

namespace {

std::mutex g_loaded_mutex;
std::map<std::string, HMODULE> g_loaded_mods;

void write_console_line(const std::string& line) {
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == nullptr || output == INVALID_HANDLE_VALUE) {
        return;
    }
    std::string message = line + "\r\n";
    DWORD written = 0;
    WriteConsoleA(output, message.c_str(), static_cast<DWORD>(message.size()), &written, nullptr);
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

    write_console_line("Loading mod: " + key);
    const HMODULE module = LoadLibraryA(key.c_str());
    if (module == nullptr) {
        write_console_line("Failed to load mod: " + key);
        return;
    }

    std::lock_guard lock(g_loaded_mutex);
    g_loaded_mods[key] = module;
    write_console_line("Successfully loaded mod: " + key);
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
        write_console_line("Successfully unloaded mod: " + key);
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

void attach() {
    AllocConsole();
    write_console_line("Initializing mod system...");

    const fs::path mods_dir = mods_directory();
    if (mods_dir.empty()) {
        write_console_line("Failed to resolve Documents directory");
        return;
    }

    std::error_code ec;
    fs::create_directories(mods_dir, ec);
    load_existing_mods(mods_dir);
    watch_mods_directory(mods_dir);
    write_console_line("Started watching mods directory for changes...");
}

void detach() {
    MessageBoxA(nullptr, "Proton-Inject Loader Detached", "Loader", MB_OK);
}

}  // namespace loader
