#include "core/embedded_assets.hpp"

extern "C" {
extern const unsigned char _binary_loader_dll_start[];
extern const unsigned char _binary_loader_dll_end[];
extern const unsigned char _binary_injector_exe_start[];
extern const unsigned char _binary_injector_exe_end[];
}

namespace embedded {

const unsigned char* loader_dll = _binary_loader_dll_start;
const std::size_t loader_dll_size =
    static_cast<std::size_t>(_binary_loader_dll_end - _binary_loader_dll_start);

const unsigned char* injector_exe = _binary_injector_exe_start;
const std::size_t injector_exe_size =
    static_cast<std::size_t>(_binary_injector_exe_end - _binary_injector_exe_start);

}  // namespace embedded
