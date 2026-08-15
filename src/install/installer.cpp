#include "install/installer.hpp"

#include <QImage>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <array>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace proton_inject {

namespace {

constexpr const char* kDesktopId = "proton-inject";

// hicolor promises icons of the directory's own size, so every size gets its own bitmap.
constexpr std::array<int, 9> kIconSizes{16, 22, 24, 32, 48, 64, 128, 256, 512};

fs::path home_directory() {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return home;
    }
    return {};
}

fs::path data_home() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0') {
        return xdg;
    }
    return home_directory() / ".local/share";
}

fs::path executable_path() {
    std::error_code ec;
    const fs::path resolved = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path{} : resolved;
}

fs::path icon_path_for(int size) {
    const std::string dimensions = std::to_string(size) + "x" + std::to_string(size);
    return install_paths().icons_root / dimensions / "apps" / (std::string(kDesktopId) + ".png");
}

bool same_file(const fs::path& left, const fs::path& right) {
    std::error_code ec;
    return fs::equivalent(left, right, ec);
}

std::expected<void, std::string> install_binary(const fs::path& source,
                                                const fs::path& destination) {
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    if (ec) {
        return std::unexpected("could not create " + destination.parent_path().string() + ": " +
                               ec.message());
    }

    // re-installing from the installed copy: only the entry and icons need refreshing.
    if (same_file(source, destination)) {
        return {};
    }

    // a running binary refuses to be written to in place, so replace the file rather than
    // overwrite it.
    fs::remove(destination, ec);
    fs::copy_file(source, destination, ec);
    if (ec) {
        return std::unexpected("could not copy to " + destination.string() + ": " + ec.message());
    }

    fs::permissions(destination,
                    fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                        fs::perms::others_read | fs::perms::others_exec,
                    ec);
    return {};
}

void install_icons(const fs::path& source) {
    const QImage image(QString::fromStdString(source.string()));
    if (image.isNull()) {
        return;
    }

    for (const int size : kIconSizes) {
        const fs::path destination = icon_path_for(size);
        std::error_code ec;
        fs::create_directories(destination.parent_path(), ec);
        image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .save(QString::fromStdString(destination.string()), "PNG");
    }
}

// a themed icon name would lose to whatever the active theme ships for its stripped prefix:
// lookup drops everything after the last hyphen, so "proton-inject" resolves to a theme's
// "proton" icon (Proton VPN and friends) wherever one exists. An absolute path cannot be
// hijacked that way, and falling back to the name at least survives a missing icon.
std::string desktop_icon_value() {
    const fs::path installed = installed_icon_path();
    return fs::exists(installed) ? installed.string() : std::string(kDesktopId);
}

std::expected<void, std::string> write_desktop_entry(const fs::path& exec_path) {
    const fs::path path = install_paths().desktop_entry;
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    std::ofstream entry(path);
    if (!entry) {
        return std::unexpected("could not write " + path.string());
    }

    entry << "[Desktop Entry]\n"
          << "Type=Application\n"
          << "Name=Proton Inject\n"
          << "Comment=Inject DLLs into Proton/Steam games\n"
          << "Exec=\"" << exec_path.string() << "\"\n"
          << "Icon=" << desktop_icon_value() << "\n"
          << "Terminal=false\n"
          // one main category only, so the entry cannot show up twice in a menu.
          << "Categories=Game;\n"
          << "Keywords=proton;steam;wine;dll;mod;\n"
          << "StartupWMClass=" << kDesktopId << "\n";
    return {};
}

// the caches are what make an entry appear without a re-login. A missing tool is not a
// failed install, so nothing here reports back.
void refresh_desktop_caches() {
    const InstallPaths paths = install_paths();
    QProcess::startDetached(QStringLiteral("update-desktop-database"),
                            {QString::fromStdString(paths.desktop_entry.parent_path().string())});

    // gtk-update-icon-cache rejects a theme directory without an index, so skip it rather
    // than print its complaint.
    if (fs::exists(paths.icons_root / "index.theme")) {
        QProcess::startDetached(QStringLiteral("gtk-update-icon-cache"),
                                {QStringLiteral("-q"), QStringLiteral("-t"),
                                 QString::fromStdString(paths.icons_root.string())});
    }
}

}  // namespace

InstallPaths install_paths() {
    return {.binary = home_directory() / ".local/bin" / kDesktopId,
            .desktop_entry = data_home() / "applications" / (std::string(kDesktopId) + ".desktop"),
            .icons_root = data_home() / "icons/hicolor"};
}

fs::path bundled_icon_path() {
    const fs::path exe_dir = executable_path().parent_path();
    const std::array<fs::path, 3> candidates{exe_dir / "icon.png", exe_dir / "assets/icon.png",
                                             fs::path(PROTON_INJECT_ASSETS_DIR) / "icon.png"};

    for (const fs::path& candidate : candidates) {
        if (fs::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

fs::path installed_icon_path() {
    // large enough for any menu or task switcher to scale down cleanly.
    return icon_path_for(256);
}

bool binary_dir_on_path() {
    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return false;
    }

    const fs::path bin_dir = install_paths().binary.parent_path().lexically_normal();
    for (const QString& entry :
         QString::fromUtf8(path_env).split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        if (fs::path(entry.toStdString()).lexically_normal() == bin_dir) {
            return true;
        }
    }
    return false;
}

std::expected<void, std::string> install_app() {
    const fs::path source = executable_path();
    if (source.empty()) {
        return std::unexpected("could not resolve the running executable");
    }

    const InstallPaths paths = install_paths();
    if (const auto copied = install_binary(source, paths.binary); !copied) {
        return copied;
    }

    // an installed copy has no assets beside it, so the theme icons are the only ones the
    // desktop entry can rely on.
    if (const fs::path icon = bundled_icon_path(); !icon.empty()) {
        install_icons(icon);
    }

    if (const auto written = write_desktop_entry(paths.binary); !written) {
        return written;
    }

    refresh_desktop_caches();
    return {};
}

std::expected<void, std::string> uninstall_app() {
    const InstallPaths paths = install_paths();

    std::vector<fs::path> targets{paths.binary, paths.desktop_entry};
    for (const int size : kIconSizes) {
        targets.push_back(icon_path_for(size));
    }

    bool removed_any = false;
    for (const fs::path& target : targets) {
        std::error_code ec;
        removed_any |= fs::remove(target, ec);
    }

    if (!removed_any) {
        return std::unexpected("nothing to uninstall: no installed copy in " +
                               paths.binary.parent_path().string());
    }

    refresh_desktop_caches();
    return {};
}

}  // namespace proton_inject
