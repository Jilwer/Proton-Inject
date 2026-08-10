#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace injector {

[[nodiscard]] bool enable_se_debug_privilege();
[[nodiscard]] std::filesystem::path resolve_module_path(const std::string& module);
[[nodiscard]] HANDLE open_process_for_inject(DWORD pid);
[[nodiscard]] DWORD get_process_id_by_name(const std::string& name);
[[nodiscard]] DWORD get_process_id_for_inject(const std::string& name);
[[nodiscard]] std::optional<DWORD> find_main_thread(DWORD pid);
[[nodiscard]] HANDLE open_thread_for_apc(DWORD tid);
[[nodiscard]] bool suspend_thread(HANDLE thread);
[[nodiscard]] bool resume_thread(HANDLE thread);
[[nodiscard]] bool process_still_running(HANDLE process);
[[nodiscard]] DWORD process_exit_code(HANDLE process);
[[nodiscard]] std::optional<std::pair<FILETIME, FILETIME>> process_times(HANDLE process);
[[nodiscard]] std::filesystem::path process_image_path(DWORD pid);
[[nodiscard]] void* get_module_base_address(HANDLE process, const std::filesystem::path& dll_path);
[[nodiscard]] bool wait_for_kernel32(HANDLE process, DWORD timeout_ms);
[[nodiscard]] void* local_proc(const char* module, const char* function_name);

struct ChildCandidate {
    DWORD pid = 0;
    FILETIME start_time{};
    std::filesystem::path exe_path;
    std::string exe_name;
};

[[nodiscard]] std::vector<ChildCandidate> list_children(DWORD parent_pid);
[[nodiscard]] std::optional<DWORD> select_follow_candidate(DWORD parent_pid,
                                                           const std::filesystem::path& parent_exe,
                                                           const FILETIME* ref_time,
                                                           const char* follow_name);
[[nodiscard]] std::optional<DWORD> find_descendant_by_name(DWORD ancestor_pid,
                                                           const std::string& name);

}  // namespace injector
