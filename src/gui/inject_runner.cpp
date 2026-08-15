#include "inject_runner.hpp"

#include "core/injector.hpp"
#include "utils/utils.hpp"

#include <filesystem>

namespace fs = std::filesystem;

std::string InjectRunner::resolve_proton_executable(const std::string& proton_path) {
    const std::string trimmed = proton_inject::trim(proton_path);
    if (trimmed.empty()) {
        return {};
    }

    if (fs::is_directory(trimmed)) {
        const fs::path candidate = fs::path(trimmed) / "proton";
        if (fs::exists(candidate)) {
            return candidate.string();
        }
        return {};
    }

    const fs::path info(trimmed);
    if (info.filename() == "proton" && fs::exists(info)) {
        return fs::absolute(info).string();
    }

    const fs::path sibling = info.parent_path() / "proton";
    if (fs::exists(sibling)) {
        return fs::absolute(sibling).string();
    }

    return fs::absolute(info).string();
}

bool InjectRunner::has_valid_proton_path(const std::string& proton_path) {
    const std::string exe = resolve_proton_executable(proton_path);
    return !exe.empty() && fs::exists(exe);
}

std::string InjectRunner::process_name_from_exe(const std::string& exe_path) {
    return fs::path(exe_path).filename().string();
}

bool InjectRunner::is_bare_exe_name(const std::string& exe_path) {
    const std::string trimmed = proton_inject::trim(exe_path);
    if (trimmed.empty()) {
        return false;
    }
    return trimmed.find('/') == std::string::npos && trimmed.find('\\') == std::string::npos &&
           !fs::path(trimmed).is_absolute();
}

std::string InjectRunner::resolve_launch_target(const std::string& app_id,
                                                const std::string& exe_path) {
    const auto result = proton_inject::resolve_launch_target(app_id, exe_path);
    if (!result) {
        return {};
    }
    return *result;
}

bool InjectRunner::inject_once(const InjectionConfig& config, const std::string& dll_path) {
    proton_inject::InjectOptions options;
    options.app_id = config.app_id;
    options.proton_path = config.proton_path;
    options.wine_prefix = config.wine_prefix;
    options.game_id = config.game_id;
    options.target_exe = config.exe_path;
    options.dll_path = dll_path;
    options.method = config.method;
    options.non_steam = config.mode == InjectionMode::NonSteam;
    options.use_loader = config.use_loader;
    options.loader_console = config.loader_console;
    options.sleep_ms = static_cast<std::uint32_t>(config.sleep_ms);

    proton_inject::Injector injector;
    const auto result = injector.inject_with(options);
    if (!result) {
        m_last_error = result.error();
        return false;
    }
    return true;
}

namespace {

// the callback points at the caller's stack, so it has to be cleared on every path out.
class ScopedLogCallback {
public:
    explicit ScopedLogCallback(const InjectRunner::LogCallback& callback) {
        proton_inject::g_log_callback = callback;
    }

    ScopedLogCallback(const ScopedLogCallback&) = delete;
    ScopedLogCallback& operator=(const ScopedLogCallback&) = delete;

    ~ScopedLogCallback() { proton_inject::g_log_callback = nullptr; }
};

}  // namespace

bool InjectRunner::run(const InjectionConfig& config, const LogCallback& on_output) {
    m_last_error.clear();
    const ScopedLogCallback logging(on_output);

    if (config.use_loader) {
        return inject_once(config, {});
    }

    if (config.dll_paths.empty()) {
        m_last_error = "No DLL selected.";
        return false;
    }

    for (const std::string& dll : config.dll_paths) {
        if (on_output) {
            on_output("Injecting " + dll + "...");
        }
        if (!inject_once(config, dll)) {
            return false;
        }
    }
    return true;
}
