#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

namespace injector {

// every injection path has several early returns; without this each one has to remember its
// own CloseHandle.
class UniqueHandle {
public:
    UniqueHandle() = default;

    explicit UniqueHandle(HANDLE handle)
        : handle_(handle == INVALID_HANDLE_VALUE ? nullptr : handle) {}

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~UniqueHandle() { reset(); }

    [[nodiscard]] HANDLE get() const { return handle_; }

    explicit operator bool() const { return handle_ != nullptr; }

private:
    void reset() {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
    }

    HANDLE handle_ = nullptr;
};

[[nodiscard]] bool enable_se_debug_privilege();
[[nodiscard]] std::filesystem::path resolve_module_path(const std::string& module);
[[nodiscard]] UniqueHandle open_process_for_inject(DWORD pid);
[[nodiscard]] DWORD get_process_id_for_inject(const std::string& name);
[[nodiscard]] std::optional<DWORD> find_main_thread(DWORD pid);
[[nodiscard]] bool wait_for_kernel32(HANDLE process, DWORD timeout_ms);

}  // namespace injector
