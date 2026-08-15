#pragma once

#include <QProcess>
#include <QString>

#include <filesystem>
#include <string>

namespace gui_util {

inline bool path_exists(const std::string& path) {
    return !path.empty() && std::filesystem::exists(path);
}

// spawned as an argv rather than a shell line: game and profile paths routinely contain
// spaces.
inline bool open_path(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    return QProcess::startDetached(QStringLiteral("xdg-open"), {QString::fromStdString(path)});
}

}  // namespace gui_util
