#include "qt/dialogs.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QStringList>

namespace {

QString to_qt(const std::string& text) {
    return QString::fromStdString(text);
}

QString filter_string(const dialogs::FileFilters& filters) {
    QStringList parts;
    for (const auto& [name, pattern] : filters) {
        const QString glob = pattern.find('*') == std::string::npos
                                 ? QStringLiteral("*.") + to_qt(pattern)
                                 : to_qt(pattern);
        parts << QStringLiteral("%1 (%2)").arg(to_qt(name), glob);
    }
    return parts.join(QStringLiteral(";;"));
}

QString usable_directory(const std::string& path) {
    const QString candidate = to_qt(path);
    return QFileInfo(candidate).isDir() ? candidate : QString{};
}

}  // namespace

namespace dialogs {

std::string open_file(QWidget* parent, const std::string& title, const FileFilters& filters,
                      bool multiple, const std::string& initial_folder) {
    const QString start = usable_directory(initial_folder);
    const QString glob = filter_string(filters);

    if (!multiple) {
        return QFileDialog::getOpenFileName(parent, to_qt(title), start, glob).toStdString();
    }

    const QStringList chosen = QFileDialog::getOpenFileNames(parent, to_qt(title), start, glob);
    return chosen.join(QStringLiteral("\n")).toStdString();
}

std::string choose_directory(QWidget* parent, const std::string& title,
                             const std::string& initial) {
    return QFileDialog::getExistingDirectory(parent, to_qt(title), usable_directory(initial))
        .toStdString();
}

std::string prompt_text(QWidget* parent, const std::string& title, const std::string& message,
                        const std::string& initial) {
    bool accepted = false;
    const QString value = QInputDialog::getText(parent, to_qt(title), to_qt(message),
                                                QLineEdit::Normal, to_qt(initial), &accepted);
    return accepted ? value.toStdString() : std::string{};
}

bool confirm(QWidget* parent, const std::string& title, const std::string& message,
             bool destructive) {
    QMessageBox box(parent);
    box.setWindowTitle(to_qt(title));
    box.setText(to_qt(message));
    box.setIcon(destructive ? QMessageBox::Warning : QMessageBox::Question);

    QPushButton* accept = box.addButton(QMessageBox::Ok);
    QPushButton* cancel = box.addButton(QMessageBox::Cancel);
    // a destructive action defaults to cancel, so a stray Enter cannot delete anything.
    box.setDefaultButton(destructive ? cancel : accept);

    box.exec();
    return box.clickedButton() == accept;
}

void alert(QWidget* parent, const std::string& title, const std::string& message) {
    QMessageBox::warning(parent, to_qt(title), to_qt(message));
}

}  // namespace dialogs
