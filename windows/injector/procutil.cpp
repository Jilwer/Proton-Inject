#include "procutil.hpp"

#include "remote_module.hpp"

#include <algorithm>
#include <cctype>
#include <tlhelp32.h>
#include <vector>

namespace fs = std::filesystem;

namespace injector {

namespace {

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string utf16_to_string(const wchar_t* buffer, std::size_t max_len) {
    std::size_t len = 0;
    while (len < max_len && buffer[len] != L'\0') {
        ++len;
    }
    std::wstring wide(buffer, len);
    return std::string(wide.begin(), wide.end());
}

int compare_filetime(const FILETIME* left, const FILETIME* right) {
    const ULARGE_INTEGER lv{.LowPart = left->dwLowDateTime, .HighPart = left->dwHighDateTime};
    const ULARGE_INTEGER rv{.LowPart = right->dwLowDateTime, .HighPart = right->dwHighDateTime};
    if (lv.QuadPart < rv.QuadPart) {
        return -1;
    }
    if (lv.QuadPart > rv.QuadPart) {
        return 1;
    }
    return 0;
}

fs::path host_path_from_wine_path(std::string path) {
    if (path.size() >= 2 && (path[0] == 'Z' || path[0] == 'z') && path[1] == ':') {
        path = path.substr(2);
        while (!path.empty() && (path[0] == '\\' || path[0] == '/')) {
            path.erase(path.begin());
        }
        std::replace(path.begin(), path.end(), '\\', '/');
        return fs::path("/") / path;
    }
    return fs::path(path);
}

}  // namespace

bool enable_se_debug_privilege() {
    HANDLE raw_token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &raw_token)) {
        return false;
    }
    const UniqueHandle token(raw_token);

    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid)) {
        return false;
    }

    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Luid = luid;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    return AdjustTokenPrivileges(token.get(), FALSE, &privileges, 0, nullptr, nullptr) != 0;
}

fs::path resolve_module_path(const std::string& module) {
    fs::path candidate = host_path_from_wine_path(module);
    std::error_code ec;
    if (fs::exists(candidate, ec)) {
        return fs::absolute(candidate, ec);
    }
    if (candidate.is_relative()) {
        char buffer[MAX_PATH]{};
        GetModuleFileNameA(nullptr, buffer, MAX_PATH);
        const fs::path beside_exe = fs::path(buffer).parent_path() / candidate;
        if (fs::exists(beside_exe, ec)) {
            return fs::absolute(beside_exe, ec);
        }
    }
    return fs::absolute(candidate, ec);
}

UniqueHandle open_process_for_inject(const DWORD pid) {
    return UniqueHandle(OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid));
}

DWORD get_process_id_for_inject(const std::string& name) {
    const auto wanted = to_lower(name);
    const UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) {
        return 0;
    }

    std::vector<DWORD> matches;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot.get(), &entry)) {
        do {
            if (to_lower(utf16_to_string(entry.szExeFile, MAX_PATH)) == wanted) {
                matches.push_back(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot.get(), &entry));
    }

    // newest first: a relaunched game leaves the earlier pid around unopenable.
    for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
        if (open_process_for_inject(*it)) {
            return *it;
        }
    }
    return 0;
}

std::optional<DWORD> find_main_thread(const DWORD pid) {
    const UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
    if (!snapshot) {
        return std::nullopt;
    }

    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    DWORD main_tid = 0;
    FILETIME earliest{UINT32_MAX, UINT32_MAX};

    if (Thread32First(snapshot.get(), &entry)) {
        do {
            if (entry.th32OwnerProcessID != pid) {
                continue;
            }
            const UniqueHandle thread(
                OpenThread(THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID));
            if (!thread) {
                continue;
            }
            FILETIME create{}, exit{}, kernel{}, user{};
            if (GetThreadTimes(thread.get(), &create, &exit, &kernel, &user) &&
                compare_filetime(&create, &earliest) < 0) {
                earliest = create;
                main_tid = entry.th32ThreadID;
            }
        } while (Thread32Next(snapshot.get(), &entry));
    }

    if (main_tid == 0) {
        return std::nullopt;
    }
    return main_tid;
}

bool wait_for_kernel32(const HANDLE process, const DWORD timeout_ms) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    while (GetTickCount64() < deadline) {
        if (find_remote_module(process, L"kernel32.dll") != nullptr) {
            return true;
        }
        Sleep(10);
    }
    return false;
}

}  // namespace injector
