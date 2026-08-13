#include "gui/app_icon.hpp"

#include "install/installer.hpp"

#include <QApplication>
#include <QIcon>
#include <QString>

#include <filesystem>

namespace fs = std::filesystem;

namespace proton_inject {

void setup_app_icon() {
    // a portable binary carries its icon beside it; an installed one uses the copy --install
    // wrote. Both are loaded by path: asking the theme for "proton-inject" hands back
    // whatever it ships for "proton", since lookup strips at the last hyphen.
    for (const fs::path& candidate : {bundled_icon_path(), installed_icon_path()}) {
        if (fs::exists(candidate)) {
            QApplication::setWindowIcon(QIcon(QString::fromStdString(candidate.string())));
            return;
        }
    }
}

}  // namespace proton_inject
