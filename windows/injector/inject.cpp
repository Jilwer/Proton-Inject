#include "inject.hpp"

#include "remote_module.hpp"

#include <cstdio>
#include <string>
#include <utility>
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

// the page holding the DLL path stays committed for the lifetime of the injection: an APC
// runs long after this returns, so the caller cannot free it on the way out.
class RemoteString {
public:
    RemoteString(HANDLE process, const std::string& text) : process_(process) {
        const SIZE_T size = text.size() + 1;
        address_ = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (address_ == nullptr) {
            return;
        }
        if (!WriteProcessMemory(process, address_, text.c_str(), size, nullptr)) {
            release();
        }
    }

    RemoteString(const RemoteString&) = delete;
    RemoteString& operator=(const RemoteString&) = delete;

    ~RemoteString() { release(); }

    [[nodiscard]] void* address() const { return address_; }

    explicit operator bool() const { return address_ != nullptr; }

    // hands ownership to the target process, for paths that outlive this scope.
    void* leak() { return std::exchange(address_, nullptr); }

private:
    void release() {
        if (address_ != nullptr) {
            VirtualFreeEx(process_, address_, 0, MEM_RELEASE);
            address_ = nullptr;
        }
    }

    HANDLE process_ = nullptr;
    void* address_ = nullptr;
};

bool inject_crt(const HANDLE process, void* load_library, const RemoteString& path) {
    const UniqueHandle thread(CreateRemoteThread(
        process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(load_library), path.address(),
        0, nullptr));
    if (!thread) {
        return false;
    }

    WaitForSingleObject(thread.get(), INFINITE);
    DWORD exit_code = 0;
    GetExitCodeThread(thread.get(), &exit_code);
    return exit_code != 0;
}

bool inject_apc(const HANDLE thread, void* load_library, RemoteString& path) {
    const auto apc = reinterpret_cast<PAPCFUNC>(load_library);
    if (QueueUserAPC(apc, thread, reinterpret_cast<ULONG_PTR>(path.address())) == 0) {
        return false;
    }
    // the APC has not run yet, so the path must survive this scope.
    path.leak();
    return true;
}

bool inject_nt(const HANDLE process, void* load_library, const RemoteString& path) {
    const auto nt_create = reinterpret_cast<NtCreateThreadExFn>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateThreadEx")));
    if (nt_create == nullptr) {
        return false;
    }

    HANDLE raw_thread = nullptr;
    const LONG status = nt_create(&raw_thread, 0x001F03FF, nullptr, process, load_library,
                                  path.address(), 0, 0, 0, 0, nullptr);
    const UniqueHandle thread(raw_thread);
    if (status != 0 || !thread) {
        return false;
    }

    WaitForSingleObject(thread.get(), INFINITE);
    DWORD exit_code = 0;
    GetExitCodeThread(thread.get(), &exit_code);
    return exit_code != 0;
}

}  // namespace

std::optional<ApcThread> ApcThread::prepare(const DWORD pid) {
    const auto tid = find_main_thread(pid);
    if (!tid.has_value()) {
        return std::nullopt;
    }

    UniqueHandle thread(OpenThread(
        THREAD_SUSPEND_RESUME | THREAD_SET_CONTEXT | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
        FALSE, *tid));
    if (!thread) {
        return std::nullopt;
    }
    if (SuspendThread(thread.get()) == static_cast<DWORD>(-1)) {
        return std::nullopt;
    }
    return ApcThread(std::move(thread));
}

ApcThread& ApcThread::operator=(ApcThread&& other) noexcept {
    if (this != &other) {
        resume();
        thread_ = std::move(other.thread_);
    }
    return *this;
}

ApcThread::~ApcThread() {
    resume();
}

void ApcThread::resume() {
    if (thread_ && ResumeThread(thread_.get()) == static_cast<DWORD>(-1)) {
        std::fprintf(stderr, "Warning: failed to resume APC thread\n");
    }
}

bool inject_dll(const HANDLE process, const HANDLE apc_thread, const fs::path& dll_path,
                const Method method) {
    void* load_library = remote_proc(process, L"kernel32.dll", "LoadLibraryA");
    if (load_library == nullptr) {
        return false;
    }

    const std::string ansi_path = dll_path_ansi(dll_path);
    if (ansi_path.empty()) {
        return false;
    }

    RemoteString path(process, ansi_path);
    if (!path) {
        return false;
    }

    switch (method) {
        case Method::Apc:
            return apc_thread != nullptr && inject_apc(apc_thread, load_library, path);
        case Method::Nt:
            return inject_nt(process, load_library, path);
        case Method::Crt:
            return inject_crt(process, load_library, path);
    }
    return false;
}

}  // namespace injector
