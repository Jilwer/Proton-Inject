#include "remote_module.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <tlhelp32.h>
#include <vector>

namespace injector {

namespace {

using NtQueryInformationProcessFn = LONG(WINAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

struct RemoteUnicodeString {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
};

struct RemoteLdrEntry {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    RemoteUnicodeString FullDllName;
    RemoteUnicodeString BaseDllName;
};

struct RemotePebLdrData {
    ULONG Length;
    BOOLEAN Initialized;
    PVOID SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
};

struct RemotePeb {
    BYTE Reserved1[2];
    BYTE BeingDebugged;
    BYTE Reserved2[1];
    PVOID Reserved3[2];
    PVOID Ldr;
};

struct ProcessBasicInformation {
    PVOID Reserved1;
    PVOID PebBaseAddress;
    PVOID Reserved2[2];
    ULONG_PTR UniqueProcessId;
    PVOID Reserved3;
};

HMODULE find_remote_module_peb(const HANDLE process, const wchar_t* module_name) {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return nullptr;
    }

    const auto nt_query = reinterpret_cast<NtQueryInformationProcessFn>(
        GetProcAddress(ntdll, "NtQueryInformationProcess"));
    if (nt_query == nullptr) {
        return nullptr;
    }

    ProcessBasicInformation info{};
    if (nt_query(process, 0, &info, sizeof(info), nullptr) != 0 || info.PebBaseAddress == nullptr) {
        return nullptr;
    }

    PVOID ldr = nullptr;
    if (!ReadProcessMemory(process,
                           static_cast<BYTE*>(info.PebBaseAddress) + offsetof(RemotePeb, Ldr), &ldr,
                           sizeof(ldr), nullptr) ||
        ldr == nullptr) {
        return nullptr;
    }

    LIST_ENTRY list_head{};
    auto* list_head_addr =
        static_cast<BYTE*>(ldr) + offsetof(RemotePebLdrData, InMemoryOrderModuleList);
    if (!ReadProcessMemory(process, list_head_addr, &list_head, sizeof(list_head), nullptr)) {
        return nullptr;
    }

    LIST_ENTRY* current = list_head.Flink;
    const auto* list_end = reinterpret_cast<LIST_ENTRY*>(list_head_addr);
    for (int i = 0; current != list_end && i < 512; ++i) {
        auto* entry_addr =
            reinterpret_cast<BYTE*>(current) - offsetof(RemoteLdrEntry, InMemoryOrderLinks);
        RemoteLdrEntry entry{};
        if (!ReadProcessMemory(process, entry_addr, &entry, sizeof(entry), nullptr)) {
            break;
        }

        if (entry.BaseDllName.Length > 0 && entry.BaseDllName.Length < 520 &&
            entry.BaseDllName.Buffer != nullptr) {
            std::vector<wchar_t> name(entry.BaseDllName.Length / sizeof(wchar_t) + 1, L'\0');
            const USHORT name_len = static_cast<USHORT>(
                std::min<USHORT>(entry.BaseDllName.Length,
                                 static_cast<USHORT>((name.size() - 1) * sizeof(wchar_t))));
            if (ReadProcessMemory(process, entry.BaseDllName.Buffer, name.data(), name_len,
                                  nullptr) &&
                _wcsicmp(name.data(), module_name) == 0) {
                return static_cast<HMODULE>(entry.DllBase);
            }
        }

        current = entry.InMemoryOrderLinks.Flink;
    }

    return nullptr;
}

HMODULE find_remote_module_toolhelp(const DWORD pid, const wchar_t* module_name) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        const HANDLE snapshot =
            CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snapshot == INVALID_HANDLE_VALUE) {
            Sleep(50);
            continue;
        }

        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        HMODULE result = nullptr;
        if (Module32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szModule, module_name) == 0) {
                    result = reinterpret_cast<HMODULE>(entry.modBaseAddr);
                    break;
                }
            } while (Module32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return result;
    }
    return nullptr;
}

void* find_export_in_remote_pe(const HANDLE process, HMODULE remote_base,
                               const char* function_name) {
    IMAGE_DOS_HEADER dos_header{};
    auto* base = reinterpret_cast<BYTE*>(remote_base);
    if (!ReadProcessMemory(process, base, &dos_header, sizeof(dos_header), nullptr) ||
        dos_header.e_magic != IMAGE_DOS_SIGNATURE) {
        return nullptr;
    }

    IMAGE_NT_HEADERS nt_headers{};
    if (!ReadProcessMemory(process, base + dos_header.e_lfanew, &nt_headers, sizeof(nt_headers),
                           nullptr) ||
        nt_headers.Signature != IMAGE_NT_SIGNATURE) {
        return nullptr;
    }

    const IMAGE_DATA_DIRECTORY export_dir_entry =
        nt_headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (export_dir_entry.VirtualAddress == 0 || export_dir_entry.Size == 0) {
        return nullptr;
    }

    IMAGE_EXPORT_DIRECTORY export_dir{};
    if (!ReadProcessMemory(process, base + export_dir_entry.VirtualAddress, &export_dir,
                           sizeof(export_dir), nullptr)) {
        return nullptr;
    }

    if (export_dir.NumberOfNames == 0 || export_dir.NumberOfNames > 0x10000) {
        return nullptr;
    }

    std::vector<DWORD> name_rvas(export_dir.NumberOfNames);
    std::vector<WORD> ordinals(export_dir.NumberOfNames);
    std::vector<DWORD> function_rvas(export_dir.NumberOfFunctions);
    if (!ReadProcessMemory(process, base + export_dir.AddressOfNames, name_rvas.data(),
                           name_rvas.size() * sizeof(DWORD), nullptr) ||
        !ReadProcessMemory(process, base + export_dir.AddressOfNameOrdinals, ordinals.data(),
                           ordinals.size() * sizeof(WORD), nullptr) ||
        !ReadProcessMemory(process, base + export_dir.AddressOfFunctions, function_rvas.data(),
                           function_rvas.size() * sizeof(DWORD), nullptr)) {
        return nullptr;
    }

    const std::size_t name_len = std::strlen(function_name) + 1;
    for (DWORD i = 0; i < export_dir.NumberOfNames; ++i) {
        std::vector<char> export_name(name_len, '\0');
        if (!ReadProcessMemory(process, base + name_rvas[i], export_name.data(), name_len,
                               nullptr)) {
            continue;
        }
        if (std::strcmp(export_name.data(), function_name) != 0 ||
            ordinals[i] >= export_dir.NumberOfFunctions) {
            continue;
        }

        const DWORD function_rva = function_rvas[ordinals[i]];
        if (function_rva >= export_dir_entry.VirtualAddress &&
            function_rva < export_dir_entry.VirtualAddress + export_dir_entry.Size) {
            return nullptr;
        }
        return base + function_rva;
    }

    return nullptr;
}

}  // namespace

HMODULE find_remote_module(const HANDLE process, const wchar_t* module_name) {
    if (HMODULE module = find_remote_module_peb(process, module_name); module != nullptr) {
        return module;
    }
    return find_remote_module_toolhelp(GetProcessId(process), module_name);
}

void* remote_proc(const HANDLE process, const wchar_t* module_name, const char* function_name) {
    const HMODULE remote_module = find_remote_module(process, module_name);
    if (remote_module == nullptr) {
        std::fprintf(stderr, "Failed to find %ls in target process\n", module_name);
        return nullptr;
    }

    if (void* address = find_export_in_remote_pe(process, remote_module, function_name);
        address != nullptr) {
        return address;
    }

    const HMODULE local_module = GetModuleHandleW(module_name);
    if (local_module == nullptr) {
        return nullptr;
    }
    const auto local_function = GetProcAddress(local_module, function_name);
    if (local_function == nullptr) {
        return nullptr;
    }

    const auto offset = reinterpret_cast<std::uintptr_t>(local_function) -
                        reinterpret_cast<std::uintptr_t>(local_module);
    return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(remote_module) + offset);
}

}  // namespace injector
