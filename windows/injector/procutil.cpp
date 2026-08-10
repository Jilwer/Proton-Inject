#include "procutil.hpp"

#include "remote_module.hpp"

#include <algorithm>
#include <cctype>
#include <psapi.h>
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

ULONGLONG filetime_delta(const FILETIME* left, const FILETIME* right) {
    const ULARGE_INTEGER lv{.LowPart = left->dwLowDateTime, .HighPart = left->dwHighDateTime};
    const ULARGE_INTEGER rv{.LowPart = right->dwLowDateTime, .HighPart = right->dwHighDateTime};
    return lv.QuadPart > rv.QuadPart ? lv.QuadPart - rv.QuadPart : rv.QuadPart - lv.QuadPart;
}

std::size_t common_path_components(const fs::path& left, const fs::path& right) {
    auto a = to_lower(fs::absolute(left).string());
    auto b = to_lower(fs::absolute(right).string());
    std::replace(a.begin(), a.end(), '/', '\\');
    std::replace(b.begin(), b.end(), '/', '\\');
    std::size_t count = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (a[i] != b[i]) {
            break;
        }
        if (a[i] == '\\') {
            ++count;
        }
    }
    return count;
}

bool is_descendant_of(HANDLE snapshot, DWORD pid, DWORD ancestor_pid) {
    DWORD current = pid;
    for (int depth = 0; depth < 16; ++depth) {
        if (current == ancestor_pid) {
            return true;
        }
        if (current == 0) {
            return false;
        }
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        bool found = false;
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (entry.th32ProcessID == current) {
                    current = entry.th32ParentProcessID;
                    found = true;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        if (!found) {
            return false;
        }
    }
    return false;
}

}  // namespace

bool enable_se_debug_privilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }
    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid)) {
        CloseHandle(token);
        return false;
    }
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Luid = luid;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    const bool ok = AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr) != 0;
    CloseHandle(token);
    return ok;
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

HANDLE open_process_for_inject(const DWORD pid) {
    return OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
}

DWORD get_process_id_by_name(const std::string& name) {
    const auto wanted = to_lower(name);
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (to_lower(utf16_to_string(entry.szExeFile, MAX_PATH)) == wanted) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pid;
}

DWORD get_process_id_for_inject(const std::string& name) {
    const auto wanted = to_lower(name);
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    std::vector<DWORD> matches;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (to_lower(utf16_to_string(entry.szExeFile, MAX_PATH)) == wanted) {
                matches.push_back(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
        const HANDLE process = open_process_for_inject(*it);
        if (process != nullptr) {
            CloseHandle(process);
            return *it;
        }
    }
    return 0;
}

std::optional<DWORD> find_main_thread(const DWORD pid) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    DWORD main_tid = 0;
    FILETIME earliest{UINT32_MAX, UINT32_MAX};

    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != pid) {
                continue;
            }
            const HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
            if (thread == nullptr) {
                continue;
            }
            FILETIME create{}, exit{}, kernel{}, user{};
            if (GetThreadTimes(thread, &create, &exit, &kernel, &user) &&
                compare_filetime(&create, &earliest) < 0) {
                earliest = create;
                main_tid = entry.th32ThreadID;
            }
            CloseHandle(thread);
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (main_tid == 0) {
        return std::nullopt;
    }
    return main_tid;
}

HANDLE open_thread_for_apc(const DWORD tid) {
    return OpenThread(
        THREAD_SUSPEND_RESUME | THREAD_SET_CONTEXT | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
        FALSE, tid);
}

bool suspend_thread(const HANDLE thread) {
    return SuspendThread(thread) != static_cast<DWORD>(-1);
}

bool resume_thread(const HANDLE thread) {
    return ResumeThread(thread) != static_cast<DWORD>(-1);
}

bool process_still_running(const HANDLE process) {
    return WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

DWORD process_exit_code(const HANDLE process) {
    DWORD code = 0;
    GetExitCodeProcess(process, &code);
    return code;
}

std::optional<std::pair<FILETIME, FILETIME>> process_times(const HANDLE process) {
    FILETIME create{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &create, &exit, &kernel, &user)) {
        return std::nullopt;
    }
    return std::make_pair(create, exit);
}

fs::path process_image_path(const DWORD pid) {
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return {};
    }
    wchar_t buffer[MAX_PATH]{};
    DWORD size = MAX_PATH;
    if (!QueryFullProcessImageNameW(process, 0, buffer, &size)) {
        CloseHandle(process);
        return {};
    }
    CloseHandle(process);
    return fs::path(std::wstring(buffer, size));
}

void* get_module_base_address(const HANDLE process, const fs::path& dll_path) {
    const auto wanted = to_lower(dll_path.filename().string());
    HMODULE modules[1024]{};
    DWORD needed = 0;
    if (!EnumProcessModules(process, modules, sizeof(modules), &needed)) {
        return nullptr;
    }
    const DWORD count = needed / sizeof(HMODULE);
    for (DWORD i = 0; i < count; ++i) {
        wchar_t name[MAX_PATH]{};
        const DWORD len = GetModuleFileNameExW(process, modules[i], name, MAX_PATH);
        if (len == 0) {
            continue;
        }
        if (to_lower(fs::path(name).filename().string()) == wanted) {
            return modules[i];
        }
    }
    return nullptr;
}

namespace {

HMODULE find_remote_module_toolhelp(const DWORD pid, const char* module_name) {
    const auto wanted = to_lower(module_name);
    for (int attempt = 0; attempt < 5; ++attempt) {
        const HANDLE snapshot =
            CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snapshot == INVALID_HANDLE_VALUE) {
            Sleep(50);
            continue;
        }
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        HMODULE result = nullptr;
        if (Module32FirstW(snapshot, &entry)) {
            do {
                if (to_lower(utf16_to_string(entry.szModule, MAX_MODULE_NAME32 + 1)) == wanted) {
                    result = reinterpret_cast<HMODULE>(entry.modBaseAddr);
                    break;
                }
            } while (Module32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return result;
    }
    return nullptr;
}

HMODULE find_module_via_enum(const HANDLE process, const char* module_name) {
    const auto wanted = to_lower(module_name);
    HMODULE modules[1024]{};
    DWORD needed = 0;
    if (!EnumProcessModules(process, modules, sizeof(modules), &needed)) {
        return nullptr;
    }
    const DWORD count = needed / sizeof(HMODULE);
    for (DWORD i = 0; i < count; ++i) {
        wchar_t name[MAX_PATH]{};
        const DWORD len = GetModuleFileNameExW(process, modules[i], name, MAX_PATH);
        if (len == 0) {
            continue;
        }
        if (to_lower(fs::path(name).filename().string()) == wanted) {
            return modules[i];
        }
    }
    return nullptr;
}

}  // namespace

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

void* local_proc(const char* module, const char* function_name) {
    const HMODULE handle = GetModuleHandleA(module);
    if (handle == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<void*>(GetProcAddress(handle, function_name));
}

std::vector<ChildCandidate> list_children(const DWORD parent_pid) {
    std::vector<ChildCandidate> children;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return children;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ParentProcessID != parent_pid) {
                continue;
            }
            const HANDLE process =
                OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (process == nullptr) {
                continue;
            }
            FILETIME create{}, exit{}, kernel{}, user{};
            if (GetProcessTimes(process, &create, &exit, &kernel, &user)) {
                children.push_back(ChildCandidate{entry.th32ProcessID, create,
                                                  process_image_path(entry.th32ProcessID),
                                                  utf16_to_string(entry.szExeFile, MAX_PATH)});
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return children;
}

std::optional<DWORD> select_follow_candidate(const DWORD parent_pid, const fs::path& parent_exe,
                                             const FILETIME* ref_time, const char* follow_name) {
    const auto candidates = list_children(parent_pid);
    if (candidates.empty()) {
        return std::nullopt;
    }

    std::size_t best = 0;
    for (std::size_t i = 1; i < candidates.size(); ++i) {
        const bool i_name =
            follow_name != nullptr && _stricmp(candidates[i].exe_name.c_str(), follow_name) == 0;
        const bool best_name =
            follow_name != nullptr && _stricmp(candidates[best].exe_name.c_str(), follow_name) == 0;
        if (i_name && !best_name) {
            best = i;
            continue;
        }
        if (!i_name && best_name) {
            continue;
        }
        const auto i_delta = filetime_delta(&candidates[i].start_time, ref_time);
        const auto best_delta = filetime_delta(&candidates[best].start_time, ref_time);
        if (i_delta < best_delta) {
            best = i;
            continue;
        }
        if (i_delta > best_delta) {
            continue;
        }
        if (common_path_components(candidates[i].exe_path, parent_exe) >
            common_path_components(candidates[best].exe_path, parent_exe)) {
            best = i;
        }
    }
    return candidates[best].pid;
}

std::optional<DWORD> find_descendant_by_name(const DWORD ancestor_pid, const std::string& name) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::optional<DWORD> result;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const auto exe = utf16_to_string(entry.szExeFile, MAX_PATH);
            if (_stricmp(exe.c_str(), name.c_str()) == 0 && entry.th32ProcessID != ancestor_pid &&
                is_descendant_of(snapshot, entry.th32ProcessID, ancestor_pid)) {
                result = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

}  // namespace injector
