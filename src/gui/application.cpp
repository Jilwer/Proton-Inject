#include "gui/application.hpp"

#include "gui/app_icon.hpp"
#include "qt/main_window.hpp"

#include <QApplication>

namespace proton_inject {

int run_gui(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Proton Inject"));
    app.setDesktopFileName(QStringLiteral("proton-inject"));

    setup_app_icon();

    MainWindow window;
    window.show();

    return QApplication::exec();
}

}  // namespace proton_inject
