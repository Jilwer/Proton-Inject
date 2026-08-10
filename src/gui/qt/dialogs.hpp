#pragma once

#include <string>
#include <vector>

class QWidget;

namespace dialogs {

// pairs of (display name, pattern). A pattern without '*' is treated as a suffix.
using FileFilters = std::vector<std::pair<std::string, std::string>>;

// multi-select results are newline separated; an empty result means cancelled.
// `initial_folder` is a hint: it is ignored if the path does not exist.
[[nodiscard]] std::string open_file(QWidget* parent, const std::string& title,
                                    const FileFilters& filters, bool multiple = false,
                                    const std::string& initial_folder = {});
[[nodiscard]] std::string choose_directory(QWidget* parent, const std::string& title,
                                           const std::string& initial = {});
[[nodiscard]] std::string prompt_text(QWidget* parent, const std::string& title,
                                      const std::string& message, const std::string& initial = {});
[[nodiscard]] bool confirm(QWidget* parent, const std::string& title, const std::string& message,
                           bool destructive = false);
void alert(QWidget* parent, const std::string& title, const std::string& message);

}  // namespace dialogs
