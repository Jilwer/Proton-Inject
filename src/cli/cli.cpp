#include "cli/cli.hpp"

#include "config/config.hpp"
#include "core/console_mode.hpp"
#include "core/injector.hpp"
#include "core/method.hpp"
#include "install/installer.hpp"
#include "utils/utils.hpp"
#include "version.hpp"

#include <CLI/CLI.hpp>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace proton_inject {

namespace {

void list_profiles(const ConfigStore& store) {
    const auto profiles = store.list_profiles_with_config();
    if (!profiles) {
        std::cerr << "error: " << profiles.error() << '\n';
        std::exit(1);
    }
    if (profiles->empty()) {
        std::cout << "No profiles found.\n";
        return;
    }
    std::cout << "Available profiles (" << profiles->size() << "):\n";
    for (const auto& profile : *profiles) {
        const auto app_id = profile.config.app_id.value_or("N/A");
        const auto exe = profile.config.target_exe.value_or("N/A");
        std::cout << "  " << profile.name << " (AppID: " << app_id << ", Exe: " << exe << ")\n";
    }
}

std::expected<void, std::string> run_install() {
    if (const auto installed = install_app(); !installed) {
        return installed;
    }

    const InstallPaths paths = install_paths();
    std::cout << "Installed proton-inject " << kVersion << "\n"
              << "  binary:  " << paths.binary.string() << "\n"
              << "  desktop: " << paths.desktop_entry.string() << "\n";
    if (!binary_dir_on_path()) {
        std::cout << "\nNote: " << paths.binary.parent_path().string()
                  << " is not on your PATH; add it to run proton-inject from a shell.\n";
    }
    return {};
}

std::expected<void, std::string> run_uninstall() {
    if (const auto removed = uninstall_app(); !removed) {
        return removed;
    }
    std::cout << "Removed proton-inject and its desktop entry.\n"
              << "Profiles and settings were left in place.\n";
    return {};
}

std::expected<InjectOptions, std::string> build_options(AppConfig& config, const CLI::App& app) {
    std::string app_id = config.app_id.value_or("");
    std::string exe = config.target_exe.value_or("");
    std::string dll = config.dll_path.value_or("");
    const bool non_steam = config.non_steam.value_or(false);

    std::string method = normalize_method(config.method.value_or(std::string(kMethodCrt)));
    if (!valid_method(method)) {
        method = std::string(kMethodCrt);
    }

    // Linux IAT methods find the game by pid and map the DLL in from Linux, so they need
    // the exe name and nothing else: no AppID, no Proton, no prefix.
    if (!is_linux_iat_method(method) && !non_steam && app_id.empty()) {
        return std::unexpected(
            "AppID is required for Steam games (use --appid, or --non-steam for umu-run)");
    }
    if (exe.empty()) {
        return std::unexpected("Exe is required (use --exe or set in config)");
    }

    bool use_loader = false;
    if (!dll.empty()) {
        use_loader = false;
    } else if (app.count("--loader") > 0) {
        use_loader = app.get_option("--loader")->as<bool>();
        if (!use_loader) {
            return std::unexpected(
                "No DLL specified: provide --dll <path> or pass --loader to use the embedded "
                "loader");
        }
        std::cerr << "Using embedded loader (mods from Documents/proton-inject-mods)\n";
    } else {
        use_loader = config.use_loader_or_default();
        if (!use_loader) {
            return std::unexpected(
                "No DLL specified and profile/config has use_loader=false: provide --dll <path> or "
                "pass --loader");
        }
        std::cerr << "Using embedded loader (from profile/config)\n";
    }

    std::string expanded_dll;
    if (!dll.empty()) {
        expanded_dll = expand_path(dll);
        if (!use_loader) {
            std::error_code ec;
            if (!fs::exists(expanded_dll, ec) || fs::is_directory(expanded_dll, ec)) {
                return std::unexpected("DLL not found at " + expanded_dll);
            }
        }
    }

    InjectOptions options;
    options.app_id = app_id;
    options.non_steam = non_steam;
    options.target_exe = exe;
    options.dll_path = expanded_dll;
    options.use_loader = use_loader;
    options.loader_console =
        normalize_console_mode(config.loader_console.value_or(std::string(kConsoleAlloc)));
    options.method = method;
    options.proton_path = config.proton_path.value_or("");
    options.wine_prefix = config.wine_prefix.value_or("");
    options.game_id = config.game_id.value_or("");
    options.sleep_ms = config.sleep_ms.value_or(0);
    return options;
}

}  // namespace

std::expected<void, std::string> run_cli(int argc, char** argv) {
    CLI::App app{"proton-inject - DLL injection for Proton games"};
    app.allow_extras(true);

    std::string app_id;
    std::string exe;
    std::string dll;
    std::string profile;
    std::string profile_new;
    bool profile_list = false;
    bool show_version = false;
    bool install = false;
    bool uninstall = false;
    bool loader = false;
    std::string loader_console;
    std::string method;
    bool non_steam = false;
    std::string proton_path;
    std::string wine_prefix;
    std::string game_id;
    std::uint32_t sleep_ms = 0;

    app.add_flag("-V,--version", show_version, "Show version and exit");
    app.add_flag("--install", install,
                 "Install to ~/.local/bin and add a desktop entry (no root needed)");
    app.add_flag("--uninstall", uninstall, "Remove the installed copy and its desktop entry");
    app.add_flag("--profile-list", profile_list, "List all available profiles");
    app.add_option("--profile-new", profile_new, "Create new profile with current configuration");
    app.add_option("--appid", app_id, "Steam AppID (required for Steam games)");
    app.add_option("--exe", exe, "Target game executable name or path");
    app.add_option("--dll", dll, "Path to DLL to inject");
    app.add_option("--profile", profile, "Load configuration from profile");
    app.add_flag("--loader", loader, "Use embedded loader (when no --dll)");
    app.add_option("--loader-console", loader_console,
                   "Loader console: alloc (new window), attach (reuse the game's existing "
                   "console, e.g. BepInEx's), none (default: alloc)");
    app.add_option("--method", method, "Injection method: crt, apc, nt, liatll (default: crt)");
    app.add_flag("--non-steam", non_steam,
                 "Non-Steam game: attach via umu-run (launch the game yourself first)");
    app.add_option("--proton-path", proton_path,
                   "Proton directory (required for --non-steam unless PROTONPATH is set)");
    app.add_option("--wine-prefix", wine_prefix,
                   "Wine prefix for --non-steam (default: ~/.proton-inject/pfx)");
    app.add_option("--game-id", game_id, "umu GAMEID for --non-steam (default: 0)");
    app.add_option("--sleep", sleep_ms, "Delay in ms before injection");

    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp&) {
        std::cout << app.help() << '\n';
        return {};
    } catch (const CLI::ParseError& error) {
        return std::unexpected(error.what());
    }

    if (show_version) {
        std::cout << "proton-inject " << kVersion << '\n';
        return {};
    }

    if (install && uninstall) {
        return std::unexpected("--install and --uninstall cannot be combined");
    }
    if (install) {
        return run_install();
    }
    if (uninstall) {
        return run_uninstall();
    }

    const auto store = ConfigStore::create();
    if (!store) {
        return std::unexpected(store.error());
    }

    if (profile_list) {
        list_profiles(*store);
        return {};
    }

    if (!profile_new.empty()) {
        const bool use_loader = dll.empty();
        const std::string* app_id_ptr = app_id.empty() ? nullptr : &app_id;
        const std::string* dll_ptr = dll.empty() ? nullptr : &dll;
        if (const auto err =
                store->create_profile(profile_new, app_id_ptr, exe, dll_ptr, use_loader);
            !err) {
            return err;
        }
        std::cout << "Profile \"" << profile_new << "\" created successfully\n";
        return {};
    }

    const std::string* profile_name = profile.empty() ? nullptr : &profile;
    auto config = store->load(profile_name);
    if (!config) {
        return std::unexpected(config.error());
    }

    if (!app_id.empty()) {
        config->app_id = app_id;
    }
    if (!exe.empty()) {
        config->target_exe = exe;
    }
    if (!dll.empty()) {
        config->dll_path = dll;
    }
    if (app.count("--method") > 0) {
        const auto normalized = normalize_method(method);
        if (!valid_method(normalized)) {
            return std::unexpected("Invalid method \"" + method + "\" (want: crt, apc, nt, liatll)");
        }
        config->method = normalized;
    }
    if (app.count("--loader-console") > 0) {
        const auto normalized = normalize_console_mode(loader_console);
        if (!valid_console_mode(normalized)) {
            return std::unexpected("Invalid loader console mode \"" + loader_console +
                                   "\" (want: alloc, attach, none)");
        }
        config->loader_console = normalized;
    }
    if (app.count("--non-steam") > 0) {
        config->non_steam = non_steam;
    }
    if (!proton_path.empty()) {
        config->proton_path = proton_path;
    }
    if (!wine_prefix.empty()) {
        config->wine_prefix = wine_prefix;
    }
    if (!game_id.empty()) {
        config->game_id = game_id;
    }
    if (app.count("--sleep") > 0) {
        config->sleep_ms = sleep_ms;
    }

    auto options = build_options(*config, app);
    if (!options) {
        return std::unexpected(options.error());
    }

    if (options->use_loader) {
        config->dll_path = std::nullopt;
    }
    if (const auto err = store->save(*config, profile_name); !err) {
        return err;
    }

    options->target_args = app.remaining();
    Injector injector;
    return injector.inject_with(*options);
}

}  // namespace proton_inject
