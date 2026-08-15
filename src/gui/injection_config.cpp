#include "injection_config.hpp"

namespace {

// the store treats an absent field as "not set"; the GUI always has a value, so only
// meaningful ones are written back.
void set_if_present(std::optional<std::string>& target, const std::string& value) {
    if (!value.empty()) {
        target = value;
    }
}

}  // namespace

proton_inject::AppConfig to_app_config(const InjectionConfig& config) {
    proton_inject::AppConfig out;
    set_if_present(out.app_id, config.app_id);
    set_if_present(out.target_exe, config.exe_path);
    set_if_present(out.proton_path, config.proton_path);
    set_if_present(out.wine_prefix, config.wine_prefix);
    set_if_present(out.game_id, config.game_id);
    set_if_present(out.method, config.method);

    out.use_loader = config.use_loader;
    if (config.use_loader) {
        set_if_present(out.loader_console, config.loader_console);
    } else if (!config.dll_paths.empty()) {
        out.dll_path = config.dll_paths.front();
    }

    if (config.mode == InjectionMode::NonSteam) {
        out.non_steam = true;
    }
    if (config.sleep_ms > 0) {
        out.sleep_ms = static_cast<std::uint32_t>(config.sleep_ms);
    }
    return out;
}

InjectionConfig from_app_config(const proton_inject::AppConfig& config) {
    InjectionConfig out;
    out.mode = config.non_steam.value_or(false) ? InjectionMode::NonSteam : InjectionMode::Steam;
    out.app_id = config.app_id.value_or("");
    out.exe_path = config.target_exe.value_or("");
    out.proton_path = config.proton_path.value_or("");
    out.wine_prefix = config.wine_prefix.value_or("");
    out.game_id = config.game_id.value_or("");
    out.use_loader = config.use_loader_or_default();

    if (const auto console = config.loader_console.value_or(""); !console.empty()) {
        out.loader_console = console;
    }
    if (const auto method = config.method.value_or(""); !method.empty()) {
        out.method = method;
    }
    if (const auto dll = config.dll_path.value_or(""); !dll.empty()) {
        out.dll_paths = {dll};
    }
    out.sleep_ms = static_cast<int>(config.sleep_ms.value_or(0));
    return out;
}
