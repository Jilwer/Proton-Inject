#include "native/inject_mem.hpp"

#include "native/pe_export.hpp"
#include "native/proc_mem.hpp"
#include "native/process.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ios>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace proton_inject {

namespace {

constexpr auto kLoadWait = std::chrono::seconds(10);
constexpr auto kPoll = std::chrono::milliseconds(10);

struct Placement {
    std::uintptr_t stub = 0;
    std::uintptr_t data = 0;
};

struct IatHook {
    IatSlot iat;
    std::string owner;
    std::string function;
};

std::string hex_addr(std::uintptr_t addr) {
    std::ostringstream out;
    out << "0x" << std::hex << addr;
    return out.str();
}

void emit_u8(std::vector<std::uint8_t>& out, std::uint8_t value) {
    out.push_back(value);
}

void emit_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

void emit_saved_regs(std::vector<std::uint8_t>& stub) {
    emit_u8(stub, 0xf3);
    emit_u8(stub, 0x0f);
    emit_u8(stub, 0x1e);
    emit_u8(stub, 0xfa);  // endbr64
    emit_u8(stub, 0x9c);  // pushfq
    for (const std::uint8_t op : {0x50, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57}) {
        emit_u8(stub, op);
    }
    emit_u8(stub, 0x41);
    emit_u8(stub, 0x50);
    emit_u8(stub, 0x41);
    emit_u8(stub, 0x51);
    emit_u8(stub, 0x41);
    emit_u8(stub, 0x52);
    emit_u8(stub, 0x41);
    emit_u8(stub, 0x53);
    emit_u8(stub, 0x48);
    emit_u8(stub, 0x89);
    emit_u8(stub, 0xe5);  // mov rbp, rsp
    emit_u8(stub, 0x48);
    emit_u8(stub, 0x83);
    emit_u8(stub, 0xe4);
    emit_u8(stub, 0xf0);  // and rsp, -16
    emit_u8(stub, 0x48);
    emit_u8(stub, 0x83);
    emit_u8(stub, 0xec);
    emit_u8(stub, 0x20);  // sub rsp, 0x20
}

void emit_restore_and_jmp(std::vector<std::uint8_t>& stub, std::uintptr_t original) {
    emit_u8(stub, 0x48);
    emit_u8(stub, 0x89);
    emit_u8(stub, 0xec);  // mov rsp, rbp
    emit_u8(stub, 0x41);
    emit_u8(stub, 0x5b);
    emit_u8(stub, 0x41);
    emit_u8(stub, 0x5a);
    emit_u8(stub, 0x41);
    emit_u8(stub, 0x59);
    emit_u8(stub, 0x41);
    emit_u8(stub, 0x58);
    for (const std::uint8_t op : {0x5f, 0x5e, 0x5d, 0x5b, 0x5a, 0x59, 0x58}) {
        emit_u8(stub, op);
    }
    emit_u8(stub, 0x9d);
    emit_u8(stub, 0x48);
    emit_u8(stub, 0xb8);
    emit_u64(stub, original);
    emit_u8(stub, 0xff);
    emit_u8(stub, 0xe0);  // jmp original
}

std::vector<std::uint8_t> build_iat_stub(std::uintptr_t load_library, std::uintptr_t data,
                                         std::uintptr_t path, std::uintptr_t original) {
    std::vector<std::uint8_t> stub;
    stub.reserve(128);
    emit_saved_regs(stub);
    emit_u8(stub, 0x48);
    emit_u8(stub, 0xbb);
    emit_u64(stub, data);  // movabs rbx, data
    emit_u8(stub, 0x48);
    emit_u8(stub, 0x83);
    emit_u8(stub, 0x7b);
    emit_u8(stub, 0x08);
    emit_u8(stub, 0x00);  // cmp qword [rbx+8], 0
    emit_u8(stub, 0x75);
    emit_u8(stub, 33);  // jne skip LoadLibrary
    emit_u8(stub, 0x48);
    emit_u8(stub, 0xb9);
    emit_u64(stub, path);  // movabs rcx, path
    emit_u8(stub, 0x48);
    emit_u8(stub, 0xb8);
    emit_u64(stub, load_library);
    emit_u8(stub, 0xff);
    emit_u8(stub, 0xd0);  // call LoadLibraryA
    emit_u8(stub, 0x48);
    emit_u8(stub, 0x89);
    emit_u8(stub, 0x03);  // mov [rbx], rax
    emit_u8(stub, 0x48);
    emit_u8(stub, 0xc7);
    emit_u8(stub, 0x43);
    emit_u8(stub, 0x08);
    emit_u8(stub, 0x01);
    emit_u8(stub, 0x00);
    emit_u8(stub, 0x00);
    emit_u8(stub, 0x00);  // mov qword [rbx+8], 1
    emit_restore_and_jmp(stub, original);
    return stub;
}

bool is_special_mapping(std::string_view path) {
    return path == "[vdso]" || path == "[vvar]" || path == "[vsyscall]" ||
           path.starts_with("[stack");
}

std::optional<std::uintptr_t> find_zero_span(const ProcMem& mem, std::uintptr_t start,
                                             std::uintptr_t end, std::size_t needed) {
    if (end <= start || end - start < needed) {
        return std::nullopt;
    }
    const std::size_t window =
        static_cast<std::size_t>(std::min<std::uintptr_t>(end - start, 0x2000));
    const std::uintptr_t region_start = end - window;
    std::vector<std::byte> buf(window);
    if (!mem.read(region_start, std::span(buf))) {
        return std::nullopt;
    }
    std::size_t run = 0;
    std::size_t run_start = 0;
    std::optional<std::uintptr_t> found;
    for (std::size_t i = 0; i < buf.size(); ++i) {
        if (buf[i] == std::byte{0}) {
            if (run == 0) {
                run_start = i;
            }
            ++run;
            if (run >= needed) {
                found = region_start + run_start;
            }
        } else {
            run = 0;
        }
    }
    return found;
}

std::optional<Placement> place_in_caves(const std::vector<SectionCave>& caves,
                                        std::size_t stub_size, std::size_t data_size) {
    const SectionCave* exec = nullptr;
    const SectionCave* write = nullptr;
    for (const auto& cave : caves) {
        if (cave.executable && cave.size >= stub_size && exec == nullptr) {
            exec = &cave;
        }
        if (cave.writable && cave.size >= data_size && write == nullptr) {
            write = &cave;
        }
    }
    if (exec == nullptr || write == nullptr) {
        return std::nullopt;
    }
    if (exec == write && exec->size >= stub_size + data_size) {
        return Placement{exec->addr, exec->addr + stub_size};
    }
    if (exec->addr == write->addr) {
        return std::nullopt;
    }
    return Placement{exec->addr, write->addr};
}

std::optional<Placement> find_placement(const ProcMem& mem, const std::vector<Mapping>& maps,
                                        std::uintptr_t kernel32, std::uintptr_t ntdll,
                                        std::size_t stub_size, std::size_t data_size) {
    std::vector<SectionCave> caves;
    if (auto found = find_pe_caves(mem, kernel32); found) {
        caves.insert(caves.end(), found->begin(), found->end());
    }
    if (ntdll != 0) {
        if (auto found = find_pe_caves(mem, ntdll); found) {
            caves.insert(caves.end(), found->begin(), found->end());
        }
    }
    if (auto placed = place_in_caves(caves, stub_size, data_size); placed) {
        return placed;
    }

    for (const auto& mapping : maps) {
        if (!mapping.writable() || is_special_mapping(mapping.path)) {
            continue;
        }
        const auto data = find_zero_span(mem, mapping.start, mapping.end, data_size);
        if (!data) {
            continue;
        }
        for (const auto& cave : caves) {
            if (cave.executable && cave.size >= stub_size) {
                return Placement{cave.addr, *data};
            }
        }
        if (mapping.executable()) {
            const auto stub =
                find_zero_span(mem, mapping.start, mapping.end, stub_size + data_size);
            if (stub && *stub + stub_size + data_size <= mapping.end) {
                return Placement{*stub, *stub + stub_size};
            }
        }
    }
    return std::nullopt;
}

std::expected<void, std::string> write_bytes(const ProcMem& mem, std::uintptr_t addr,
                                             const void* data, std::size_t size) {
    return mem.write(addr, std::span(static_cast<const std::byte*>(data), size));
}

int module_preference(std::string_view path) {
    const auto base = path_ends_with_ignore_case(path, "unityplayer.dll")    ? 100
                      : path_ends_with_ignore_case(path, "gameassembly.dll") ? 90
                      : path_ends_with_ignore_case(path, ".exe")             ? 50
                      : path_ends_with_ignore_case(path, "kernel32.dll")     ? 0
                      : path_ends_with_ignore_case(path, "ntdll.dll")        ? 0
                      : path_ends_with_ignore_case(path, "user32.dll")       ? 0
                      : path_ends_with_ignore_case(path, ".dll")             ? 20
                                                                             : -1;
    return base;
}

std::optional<IatHook> find_process_iat(const ProcMem& mem, const std::vector<Mapping>& maps) {
    std::vector<const Mapping*> images;
    for (const auto& mapping : maps) {
        if (mapping.offset != 0 || module_preference(mapping.path) <= 0) {
            continue;
        }
        images.push_back(&mapping);
    }
    std::sort(images.begin(), images.end(), [](const Mapping* left, const Mapping* right) {
        return module_preference(left->path) > module_preference(right->path);
    });

    static constexpr std::array<std::string_view, 6> kFunctions{
        "QueryPerformanceCounter", "GetTickCount64", "PeekMessageW", "GetMessageW", "SleepEx",
        "WaitForSingleObjectEx",
    };
    for (const auto function : kFunctions) {
        for (const Mapping* image : images) {
            auto slot = find_iat_slot(mem, image->start, {}, function);
            if (!slot) {
                continue;
            }
            IatHook hook;
            hook.iat = *slot;
            hook.owner = image->path;
            hook.function = std::string(function);
            return hook;
        }
    }
    return std::nullopt;
}

}  // namespace

std::expected<void, std::string> inject_dll_via_iat(const pid_t pid,
                                                    const std::string& linux_dll_path) {
    auto mem = ProcMem::open(pid);
    if (!mem) {
        return std::unexpected(mem.error());
    }

    auto maps = parse_maps(pid);
    if (!maps) {
        return std::unexpected(maps.error());
    }
    const Mapping* kernel32_map = find_module_base(*maps, "kernel32.dll");
    if (kernel32_map == nullptr) {
        return std::unexpected("kernel32.dll is not mapped in pid " + std::to_string(pid));
    }
    const std::uintptr_t kernel32 = kernel32_map->start;
    const Mapping* ntdll_map = find_module_base(*maps, "ntdll.dll");
    const std::uintptr_t ntdll = ntdll_map != nullptr ? ntdll_map->start : 0;

    debug("kernel32.dll at pid " + std::to_string(pid) + " base " + hex_addr(kernel32));

    auto load_library = find_pe_export(*mem, kernel32, "LoadLibraryA");
    if (!load_library) {
        return std::unexpected(load_library.error());
    }
    debug("LoadLibraryA at " + hex_addr(*load_library));

    auto hook = find_process_iat(*mem, *maps);
    if (!hook) {
        return std::unexpected(
            "Could not find a kernel32/user32 import in the game to transfer control");
    }

    const std::string wine_path = to_windows_path(linux_dll_path);
    const std::size_t data_size = 16 + wine_path.size() + 1;
    constexpr std::size_t kStubBudget = 160;
    auto placement = find_placement(*mem, *maps, kernel32, ntdll, kStubBudget, data_size);
    if (!placement) {
        return std::unexpected(
            "No unused mapped memory large enough for the LoadLibrary stub and DLL path");
    }
    const Mapping* stub_map = find_mapping(*maps, placement->stub);
    if (stub_map == nullptr || !stub_map->executable()) {
        return std::unexpected("LoadLibrary stub cave is not in executable memory");
    }

    const std::uintptr_t result_addr = placement->data;
    const std::uintptr_t done_addr = placement->data + 8;
    const std::uintptr_t path_addr = placement->data + 16;

    std::vector<std::byte> data_block(data_size, std::byte{0});
    std::memcpy(data_block.data() + 16, wine_path.data(), wine_path.size());
    if (auto err = mem->write(placement->data, std::span(data_block)); !err) {
        return err;
    }

    const auto stub = build_iat_stub(*load_library, result_addr, path_addr, hook->iat.original);
    if (stub.size() > kStubBudget) {
        return std::unexpected("LoadLibrary stub is larger than the reserved cave");
    }
    if (auto err = write_bytes(*mem, placement->stub, stub.data(), stub.size()); !err) {
        return err;
    }

    const std::uint64_t stub_addr = placement->stub;
    if (auto err = mem->write_value(hook->iat.slot, stub_addr); !err) {
        return err;
    }
    debug("Queued LoadLibraryA via " + std::string(hook->function) + " IAT in " + hook->owner +
          " slot " + hex_addr(hook->iat.slot) + " -> stub " + hex_addr(stub_addr));

    const auto deadline = std::chrono::steady_clock::now() + kLoadWait;
    std::uint64_t done = 0;
    std::uint64_t result = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        auto done_val = mem->read_value<std::uint64_t>(done_addr);
        if (!done_val) {
            if (auto restored = mem->write_value(hook->iat.slot, hook->iat.original); !restored) {
                debug("Failed to restore IAT: " + restored.error());
            }
            return std::unexpected(done_val.error());
        }
        done = *done_val;
        if (done != 0) {
            auto result_val = mem->read_value<std::uint64_t>(result_addr);
            if (!result_val) {
                return std::unexpected(result_val.error());
            }
            result = *result_val;
            break;
        }
        std::this_thread::sleep_for(kPoll);
    }

    if (auto restored = mem->write_value(hook->iat.slot, hook->iat.original); !restored) {
        debug("Failed to restore IAT: " + restored.error());
    }

    if (done == 0) {
        return std::unexpected("Timed out waiting for LoadLibraryA via " +
                               std::string(hook->function) +
                               " (the game did not call that import, or the stub did not run)");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::vector<std::byte> zeros(std::max(stub.size(), data_size), std::byte{0});
    if (auto err = mem->write(placement->stub, std::span(zeros.data(), stub.size())); !err) {
        debug("Failed to clear stub cave: " + err.error());
    }
    if (auto err = mem->write(placement->data, std::span(zeros.data(), data_size)); !err) {
        debug("Failed to clear data cave: " + err.error());
    }

    if (result == 0) {
        return std::unexpected("LoadLibraryA returned NULL for " + wine_path);
    }

    maps = parse_maps(pid);
    if (maps) {
        const auto want = exe_basename(linux_dll_path);
        bool seen = false;
        for (const auto& mapping : *maps) {
            if (path_ends_with_ignore_case(mapping.path, want)) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            debug("LoadLibraryA succeeded but " + want + " is not yet in maps");
        }
    }
    debug("Injected " + wine_path + " via liatll (pid " + std::to_string(pid) + ")");
    return {};
}

}  // namespace proton_inject
