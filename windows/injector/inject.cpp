#include "inject.hpp"

#include "procutil.hpp"
#include "remote_module.hpp"

#include <cstdio>
#include <string>
#include <vector>

#ifndef NTSTATUS
using NTSTATUS = LONG;
#endif

namespace fs = std::filesystem;

namespace injector {

namespace {

using NtCreateThreadExFn = NTSTATUS(WINAPI*)(PHANDLE, ACCESS_MASK, PVOID, HANDLE, PVOID, PVOID,
                                             ULONG, SIZE_T, SIZE_T, SIZE_T, PVOID);

std::string dll_path_ansi(const fs::path& dll_path) {
    const std::wstring wide = fs::absolute(dll_path).wstring();
    if (wide.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::vector<char> buffer(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, buffer.data(), size, nullptr, nullptr);
    return std::string(buffer.data(), buffer.size() - 1);
}

bool prepare_loadlibrary_a(const HANDLE process, const fs::path& dll_path, void** load_library,
                           void** remote_path) {
    *load_library = remote_proc(process, L"kernel32.dll", "LoadLibraryA");
    if (*load_library == nullptr) {
        return false;
    }

    const std::string ansi_path = dll_path_ansi(dll_path);
    if (ansi_path.empty()) {
        return false;
    }

    const SIZE_T size = ansi_path.size() + 1;
    *remote_path = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (*remote_path == nullptr) {
        return false;
    }
    if (!WriteProcessMemory(process, *remote_path, ansi_path.c_str(), size, nullptr)) {
        VirtualFreeEx(process, *remote_path, 0, MEM_RELEASE);
        *remote_path = nullptr;
        return false;
    }
    return true;
}

bool inject_crt(const HANDLE process, const fs::path& dll_path) {
    void* load_library = nullptr;
    void* remote_path = nullptr;
    if (!prepare_loadlibrary_a(process, dll_path, &load_library, &remote_path)) {
        return false;
    }

    const HANDLE thread = CreateRemoteThread(process, nullptr, 0,
                                             reinterpret_cast<LPTHREAD_START_ROUTINE>(load_library),
                                             remote_path, 0, nullptr);
    if (thread == nullptr) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(thread, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeThread(thread, &exit_code);
    CloseHandle(thread);
    VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    return exit_code != 0;
}

bool inject_apc(const HANDLE process, const HANDLE thread, const fs::path& dll_path) {
    void* load_library = nullptr;
    void* remote_path = nullptr;
    if (!prepare_loadlibrary_a(process, dll_path, &load_library, &remote_path)) {
        return false;
    }

    const auto apc = reinterpret_cast<PAPCFUNC>(load_library);
    if (QueueUserAPC(apc, thread, reinterpret_cast<ULONG_PTR>(remote_path)) == 0) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }
    return true;
}

bool inject_nt(const HANDLE process, const fs::path& dll_path) {
    void* load_library = nullptr;
    void* remote_path = nullptr;
    if (!prepare_loadlibrary_a(process, dll_path, &load_library, &remote_path)) {
        return false;
    }

    const auto nt_create = reinterpret_cast<NtCreateThreadExFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateThreadEx"));
    if (nt_create == nullptr) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    HANDLE remote_thread = nullptr;
    const LONG status = nt_create(&remote_thread, 0x001F03FF, nullptr, process, load_library,
                                  remote_path, 0, 0, 0, 0, nullptr);
    if (status != 0 || remote_thread == nullptr) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(remote_thread, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeThread(remote_thread, &exit_code);
    CloseHandle(remote_thread);
    VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    return exit_code != 0;
}

}  // namespace

bool inject_dll(const HANDLE process, HANDLE thread, DWORD /*pid*/, const fs::path& dll_path,
                const Method method) {
    switch (method) {
        case Method::Apc:
            if (thread == nullptr) {
                return false;
            }
            return inject_apc(process, thread, dll_path);
        case Method::Nt:
            return inject_nt(process, dll_path);
        case Method::Crt:
        default:
            return inject_crt(process, dll_path);
    }
}

bool prepare_apc_thread(const DWORD pid, const HANDLE existing_thread, HANDLE* thread, bool* owned,
                        bool* suspended) {
    if (existing_thread != nullptr) {
        if (!suspend_thread(existing_thread)) {
            return false;
        }
        *thread = existing_thread;
        *owned = false;
        *suspended = true;
        return true;
    }

    const auto tid = find_main_thread(pid);
    if (!tid.has_value()) {
        return false;
    }
    *thread = open_thread_for_apc(*tid);
    if (*thread == nullptr) {
        return false;
    }
    if (!suspend_thread(*thread)) {
        CloseHandle(*thread);
        *thread = nullptr;
        return false;
    }
    *owned = true;
    *suspended = true;
    return true;
}

void finish_apc_thread(const HANDLE thread, const bool owned, const bool suspended) {
    if (suspended && !resume_thread(thread)) {
        std::fprintf(stderr, "Warning: failed to resume APC thread\n");
    }
    if (owned) {
        CloseHandle(thread);
    }
}

}  // namespace injector
