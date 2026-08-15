#include "inject.hpp"
#include "method.hpp"
#include "procutil.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <tlhelp32.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr DWORD kKernel32TimeoutMs = 10000;

void log_process_list() {
    const injector::UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) {
        std::fprintf(stderr, "Failed to enumerate processes: %lu\n", GetLastError());
        return;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::fprintf(stderr, "Running processes:\n");
    if (Process32FirstW(snapshot.get(), &entry)) {
        do {
            std::fprintf(stderr, "  [%5lu] (ppid: %5lu) %ls\n", entry.th32ProcessID,
                         entry.th32ParentProcessID, entry.szExeFile);
        } while (Process32NextW(snapshot.get(), &entry));
    }
}

struct Args {
    std::string process_name;
    std::vector<std::string> dll_paths;
    injector::Method method = injector::Method::Crt;
    unsigned sleep_ms = 0;
};

void print_usage(const char* program) {
    std::fprintf(stderr, "Proton Inject Windows injector\n\n");
    std::fprintf(stderr,
                 "  %s -n <process.exe> -i <dll.dll> [--method crt|apc|nt] [--sleep ms]\n\n",
                 program);
}

Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-n" || arg == "--process-name") {
            if (++i < argc) {
                args.process_name = argv[i];
            }
        } else if (arg == "-i" || arg == "--inject") {
            if (++i < argc) {
                args.dll_paths.emplace_back(argv[i]);
            }
        } else if (arg == "--method") {
            if (++i < argc) {
                args.method = injector::parse_method(argv[i]);
            }
        } else if (arg == "--sleep") {
            if (++i < argc) {
                args.sleep_ms = static_cast<unsigned>(std::strtoul(argv[i], nullptr, 10));
            }
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            print_usage(argv[0]);
            std::exit(1);
        }
    }

    if (args.process_name.empty() || args.dll_paths.empty()) {
        print_usage(argv[0]);
        std::exit(1);
    }
    return args;
}

bool inject_all(const HANDLE process, const DWORD pid, const std::vector<fs::path>& dlls,
                injector::Method method) {
    std::optional<injector::ApcThread> apc;
    if (method == injector::Method::Apc) {
        apc = injector::ApcThread::prepare(pid);
        if (!apc.has_value()) {
            std::fprintf(stderr, "APC prepare failed; falling back to crt\n");
            method = injector::Method::Crt;
        }
    }

    const HANDLE apc_handle = apc.has_value() ? apc->get() : nullptr;
    for (const auto& dll : dlls) {
        if (!injector::inject_dll(process, apc_handle, dll, method)) {
            std::fprintf(stderr, "Injection failed for %s (error %lu)\n", dll.string().c_str(),
                         GetLastError());
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const Args args = parse_args(argc, argv);
    if (!injector::enable_se_debug_privilege()) {
        std::fprintf(stderr, "Warning: could not enable SeDebugPrivilege\n");
    }

    std::vector<fs::path> dlls;
    dlls.reserve(args.dll_paths.size());
    for (const auto& dll : args.dll_paths) {
        dlls.push_back(injector::resolve_module_path(dll));
    }

    const DWORD pid = injector::get_process_id_for_inject(args.process_name);
    if (pid == 0) {
        std::fprintf(stderr, "Could not find an injectable process '%s'\n",
                     args.process_name.c_str());
        log_process_list();
        return 2;
    }

    const injector::UniqueHandle process = injector::open_process_for_inject(pid);
    if (!process) {
        std::fprintf(stderr, "OpenProcess failed for %s (pid %lu, error %lu)\n",
                     args.process_name.c_str(), pid, GetLastError());
        log_process_list();
        return 2;
    }

    if (!injector::wait_for_kernel32(process.get(), kKernel32TimeoutMs)) {
        std::fprintf(stderr, "Warning: kernel32.dll not ready within timeout, continuing\n");
    }
    if (args.sleep_ms > 0) {
        Sleep(args.sleep_ms);
    }

    if (!inject_all(process.get(), pid, dlls, args.method)) {
        log_process_list();
        return 2;
    }
    return 0;
}
