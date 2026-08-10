#pragma once

#include <windows.h>

#include <filesystem>

#include "method.hpp"

namespace injector {

[[nodiscard]] bool inject_dll(HANDLE process, HANDLE thread, DWORD pid,
                              const std::filesystem::path& dll_path, Method method);
[[nodiscard]] bool prepare_apc_thread(DWORD pid, HANDLE existing_thread, HANDLE* thread,
                                      bool* owned, bool* suspended);
void finish_apc_thread(HANDLE thread, bool owned, bool suspended);

}  // namespace injector
