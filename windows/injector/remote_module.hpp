#pragma once

#include <windows.h>

namespace injector {

[[nodiscard]] void* remote_proc(HANDLE process, const wchar_t* module_name,
                                const char* function_name);
[[nodiscard]] HMODULE find_remote_module(HANDLE process, const wchar_t* module_name);

}  // namespace injector
