#include "inject.hpp"
#include "method.hpp"
#include "procutil.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <tlhelp32.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

void log_process_list() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "Failed to enumerate processes: %lu\n", GetLastError());
        return;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::fprintf(stderr, "Running processes:\n");
    if (Process32FirstW(snapshot, &entry)) {
        do {
            std::fprintf(stderr, "  [%5lu] (ppid: %5lu) %ls\n", entry.th32ProcessID,
                         entry.th32ParentProcessID, entry.szExeFile);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

struct Args {
    std::string process_name;
    fs::path launch_exe;
    std::vector<std::string> dll_paths;
    injector::Method method = injector::Method::Crt;
    unsigned sleep_ms = 0;
    unsigned follow_sleep_ms = 0;
    bool follow_process = false;
    std::string follow_process_name;
    bool no_parent = false;
    std::vector<std::string> target_args;
    bool attach_mode = false;
    bool launch_mode = false;
};

void print_usage(const char* program) {
    std::fprintf(stderr, "Proton Inject Windows injector\n\n");
    std::fprintf(stderr, "Attach (default Steam workflow):\n");
    std::fprintf(stderr, "  %s -n <process.exe> -i <dll.dll> [options]\n\n", program);
    std::fprintf(stderr, "Launch:\n");
    std::fprintf(stderr, "  %s <target.exe> <dll.dll> [options] [-- target args...]\n\n", program);
}

Args parse_args(int argc, char** argv) {
    Args args;
    std::vector<std::string> positionals;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-n" || arg == "--process-name") {
            if (++i < argc) {
                args.process_name = argv[i];
                args.attach_mode = true;
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
        } else if (arg == "--follow-sleep") {
            if (++i < argc) {
                args.follow_sleep_ms = static_cast<unsigned>(std::strtoul(argv[i], nullptr, 10));
            }
        } else if (arg == "--follow-process") {
            args.follow_process = true;
        } else if (arg == "--follow-process-name") {
            if (++i < argc) {
                args.follow_process_name = argv[i];
            }
        } else if (arg == "--no-parent") {
            args.no_parent = true;
        } else if (arg == "--") {
            for (++i; i < argc; ++i) {
                args.target_args.emplace_back(argv[i]);
            }
            break;
        } else if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            print_usage(argv[0]);
            std::exit(1);
        } else {
            positionals.push_back(arg);
        }
    }

    if (args.process_name.empty() && !positionals.empty()) {
        args.launch_exe = positionals.front();
        args.launch_mode = true;
        for (std::size_t i = 1; i < positionals.size(); ++i) {
            if (positionals[i].starts_with("-")) {
                break;
            }
            args.dll_paths.push_back(positionals[i]);
        }
    } else if (!args.process_name.empty()) {
        args.attach_mode = true;
    }

    if (args.dll_paths.empty() || (!args.attach_mode && !args.launch_mode)) {
        print_usage(argv[0]);
        std::exit(1);
    }
    return args;
}

std::string quote_arg(const std::string& arg) {
    if (arg.find_first_of(" \t\"") == std::string::npos) {
        return arg;
    }
    std::string out = "\"";
    std::size_t backslashes = 0;
    for (const char ch : arg) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }
        if (ch == '"') {
            out.append(backslashes * 2 + 1, '\\');
            backslashes = 0;
            out.push_back('"');
            continue;
        }
        out.append(backslashes, '\\');
        backslashes = 0;
        out.push_back(ch);
    }
    out.append(backslashes, '\\');
    out.push_back('"');
    return out;
}

bool inject_into_open(HANDLE process, DWORD pid, HANDLE main_thread,
                      const std::vector<fs::path>& dlls, injector::Method method) {
    HANDLE apc_thread = nullptr;
    bool apc_owned = false;
    bool apc_suspended = false;
    if (method == injector::Method::Apc) {
        if (!injector::prepare_apc_thread(pid, main_thread, &apc_thread, &apc_owned,
                                          &apc_suspended)) {
            std::fprintf(stderr, "APC prepare failed; falling back to crt\n");
            method = injector::Method::Crt;
        }
    }

    for (const auto& dll : dlls) {
        if (!injector::inject_dll(process, apc_thread, pid, dll, method)) {
            std::fprintf(stderr, "Injection failed for %s (error %lu)\n", dll.string().c_str(),
                         GetLastError());
            if (apc_thread != nullptr) {
                injector::finish_apc_thread(apc_thread, apc_owned, apc_suspended);
            }
            return false;
        }
    }

    if (apc_thread != nullptr) {
        injector::finish_apc_thread(apc_thread, apc_owned, apc_suspended);
    }
    return true;
}

bool inject_into_pid(DWORD pid, const std::vector<fs::path>& dlls, unsigned sleep_ms,
                     injector::Method method) {
    const HANDLE process = injector::open_process_for_inject(pid);
    if (process == nullptr) {
        std::fprintf(stderr, "OpenProcess failed for pid %lu (error %lu)\n", pid, GetLastError());
        return false;
    }
    if (!injector::wait_for_kernel32(process, 10000)) {
        std::fprintf(stderr, "Warning: kernel32.dll not ready within timeout, continuing\n");
    }
    if (sleep_ms > 0) {
        Sleep(sleep_ms);
    }
    const bool ok = inject_into_open(process, pid, nullptr, dlls, method);
    CloseHandle(process);
    return ok;
}

bool run_attach(const Args& args, const std::vector<fs::path>& dlls) {
    const DWORD pid = injector::get_process_id_for_inject(args.process_name);
    if (pid == 0) {
        std::fprintf(stderr, "Could not find an injectable process '%s'\n",
                     args.process_name.c_str());
        log_process_list();
        return false;
    }

    const HANDLE process = injector::open_process_for_inject(pid);
    if (process == nullptr) {
        std::fprintf(stderr, "OpenProcess failed for %s (pid %lu, error %lu)\n",
                     args.process_name.c_str(), pid, GetLastError());
        log_process_list();
        return false;
    }

    if (args.no_parent) {
        while (injector::process_still_running(process)) {
            std::optional<DWORD> child;
            if (!args.follow_process_name.empty()) {
                child = injector::find_descendant_by_name(pid, args.follow_process_name);
            } else {
                FILETIME now{};
                GetSystemTimeAsFileTime(&now);
                child = injector::select_follow_candidate(pid, injector::process_image_path(pid),
                                                          &now, nullptr);
            }
            if (child.has_value()) {
                inject_into_pid(*child, dlls, args.follow_sleep_ms, args.method);
            } else {
                Sleep(500);
            }
        }
        CloseHandle(process);
        return true;
    }

    if (!injector::wait_for_kernel32(process, 10000)) {
        std::fprintf(stderr, "Warning: kernel32.dll not ready within timeout, continuing\n");
    }
    if (args.sleep_ms > 0) {
        Sleep(args.sleep_ms);
    }
    if (!inject_into_open(process, pid, nullptr, dlls, args.method)) {
        log_process_list();
        CloseHandle(process);
        return false;
    }

    if (args.follow_process) {
        WaitForSingleObject(process, INFINITE);
        if (injector::process_exit_code(process) == 0) {
            const auto times = injector::process_times(process);
            if (times.has_value()) {
                const auto parent_exe = injector::process_image_path(pid);
                const char* follow_name =
                    args.follow_process_name.empty() ? nullptr : args.follow_process_name.c_str();
                if (const auto child = injector::select_follow_candidate(
                        pid, parent_exe, &times->second, follow_name);
                    child.has_value()) {
                    inject_into_pid(*child, dlls, args.follow_sleep_ms, args.method);
                }
            }
        }
    }

    CloseHandle(process);
    return true;
}

bool run_launch(const Args& args, const std::vector<fs::path>& dlls) {
    const fs::path target = fs::absolute(args.launch_exe);
    if (!fs::exists(target)) {
        std::fprintf(stderr, "Target executable not found: %s\n", target.string().c_str());
        return false;
    }

    const fs::path workdir = target.parent_path();
    std::string cmdline = quote_arg(target.string());
    for (const auto& arg : args.target_args) {
        cmdline.push_back(' ');
        cmdline.append(quote_arg(arg));
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdline_wide(cmdline.begin(), cmdline.end());
    cmdline_wide.push_back(L'\0');
    const std::wstring workdir_wide = workdir.wstring();

    if (!CreateProcessW(nullptr, cmdline_wide.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                        nullptr, workdir_wide.c_str(), &si, &pi)) {
        return false;
    }

    ResumeThread(pi.hThread);

    if (args.no_parent) {
        while (injector::process_still_running(pi.hProcess)) {
            std::optional<DWORD> child;
            if (!args.follow_process_name.empty()) {
                child = injector::find_descendant_by_name(pi.dwProcessId, args.follow_process_name);
            } else {
                FILETIME now{};
                GetSystemTimeAsFileTime(&now);
                child = injector::select_follow_candidate(pi.dwProcessId, target, &now, nullptr);
            }
            if (child.has_value()) {
                inject_into_pid(*child, dlls, args.follow_sleep_ms, args.method);
            } else {
                Sleep(500);
            }
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    if (!injector::wait_for_kernel32(pi.hProcess, 10000)) {
        std::fprintf(stderr, "Warning: kernel32.dll not ready within timeout, continuing\n");
    }
    if (args.sleep_ms > 0) {
        Sleep(args.sleep_ms);
    }
    if (!inject_into_open(pi.hProcess, pi.dwProcessId, pi.hThread, dlls, args.method)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return false;
    }

    if (!args.follow_process) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    if (injector::process_exit_code(pi.hProcess) == 0) {
        const auto times = injector::process_times(pi.hProcess);
        if (times.has_value()) {
            const char* follow_name =
                args.follow_process_name.empty() ? nullptr : args.follow_process_name.c_str();
            if (const auto child = injector::select_follow_candidate(pi.dwProcessId, target,
                                                                     &times->second, follow_name);
                child.has_value()) {
                inject_into_pid(*child, dlls, args.follow_sleep_ms, args.method);
            }
        }
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const Args args = parse_args(argc, argv);
    if (!injector::enable_se_debug_privilege()) {
        std::fprintf(stderr, "Warning: could not enable SeDebugPrivilege\n");
    }

    std::vector<fs::path> dlls;
    for (const auto& dll : args.dll_paths) {
        dlls.push_back(injector::resolve_module_path(dll));
    }

    const bool ok = args.attach_mode ? run_attach(args, dlls) : run_launch(args, dlls);
    return ok ? 0 : 2;
}
