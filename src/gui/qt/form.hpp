#pragma once

#include <QString>

#include <initializer_list>
#include <vector>

class QBoxLayout;
class QFormLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QScrollArea;
class QWidget;

// the layout vocabulary the pages are written in. Groups are separated by spacing and a
// heading rather than by frames, and rows go through QFormLayout so label alignment comes
// from the platform style instead of being hard-coded here.
namespace form {

// KDE HIG spacing scale, matching Kirigami's smallSpacing and largeSpacing.
inline constexpr int kSmallSpacing = 4;
inline constexpr int kLargeSpacing = 8;

class Form {
public:
    explicit Form(QWidget* parent);

    // increasing the gap above a heading is what separates one group from the last.
    void add_group(const QString& title);
    QLabel* add(const QString& caption, QWidget* field);
    void add_wide(QWidget* field);

    [[nodiscard]] const std::vector<QLabel*>& captions() const { return m_captions; }

private:
    void start_rows();

    QWidget* m_parent = nullptr;
    QBoxLayout* m_layout = nullptr;
    QFormLayout* m_rows = nullptr;
    std::vector<QLabel*> m_captions;
};

// gives every caption the widest one's width, so rows in a stacked page still line up with
// the rows around it.
void align_captions(const std::vector<QLabel*>& captions);

QWidget* make_row(std::initializer_list<QWidget*> widgets, bool expand_first = true);
QWidget* make_page();
QBoxLayout* page_layout(QWidget* page);
QScrollArea* wrap_scrollable(QWidget* content);
QListWidget* make_list(int height);
QLabel* make_heading(const QString& text);
QPushButton* make_primary(const QString& text);

}  // namespace form
