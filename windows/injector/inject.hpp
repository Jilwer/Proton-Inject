#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>

#include "method.hpp"
#include "procutil.hpp"

namespace injector {

// an APC only runs when its thread next enters an alertable wait, so the thread is suspended
// while the APC is queued and resumed afterwards. Tying that pair to scope keeps a failed
// injection from leaving the game frozen.
class ApcThread {
public:
    [[nodiscard]] static std::optional<ApcThread> prepare(DWORD pid);

    ApcThread(const ApcThread&) = delete;
    ApcThread& operator=(const ApcThread&) = delete;
    ApcThread(ApcThread&&) noexcept = default;
    ApcThread& operator=(ApcThread&& other) noexcept;
    ~ApcThread();

    [[nodiscard]] HANDLE get() const { return thread_.get(); }

private:
    explicit ApcThread(UniqueHandle thread) : thread_(std::move(thread)) {}

    void resume();

    UniqueHandle thread_;
};

[[nodiscard]] bool inject_dll(HANDLE process, HANDLE apc_thread,
                              const std::filesystem::path& dll_path, Method method);

}  // namespace injector
