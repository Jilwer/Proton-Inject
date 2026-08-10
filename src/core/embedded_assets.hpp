#pragma once

#include <cstddef>

namespace embedded {

extern const unsigned char* loader_dll;
extern const std::size_t loader_dll_size;

extern const unsigned char* injector_exe;
extern const std::size_t injector_exe_size;

}  // namespace embedded
