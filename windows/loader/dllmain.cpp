#include "mod_loader.hpp"

#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        loader::attach(instance);
    } else if (reason == DLL_PROCESS_DETACH) {
        loader::detach();
    }
    return TRUE;
}
