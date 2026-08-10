#include "qt/form.hpp"

#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace form {

Form::Form(QWidget* parent) : m_parent(parent) {
    m_layout = qobject_cast<QBoxLayout*>(parent->layout());
}

void Form::start_rows() {
    m_rows = new QFormLayout;
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setSpacing(kSmallSpacing);
    m_rows->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_layout->addLayout(m_rows);
}

void Form::add_group(const QString& title) {
    QLabel* heading = make_heading(title);
    if (m_rows != nullptr) {
        heading->setContentsMargins(0, kLargeSpacing, 0, 0);
    }
    m_layout->addWidget(heading);
    start_rows();
}

QLabel* Form::add(const QString& caption, QWidget* field) {
    if (m_rows == nullptr) {
        start_rows();
    }
    auto* label = new QLabel(caption, m_parent);
    label->setBuddy(field);
    m_rows->addRow(label, field);
    m_captions.push_back(label);
    return label;
}

void Form::add_wide(QWidget* field) {
    if (m_rows == nullptr) {
        start_rows();
    }
    m_rows->addRow(field);
}

void align_captions(const std::vector<QLabel*>& captions) {
    int width = 0;
    for (const QLabel* label : captions) {
        width = std::max(width, label->sizeHint().width());
    }
    for (QLabel* label : captions) {
        label->setFixedWidth(width);
    }
}

QWidget* make_row(std::initializer_list<QWidget*> widgets, bool expand_first) {
    auto* box = new QWidget;
    auto* layout = new QHBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kSmallSpacing);

    for (QWidget* widget : widgets) {
        layout->addWidget(widget);
    }
    if (expand_first && widgets.size() > 0) {
        layout->setStretch(0, 1);
    } else {
        layout->addStretch(1);
    }
    return box;
}

QWidget* make_page() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(kLargeSpacing, kLargeSpacing, kLargeSpacing, kLargeSpacing);
    layout->setSpacing(kLargeSpacing);
    return page;
}

QBoxLayout* page_layout(QWidget* page) {
    return qobject_cast<QBoxLayout*>(page->layout());
}

// the scroller keeps a page usable on short screens without the default layout scrolling.
QScrollArea* wrap_scrollable(QWidget* content) {
    auto* scroller = new QScrollArea;
    scroller->setWidget(content);
    scroller->setWidgetResizable(true);
    scroller->setFrameShape(QFrame::NoFrame);
    scroller->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroller->viewport()->setAutoFillBackground(false);
    content->setAutoFillBackground(false);
    return scroller;
}

QListWidget* make_list(int height) {
    auto* list = new QListWidget;
    list->setMinimumHeight(height);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setTextElideMode(Qt::ElideLeft);
    list->setUniformItemSizes(true);
    return list;
}

QLabel* make_heading(const QString& text) {
    auto* heading = new QLabel(text);
    QFont bold = heading->font();
    bold.setBold(true);
    heading->setFont(bold);
    return heading;
}

QPushButton* make_primary(const QString& text) {
    auto* button = new QPushButton(text);
    button->setDefault(true);
    return button;
}

}  // namespace form
