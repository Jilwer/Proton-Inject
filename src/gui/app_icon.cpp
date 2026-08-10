#include "gui/app_icon.hpp"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QPixmap>
#include <QProcess>
#include <QString>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace proton_inject {

namespace {

constexpr const char* k_desktop_id = "proton-inject";

fs::path home_directory() {
    return fs::path(QDir::homePath().toStdString());
}

void install_theme_icons(const fs::path& icon_path) {
    const fs::path icons_root = home_directory() / ".local/share/icons/hicolor";

    const QPixmap source(QString::fromStdString(icon_path.string()));
    if (source.isNull()) {
        return;
    }

    const int sizes[] = {16, 22, 24, 32, 48, 64, 128, 256, 512};
    for (const int size : sizes) {
        const std::string dimensions = std::to_string(size) + "x" + std::to_string(size);
        const fs::path dest = icons_root / (dimensions + "/apps/" + k_desktop_id + ".png");
        fs::create_directories(dest.parent_path());
        // each directory promises icons of its own size, so scale rather than copy one
        // oversized bitmap into all of them.
        source.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .save(QString::fromStdString(dest.string()), "PNG");
    }

    QProcess::startDetached(
        QStringLiteral("gtk-update-icon-cache"),
        {QStringLiteral("-q"), QStringLiteral("-t"), QString::fromStdString(icons_root.string())});
}

std::string executable_path() {
    std::error_code ec;
    const fs::path resolved = fs::read_symlink("/proc/self/exe", ec);
    return ec ? std::string{} : resolved.string();
}

}  // namespace

void setup_app_icon() {
    const fs::path icon_path = fs::path(PROTON_INJECT_ASSETS_DIR) / "icon.png";
    if (!fs::exists(icon_path)) {
        return;
    }

    QApplication::setWindowIcon(QIcon(QString::fromStdString(icon_path.string())));
    install_theme_icons(icon_path);

    const fs::path icon_dest =
        home_directory() / ".local/share/icons/hicolor/256x256/apps/proton-inject.png";
    const fs::path desktop_path =
        home_directory() / ".local/share/applications/proton-inject.desktop";

    fs::create_directories(desktop_path.parent_path());

    const std::string exec_path = executable_path();
    std::ofstream desktop(desktop_path);
    if (!desktop || exec_path.empty()) {
        return;
    }

    desktop << "[Desktop Entry]\n"
            << "Type=Application\n"
            << "Name=Proton Inject\n"
            << "Comment=Inject DLLs into Proton/Steam games\n"
            << "Exec=\"" << exec_path << "\"\n"
            << "Icon=" << icon_dest.string() << "\n"
            << "Terminal=false\n"
            << "Categories=Game;Utility;\n"
            << "StartupWMClass=proton-inject\n";
}

}  // namespace proton_inject
