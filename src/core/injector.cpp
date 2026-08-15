#include "core/injector.hpp"

#include "core/console_mode.hpp"
#include "core/embedded_assets.hpp"
#include "core/method.hpp"
#include "native/inject_mem.hpp"
#include "native/process.hpp"
#include "proton/proton.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <regex>
#include <sstream>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace fs = std::filesystem;

namespace proton_inject {

namespace {

// Steam and umu both hand off to a launcher chain, so the game's own process shows up well
// after the command returns.
constexpr int kProcessWaitSeconds = 30;

std::expected<std::string, std::string> validate_launch_target(const std::string& target) {
    std::error_code ec;
    const auto absolute = fs::absolute(target, ec);
    if (ec) {
        return std::unexpected("Could not resolve launch target \"" + target +
                               "\": " + ec.message());
    }
    if (!fs::exists(absolute)) {
        return std::unexpected("Launch target not found at " + absolute.string());
    }
    if (fs::is_directory(absolute)) {
        return std::unexpected("Launch target is a directory: " + absolute.string());
    }
    return absolute.string();
}

std::string find_executable_under(const fs::path& game_dir, const std::string& target) {
    std::string relative = target;
    std::replace(relative.begin(), relative.end(), '\\', '/');
    if (const auto resolved = validate_launch_target((game_dir / relative).string()); resolved) {
        return *resolved;
    }

    const auto wanted = to_lower(exe_basename(relative));

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(
             game_dir, fs::directory_options::skip_permission_denied, ec)) {
        if (entry.is_regular_file() && to_lower(entry.path().filename().string()) == wanted) {
            return entry.path().string();
        }
    }
    return {};
}

std::string resolve_steam_launch_target(const std::string& app_id, const std::string& target) {
    static const std::regex install_dir_re(R"rx("installdir"\s+"([^"]+)")rx", std::regex::icase);
    for (const auto& library : steam_library_roots()) {
        const auto manifest = fs::path(library) / "steamapps" / ("appmanifest_" + app_id + ".acf");
        std::ifstream input(manifest);
        if (!input) {
            continue;
        }
        const std::string content((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
        std::smatch match;
        if (!std::regex_search(content, match, install_dir_re) || match.size() < 2) {
            continue;
        }
        const auto game_dir = fs::path(library) / "steamapps" / "common" / match[1].str();
        if (const auto resolved = find_executable_under(game_dir, target); !resolved.empty()) {
            return resolved;
        }
    }
    return {};
}

std::expected<void, std::string> write_embedded_file(const fs::path& path,
                                                     const unsigned char* data, std::size_t size,
                                                     fs::perms permissions) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return std::unexpected("Failed to create " + path.string() + ": " + std::strerror(errno));
    }
    output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!output) {
        return std::unexpected("Failed to write " + std::to_string(size) + " bytes to " +
                               path.string() + ": " + std::strerror(errno));
    }
    std::error_code ec;
    fs::permissions(path, permissions, ec);
    return {};
}

// the loader reads this from beside its own module: the game process is not ours to give
// an environment or a command line to, so a staged file is the only way in.
std::expected<void, std::string> write_loader_settings(const fs::path& path,
                                                       const std::string& console_mode) {
    std::ofstream output(path);
    if (!output) {
        return std::unexpected("Failed to write loader settings to " + path.string());
    }
    output << "console=" << console_mode << '\n';
    if (!output) {
        return std::unexpected("Failed to write loader settings to " + path.string());
    }
    return {};
}

void relay_output_lines(std::string& pending, std::string_view chunk) {
    pending.append(chunk);
    std::size_t pos = 0;
    while ((pos = pending.find('\n')) != std::string::npos) {
        auto line = pending.substr(0, pos);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            debug(line);
        }
        pending.erase(0, pos + 1);
    }
}

std::string describe_exit_status(int status) {
    if (WIFEXITED(status)) {
        return "exit code " + std::to_string(WEXITSTATUS(status));
    }
    if (WIFSIGNALED(status)) {
        return "signal " + std::to_string(WTERMSIG(status));
    }
    return "status " + std::to_string(status);
}

// randomize the staged loader's filename so it does not present the same signature on disk or in
// the module list on every injection. The loader locates loader.cfg relative to its own module
// path, so the name itself carries no meaning.
std::string random_loader_name() {
    static constexpr std::string_view kAlphabet = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device device;
    std::mt19937 generator(device());
    std::uniform_int_distribution<std::size_t> pick(0, kAlphabet.size() - 1);

    std::string name;
    name.reserve(12);
    for (int i = 0; i < 12; ++i) {
        name.push_back(kAlphabet[pick(generator)]);
    }
    name += ".dll";
    return name;
}

std::string injector_dll_argument(const std::string& target_dll, const bool staged_basenames) {
    if (!staged_basenames) {
        return to_windows_path(target_dll);
    }
    return fs::path(target_dll).filename().string();
}

std::vector<std::string> read_null_env_block(const fs::path& environ_path) {
    std::ifstream input(environ_path, std::ios::binary);
    if (!input) {
        return {};
    }
    const std::string raw((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());
    std::vector<std::string> entries;
    std::size_t start = 0;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\0') {
            continue;
        }
        if (i > start) {
            entries.emplace_back(raw.data() + start, i - start);
        }
        start = i + 1;
    }
    return entries;
}

std::vector<std::string> wineserver_env_for_prefix(const std::string& compat_data) {
    const std::string wineprefix = (fs::path(compat_data) / "pfx").string();
    const std::string prefix_needle = "WINEPREFIX=" + wineprefix;
    static const char* kKeys[] = {"WINEESYNC=", "WINEFSYNC="};

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/proc", ec)) {
        if (ec || !entry.is_directory()) {
            continue;
        }
        const auto pid_name = entry.path().filename().string();
        if (pid_name.empty() || !std::isdigit(static_cast<unsigned char>(pid_name.front()))) {
            continue;
        }

        std::ifstream comm(entry.path() / "comm");
        std::string comm_name;
        if (!comm || !std::getline(comm, comm_name) || comm_name != "wineserver") {
            continue;
        }

        const auto env_entries = read_null_env_block(entry.path() / "environ");
        bool prefix_match = false;
        for (const auto& env_entry : env_entries) {
            if (env_entry == prefix_needle) {
                prefix_match = true;
                break;
            }
        }
        if (!prefix_match) {
            continue;
        }

        std::vector<std::string> copied;
        for (const auto& env_entry : env_entries) {
            for (const char* key : kKeys) {
                if (env_entry.starts_with(key)) {
                    copied.push_back(env_entry);
                }
            }
        }
        if (!copied.empty()) {
            debug("Reusing wineserver sync settings from pid " + pid_name);
        }
        return copied;
    }
    return {};
}

std::string proton_log_path(const std::string& app_id) {
    const auto home = expand_path("~");
    if (home.empty() || app_id.empty()) {
        return {};
    }
    return (fs::path(home) / ("steam-" + app_id + ".log")).string();
}

void relay_proton_log_tail(const std::string& app_id) {
    const auto log_path = proton_log_path(app_id);
    if (log_path.empty()) {
        return;
    }
    std::ifstream input(log_path);
    if (!input) {
        return;
    }

    std::vector<std::string> lines;
    for (std::string line; std::getline(input, line);) {
        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
    }
    if (lines.empty()) {
        return;
    }

    const std::size_t start = lines.size() > 40 ? lines.size() - 40 : 0;
    debug("--- proton log (last lines) ---");
    for (std::size_t i = start; i < lines.size(); ++i) {
        debug(lines[i]);
    }
    debug("--- end proton log ---");
}

std::string extract_proton_log_error(const std::string& app_id) {
    const auto log_path = proton_log_path(app_id);
    if (log_path.empty()) {
        return {};
    }
    std::ifstream input(log_path);
    if (!input) {
        return {};
    }

    std::string last_error;
    for (std::string line; std::getline(input, line);) {
        if (line.find("OpenProcess failed") != std::string::npos ||
            line.find("Could not find an injectable process") != std::string::npos ||
            line.find("Injection failed") != std::string::npos ||
            line.find("Failed to find kernel32.dll") != std::string::npos ||
            line.find("LoadLibraryA returned NULL") != std::string::npos) {
            last_error = line;
        }
    }
    return last_error;
}

std::expected<void, std::string> run_command(const std::string& program,
                                             const std::vector<std::string>& args,
                                             const std::string& working_dir,
                                             const std::vector<std::string>& extra_env) {
    std::vector<std::string> env_storage;
    env_storage.reserve(extra_env.size());
    std::vector<char*> envp;

    for (char** current = environ; current != nullptr && *current != nullptr; ++current) {
        envp.push_back(*current);
    }
    for (const auto& entry : extra_env) {
        env_storage.push_back(entry);
        envp.push_back(env_storage.back().data());
    }
    envp.push_back(nullptr);

    std::vector<std::vector<char>> arg_storage;
    arg_storage.reserve(args.size() + 2);
    arg_storage.emplace_back(program.begin(), program.end());
    arg_storage.back().push_back('\0');
    for (const auto& arg : args) {
        arg_storage.emplace_back(arg.begin(), arg.end());
        arg_storage.back().push_back('\0');
    }

    std::vector<char*> argv;
    argv.reserve(arg_storage.size() + 1);
    for (auto& arg : arg_storage) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    std::array<int, 2> pipe_fd{};
    if (pipe(pipe_fd.data()) != 0) {
        return std::unexpected("Failed to create output pipe");
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return std::unexpected("Failed to fork process");
    }
    if (pid == 0) {
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);
        if (!working_dir.empty()) {
            if (chdir(working_dir.c_str()) != 0) {
                _exit(127);
            }
        }
        execve(program.c_str(), argv.data(), envp.data());
        _exit(127);
    }

    close(pipe_fd[1]);

    int status = 0;
    std::array<char, 4096> buffer{};
    std::string captured;
    std::string pending;
    while (true) {
        const ssize_t bytes = read(pipe_fd[0], buffer.data(), buffer.size());
        if (bytes > 0) {
            const std::string_view chunk(buffer.data(), static_cast<std::size_t>(bytes));
            captured.append(chunk);
            relay_output_lines(pending, chunk);
            continue;
        }
        if (bytes == 0) {
            break;
        }
        if (errno != EINTR) {
            close(pipe_fd[0]);
            return std::unexpected("Failed to read child process output");
        }
    }

    close(pipe_fd[0]);

    if (waitpid(pid, &status, 0) < 0) {
        return std::unexpected("Failed to wait for child process");
    }

    if (!pending.empty()) {
        debug(pending);
        pending.clear();
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::string error = program + " " + describe_exit_status(status);
        if (!captured.empty()) {
            error += ":\n" + captured;
        }
        return std::unexpected(error);
    }
    return {};
}

}  // namespace

std::expected<std::string, std::string> resolve_launch_target(const std::string& app_id,
                                                              const std::string& target) {
    auto expanded = expand_path(target);
    if (expanded.empty()) {
        return std::unexpected("Launch requires an executable path");
    }
    if (fs::path(expanded).is_absolute()) {
        return validate_launch_target(expanded);
    }
    if (!app_id.empty()) {
        if (const auto resolved = resolve_steam_launch_target(app_id, expanded);
            !resolved.empty()) {
            return resolved;
        }
    }
    if (expanded.find_first_of("/\\") == std::string::npos) {
        if (app_id.empty()) {
            return std::unexpected("Launch requires a path to the executable, not only \"" +
                                   expanded + "\"");
        }
        return std::unexpected("Could not find \"" + expanded +
                               "\" in the Steam installation for app " + app_id);
    }
    return validate_launch_target(expanded);
}

std::vector<std::string> build_injector_args(const InjectOptions& options,
                                             const std::string& target_dll,
                                             const bool staged_basenames) {
    const auto dll_arg = injector_dll_argument(target_dll, staged_basenames);
    std::vector<std::string> args;

    // both Steam and non-Steam attach to an already-running game process.
    args.push_back("-n");
    args.push_back(exe_basename(options.target_exe));
    args.push_back("-i");
    args.push_back(dll_arg);

    args.push_back("--method");
    args.push_back(normalize_method(options.method));
    if (options.sleep_ms > 0) {
        args.push_back("--sleep");
        args.push_back(std::to_string(options.sleep_ms));
    }
    return args;
}

std::expected<void, std::string> Injector::inject_with(const InjectOptions& options) const {
    InjectOptions resolved = options;
    resolved.method = normalize_method(options.method);
    if (!valid_method(resolved.method)) {
        return std::unexpected("Unsupported injection method \"" + options.method + "\"");
    }
    resolved.loader_console = normalize_console_mode(options.loader_console);
    if (!valid_console_mode(resolved.loader_console)) {
        return std::unexpected("Unsupported loader console mode \"" + options.loader_console +
                               "\" (want: alloc, attach, none)");
    }
    if (resolved.target_exe.empty()) {
        return std::unexpected("Invalid target executable name");
    }

    const auto stage_template = (fs::temp_directory_path() / "proton-inject-XXXXXX").string();
    std::vector<char> stage_buffer(stage_template.begin(), stage_template.end());
    stage_buffer.push_back('\0');
    if (mkdtemp(stage_buffer.data()) == nullptr) {
        return std::unexpected("Failed to create staging directory");
    }
    const fs::path stage_path(stage_buffer.data());

    struct StageGuard {
        fs::path path;

        ~StageGuard() {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    } guard{stage_path};

    std::string target_dll = resolved.dll_path;
    if (resolved.use_loader) {
        target_dll = (stage_path / random_loader_name()).string();
        if (const auto err = write_embedded_file(
                target_dll, embedded::loader_dll, embedded::loader_dll_size,
                fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
            !err) {
            return err;
        }
        debug("Staged loader at " + target_dll + " (" + std::to_string(embedded::loader_dll_size) +
              " bytes)");
        if (const auto err =
                write_loader_settings(stage_path / "loader.cfg", resolved.loader_console);
            !err) {
            return err;
        }
        debug("Loader console mode: " + resolved.loader_console);
    } else {
        std::error_code ec;
        if (!fs::exists(target_dll, ec) || fs::is_directory(target_dll, ec)) {
            return std::unexpected("DLL not found at " + target_dll);
        }
    }

    if (is_linux_iat_method(resolved.method)) {
        return run_iat(resolved, target_dll);
    }

    const auto local_injector = stage_path / "injector.exe";
    if (const auto err = write_embedded_file(
            local_injector, embedded::injector_exe, embedded::injector_exe_size,
            fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
        !err) {
        return err;
    }
    debug("Staged injector at " + local_injector.string() + " (" +
          std::to_string(embedded::injector_exe_size) + " bytes)");

    // Steam runs the injector inside the prefix via runinprefix with the stage dir as CWD, so
    // basenames resolve; umu-run needs absolute Z:\ paths instead.
    const bool use_staged_basenames = !resolved.non_steam;
    const auto injector_args = build_injector_args(resolved, target_dll, use_staged_basenames);
    if (resolved.non_steam) {
        return run_umu(resolved, stage_path.string(), local_injector.string(), injector_args);
    }
    return run_steam(resolved, stage_path.string(), local_injector.string(), injector_args);
}

std::expected<void, std::string> Injector::run_iat(const InjectOptions& options,
                                                   const std::string& target_dll) const {
    debug("Waiting for game process to be ready...");
    if (!wait_for_process(options.target_exe)) {
        return std::unexpected("Game process not ready: process " +
                               exe_basename(options.target_exe) + " not found within " +
                               std::to_string(kProcessWaitSeconds) + "s");
    }
    debug("Game process is ready");

    if (options.sleep_ms > 0) {
        debug("Sleeping " + std::to_string(options.sleep_ms) + " ms before " + options.method +
              " injection");
        std::this_thread::sleep_for(std::chrono::milliseconds(options.sleep_ms));
    }

    std::string last_error = "kernel32.dll is not mapped yet";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    pid_t pid = -1;
    while (std::chrono::steady_clock::now() < deadline) {
        auto found = find_wine_target_pid(options.target_exe);
        if (found) {
            pid = *found;
            break;
        }
        last_error = found.error();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (pid < 0) {
        return std::unexpected(last_error);
    }
    debug("Using pid " + std::to_string(pid) + " for " + options.method + " injection");
    return inject_dll_via_iat(pid, target_dll);
}

std::expected<void, std::string> Injector::run_steam(
    const InjectOptions& options, const std::string& stage_dir,
    const std::string& /*local_injector*/, const std::vector<std::string>& injector_args) const {
    if (options.app_id.empty()) {
        return std::unexpected("AppID is required for Steam games");
    }

    const auto install = resolve_proton(options.app_id);
    if (!install) {
        return std::unexpected(install.error());
    }
    const auto compat_data = compat_data_path(options.app_id);
    if (compat_data.empty()) {
        return std::unexpected("No Proton prefix found for app " + options.app_id +
                               ": launch the game through Steam at least once");
    }
    const auto client_root = proton_inject::steam_root();
    if (client_root.empty()) {
        return std::unexpected("Could not locate the Steam installation directory");
    }

    debug("Using Proton \"" + install->name + "\" at " + install->path);
    debug("Using prefix " + compat_data);

    std::vector<std::string> args{"runinprefix", "injector.exe"};
    args.insert(args.end(), injector_args.begin(), injector_args.end());

    debug("Waiting for game process to be ready...");
    if (!wait_for_process(options.target_exe)) {
        return std::unexpected("Game process not ready: process " +
                               exe_basename(options.target_exe) + " not found within " +
                               std::to_string(kProcessWaitSeconds) + "s");
    }
    debug("Game process is ready");

    std::string program = install->script_path();
    if (!is_executable(program)) {
        args.insert(args.begin(), program);
        program = "python3";
    }

    std::ostringstream command;
    command << program;
    for (const auto& arg : args) {
        command << ' ' << arg;
    }
    debug("Executing: " + command.str());

    std::vector<std::string> extra_env{
        "STEAM_COMPAT_DATA_PATH=" + compat_data, "STEAM_COMPAT_CLIENT_INSTALL_PATH=" + client_root,
        "STEAM_COMPAT_APP_ID=" + options.app_id, "SteamAppId=" + options.app_id,
        "SteamGameId=" + options.app_id,         "PROTON_LOG=1",
    };
    const auto wineserver_env = wineserver_env_for_prefix(compat_data);
    extra_env.insert(extra_env.end(), wineserver_env.begin(), wineserver_env.end());

    const auto result = run_command(program, args, stage_dir, extra_env);
    relay_proton_log_tail(options.app_id);
    if (result) {
        return result;
    }

    std::string error = result.error();
    if (const auto detail = extract_proton_log_error(options.app_id); !detail.empty()) {
        error += "\n" + detail;
    }
    return std::unexpected(error);
}

std::expected<void, std::string> Injector::run_umu(
    const InjectOptions& options, const std::string& stage_dir, const std::string& local_injector,
    const std::vector<std::string>& injector_args) const {
    const auto umu = find_in_path("umu-run");
    if (umu.empty()) {
        return std::unexpected("umu-run not found in PATH (required for non-Steam games)");
    }

    auto proton_path = expand_path(options.proton_path);
    if (proton_path.empty()) {
        if (const char* env = std::getenv("PROTONPATH"); env != nullptr && *env != '\0') {
            proton_path = expand_path(env);
        }
    }
    if (proton_path.empty()) {
        if (const char* env = std::getenv("PROTON_PATH"); env != nullptr && *env != '\0') {
            proton_path = expand_path(env);
        }
    }
    if (proton_path.empty()) {
        return std::unexpected(
            "Proton path is required for non-Steam games (set --proton-path or PROTONPATH)");
    }
    if (fs::path(proton_path).filename() == "proton") {
        proton_path = fs::path(proton_path).parent_path().string();
    }
    if (!is_proton_dir(proton_path)) {
        return std::unexpected("PROTONPATH " + proton_path + " does not contain a proton script");
    }

    auto prefix = expand_path(options.wine_prefix);
    if (prefix.empty()) {
        prefix = default_wine_prefix();
    }
    std::error_code ec;
    fs::create_directories(prefix, ec);
    if (ec) {
        return std::unexpected("Failed to create wine prefix: " + ec.message());
    }

    auto game_id = options.game_id;
    if (game_id.empty()) {
        game_id = "0";
    }

    debug("Waiting for game process to be ready...");
    if (!wait_for_process(options.target_exe)) {
        return std::unexpected("Game process not ready: process " +
                               exe_basename(options.target_exe) + " not found within " +
                               std::to_string(kProcessWaitSeconds) + "s");
    }
    debug("Game process is ready");

    std::vector<std::string> args{local_injector};
    args.insert(args.end(), injector_args.begin(), injector_args.end());

    std::ostringstream command;
    command << umu;
    for (const auto& arg : args) {
        command << ' ' << arg;
    }
    debug("Executing: " + command.str());
    debug("PROTONPATH=" + proton_path + " WINEPREFIX=" + prefix + " GAMEID=" + game_id);

    return run_command(umu, args, stage_dir,
                       {"PROTONPATH=" + proton_path, "WINEPREFIX=" + prefix, "GAMEID=" + game_id});
}

bool Injector::wait_for_process(const std::string& process_name) const {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(kProcessWaitSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (is_process_running(process_name)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return false;
}

}  // namespace proton_inject
