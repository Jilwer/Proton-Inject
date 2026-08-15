#include "qt/main_window.hpp"

#include "gui_util.hpp"
#include "mods_directory.hpp"
#include "qt/dialogs.hpp"
#include "qt/form.hpp"

#include "core/console_mode.hpp"
#include "core/method.hpp"
#include "proton/proton.hpp"
#include "utils/utils.hpp"
#include "version.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStatusBar>
#include <QIcon>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QClipboard>

#include <algorithm>
#include <array>
#include <filesystem>
#include <thread>
#include <utility>

namespace {

// the layout survives narrower than this, but the rows get cramped enough to be worth
// stopping at. The window opens here too: the hinted width is narrower still.
constexpr int kMinimumWidth = 500;
constexpr int kDelayFieldWidth = 80;
constexpr int kDllListHeight = 64;

// the injector and the loader take the canonical ids; the labels only say which one you get
// by default.
struct Choice {
    const char* id;
    const char* label;
};

constexpr std::array<Choice, 4> kMethods{
    {{"crt", "crt (default)"}, {"apc", "apc"}, {"nt", "nt"}, {"liatll", "liat+ll"}}};

constexpr std::array<Choice, 3> kConsoleModes{
    {{"alloc", "New window"}, {"attach", "Use existing"}, {"none", "Off"}}};

// an id the combo does not offer falls back to the first entry rather than an empty selection.
template<std::size_t N>
int index_of(const std::array<Choice, N>& choices, const std::string& id) {
    for (std::size_t i = 0; i < choices.size(); ++i) {
        if (id == choices[i].id) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

template<std::size_t N>
const char* id_at(const std::array<Choice, N>& choices, int index) {
    const auto clamped = static_cast<std::size_t>(std::max(0, index));
    return clamped < choices.size() ? choices[clamped].id : choices.front().id;
}

void fill_combo(QComboBox& combo, const auto& choices) {
    for (const Choice& choice : choices) {
        combo.addItem(QString::fromLatin1(choice.label));
    }
}

QString to_qt(const std::string& text) {
    return QString::fromStdString(text);
}

std::string from_qt(const QString& text) {
    return text.toStdString();
}

// directory a path already points into, if it still exists.
std::string existing_parent_of(const std::string& path) {
    const std::string trimmed = proton_inject::trim(path);
    if (trimmed.empty()) {
        return {};
    }
    const std::filesystem::path parent = std::filesystem::path(trimmed).parent_path();
    if (parent.empty() || !std::filesystem::is_directory(parent)) {
        return {};
    }
    return parent.string();
}

void set_items(QListWidget& list, const std::vector<std::string>& items) {
    list.clear();
    for (const std::string& item : items) {
        list.addItem(to_qt(item));
    }
}

std::vector<std::string> items_of(const QListWidget& list) {
    std::vector<std::string> items;
    items.reserve(static_cast<std::size_t>(list.count()));
    for (int i = 0; i < list.count(); ++i) {
        items.push_back(from_qt(list.item(i)->text()));
    }
    return items;
}

std::string selected_text(const QListWidget& list) {
    const auto* item = list.currentItem();
    return item == nullptr ? std::string{} : from_qt(item->text());
}

QLineEdit* make_entry(const QString& placeholder) {
    auto* entry = new QLineEdit;
    entry->setPlaceholderText(placeholder);
    return entry;
}

}  // namespace

MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("Proton Inject"));
    setMinimumWidth(kMinimumWidth);

    // must run before the game list is populated, and the failure dialog is deferred so it
    // has a mapped window to attach to.
    const bool steam_found = m_steam.detect_steam();

    if (auto store = proton_inject::ConfigStore::create(); store) {
        m_store = std::move(*store);
    } else {
        m_store_error = store.error();
    }

    setup_ui();
    refresh_profiles();
    refresh_history_combo();
    refresh_game_combo();
    sync_source_state();
    sync_method_state();
    sync_dll_input_state();
    sync_loader_console_state();
    update_resolved_proton();
    update_action_states();
    size_to_content();

    if (!steam_found) {
        const std::string message = m_steam.error_message();
        QTimer::singleShot(0, this,
                           [this, message]() { dialogs::alert(this, "Steam Not Found", message); });
    }
}

// a scroll area reports a small size hint, so the window would otherwise open with the
// inject page already scrolled. Size to the page once, then let it shrink again.
void MainWindow::size_to_content() {
    m_inject_scroller->setMinimumHeight(m_inject_scroller->widget()->sizeHint().height());
    adjustSize();
    m_inject_scroller->setMinimumHeight(0);
}

void MainWindow::setup_ui() {
    m_tabs = new QTabWidget;
    m_tabs->addTab(build_inject_page(), QIcon::fromTheme(QStringLiteral("media-playback-start")),
                   QStringLiteral("Inject"));
    m_tabs->addTab(build_profiles_page(), QIcon::fromTheme(QStringLiteral("bookmarks")),
                   QStringLiteral("Profiles"));
    m_tabs->addTab(build_loader_page(), QIcon::fromTheme(QStringLiteral("folder-download")),
                   QStringLiteral("Loader"));
    m_tabs->addTab(build_logs_page(), QIcon::fromTheme(QStringLiteral("text-x-generic")),
                   QStringLiteral("Logs"));
    m_tabs->addTab(build_about_page(), QIcon::fromTheme(QStringLiteral("help-about")),
                   QStringLiteral("About"));
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::on_tab_changed);

    setCentralWidget(m_tabs);

    m_status_label = new QLabel(QStringLiteral("Ready."));
    m_status_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(m_status_label, 1);
    statusBar()->setSizeGripEnabled(true);
}

void MainWindow::add_profile_group(form::Form& form) {
    form.add_group(QStringLiteral("Profile"));

    m_profile_combo = new QComboBox;
    m_profile_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_update_profile_btn = new QPushButton(QStringLiteral("Update"));
    m_update_profile_btn->setEnabled(false);
    auto* save_profile_btn = new QPushButton(QStringLiteral("Save As…"));
    form.add(QStringLiteral("Profile"),
             form::make_row({m_profile_combo, m_update_profile_btn, save_profile_btn}));

    m_history_combo = new QComboBox;
    m_history_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    form.add(QStringLiteral("Recent"), m_history_combo);

    connect(m_profile_combo, &QComboBox::currentIndexChanged, this,
            &MainWindow::on_profile_combo_changed);
    connect(save_profile_btn, &QPushButton::clicked, this, &MainWindow::save_current_as_profile);
    connect(m_update_profile_btn, &QPushButton::clicked, this,
            &MainWindow::update_selected_profile);
    connect(m_history_combo, &QComboBox::currentIndexChanged, this,
            &MainWindow::on_history_combo_changed);
}

// a browse button that drops the chosen directory into `entry`.
QWidget* MainWindow::make_directory_row(QLineEdit* entry, const QString& title) {
    auto* button = new QPushButton(QStringLiteral("Browse…"));
    connect(button, &QPushButton::clicked, this, [this, entry, title]() {
        const std::string chosen =
            dialogs::choose_directory(this, from_qt(title), from_qt(entry->text()));
        if (!chosen.empty()) {
            entry->setText(to_qt(chosen));
        }
    });
    return form::make_row({entry, button});
}

QWidget* MainWindow::build_steam_fields(std::vector<QLabel*>& captions) {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_game_combo = new QComboBox;
    m_game_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_browse_game_dir_btn = new QPushButton(QStringLiteral("Game Dir"));
    m_browse_game_dir_btn->setEnabled(false);

    m_app_id_entry = make_entry(QStringLiteral("e.g. 123456"));

    m_proton_label = new QLabel(QStringLiteral("(enter AppID or select a Steam game)"));
    m_proton_label->setTextInteractionFlags(Qt::TextSelectableByMouse);

    form::Form fields(page);
    fields.add(QStringLiteral("Steam game"), form::make_row({m_game_combo, m_browse_game_dir_btn}));
    fields.add(QStringLiteral("AppID"), m_app_id_entry);
    fields.add(QStringLiteral("Proton"), m_proton_label);
    layout->addStretch(1);

    captions.insert(captions.end(), fields.captions().begin(), fields.captions().end());

    connect(m_game_combo, &QComboBox::currentIndexChanged, this,
            &MainWindow::on_game_combo_changed);
    connect(m_browse_game_dir_btn, &QPushButton::clicked, this, &MainWindow::browse_game_dir);
    connect(m_app_id_entry, &QLineEdit::textChanged, this, [this]() {
        update_resolved_proton();
        update_action_states();
    });
    return page;
}

QWidget* MainWindow::build_non_steam_fields(std::vector<QLabel*>& captions) {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_proton_path_entry = make_entry(QStringLiteral("Proton directory"));

    // the injector creates the prefix when it is actually used; building a form must not
    // touch the filesystem.
    m_prefix_entry = new QLineEdit(to_qt(proton_inject::default_wine_prefix()));

    m_game_id_entry = make_entry(QStringLiteral("0"));

    form::Form fields(page);
    fields.add(QStringLiteral("Proton"),
               make_directory_row(m_proton_path_entry, QStringLiteral("Select Proton Directory")));
    fields.add(QStringLiteral("Prefix"),
               make_directory_row(m_prefix_entry, QStringLiteral("Select Wine Prefix")));
    fields.add(QStringLiteral("Game ID"), m_game_id_entry);
    layout->addStretch(1);

    captions.insert(captions.end(), fields.captions().begin(), fields.captions().end());

    connect(m_proton_path_entry, &QLineEdit::textChanged, this,
            [this]() { update_action_states(); });
    return page;
}

void MainWindow::add_game_group(form::Form& form, std::vector<QLabel*>& captions) {
    m_game_heading = form.add_group(QStringLiteral("Game"));

    m_source_combo = new QComboBox;
    m_source_combo->addItems({QStringLiteral("Steam"), QStringLiteral("Non-Steam (umu)")});
    m_source_combo->setToolTip(
        QStringLiteral("Non-Steam attaches through umu-run; launch the game yourself first."));
    m_source_row = form::make_row({m_source_combo}, false);
    m_source_caption = form.add(QStringLiteral("Source"), m_source_row);

    // Steam and non-Steam need different fields, but swapping the whole group in and out
    // loses the reader's place. A stack keeps the group and its height fixed.
    m_mode_stack = new QStackedWidget;
    m_mode_stack->addWidget(build_steam_fields(captions));
    m_mode_stack->addWidget(build_non_steam_fields(captions));
    m_mode_stack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    form.add_wide(m_mode_stack);

    connect(m_source_combo, &QComboBox::currentIndexChanged, this, [this]() {
        sync_source_state();
        update_action_states();
    });
}

void MainWindow::add_injection_group(form::Form& form) {
    form.add_group(QStringLiteral("Injection"));

    m_exe_entry = make_entry(QStringLiteral("Game.exe or full path"));
    auto* exe_browse = new QPushButton(QStringLiteral("Browse…"));
    connect(exe_browse, &QPushButton::clicked, this, [this]() {
        const std::string file = dialogs::open_file(this, "Select Game EXE",
                                                    {{"Executables", "exe"}, {"All Files", "*"}},
                                                    false, exe_browse_directory());
        if (!file.empty()) {
            m_exe_entry->setText(to_qt(file));
        }
    });
    form.add(QStringLiteral("Game .exe"), form::make_row({m_exe_entry, exe_browse}));

    m_method_combo = new QComboBox;
    fill_combo(*m_method_combo, kMethods);
    m_method_combo->setToolTip(
        QStringLiteral("crt, apc and nt run the Windows injector inside the prefix, so they need "
                       "the game's Steam or Proton details. liat+ll hooks a game IAT from Linux "
                       "and calls LoadLibrary; it needs nothing but the .exe name."));
    auto* delay_label = new QLabel(QStringLiteral("Delay (ms)"));
    m_sleep_entry = make_entry(QStringLiteral("0"));
    m_sleep_entry->setFixedWidth(kDelayFieldWidth);
    form.add(QStringLiteral("Method"),
             form::make_row({m_method_combo, delay_label, m_sleep_entry}, false));

    m_use_loader_check = new QCheckBox(QStringLiteral("Use embedded mod loader"));
    m_use_loader_check->setToolTip(
        QStringLiteral("Hot-load mods from the game's proton-inject-mods folder."));
    m_use_loader_check->setChecked(true);

    m_loader_console_label = new QLabel(QStringLiteral("Console"));
    m_loader_console_combo = new QComboBox;
    fill_combo(*m_loader_console_combo, kConsoleModes);
    m_loader_console_combo->setToolTip(QStringLiteral(
        "New window allocates a console for the game. Use existing writes into one the game "
        "already has, which is what you want when a mod loader such as BepInEx allocated it "
        "first. Off keeps the loader silent."));
    form.add(QStringLiteral("Mods"),
             form::make_row({m_use_loader_check, m_loader_console_label, m_loader_console_combo},
                            false));

    m_dll_list = form::make_list(kDllListHeight);
    m_dll_list->setMaximumHeight(kDllListHeight);

    auto* add_dll_btn = new QPushButton(QStringLiteral("Add…"));
    auto* remove_dll_btn = new QPushButton(QStringLiteral("Remove"));
    auto* dll_buttons = new QWidget;
    auto* dll_button_layout = new QVBoxLayout(dll_buttons);
    dll_button_layout->setContentsMargins(0, 0, 0, 0);
    dll_button_layout->setSpacing(form::kSmallSpacing);
    dll_button_layout->addWidget(add_dll_btn);
    dll_button_layout->addWidget(remove_dll_btn);
    dll_button_layout->addStretch(1);
    dll_buttons->setMaximumHeight(kDllListHeight);

    // buttons beside the list rather than under it, so the manual DLL field costs a single
    // form row.
    m_dll_fields = form::make_row({m_dll_list, dll_buttons});
    m_dll_caption = form.add(QStringLiteral("DLL files"), m_dll_fields);

    connect(m_method_combo, &QComboBox::currentIndexChanged, this, [this]() {
        sync_method_state();
        update_action_states();
    });
    connect(m_use_loader_check, &QCheckBox::toggled, this, [this]() {
        on_use_loader_toggled();
        update_action_states();
    });
    connect(m_exe_entry, &QLineEdit::textChanged, this, [this]() { update_action_states(); });
    connect(add_dll_btn, &QPushButton::clicked, this, [this]() {
        const std::string files = dialogs::open_file(
            this, "Select DLL(s)", {{"DLL Files", "dll"}, {"All Files", "*"}}, true);
        const std::vector<std::string> existing = items_of(*m_dll_list);
        for (const std::string& path : proton_inject::split(files, '\n')) {
            if (path.empty() ||
                std::find(existing.begin(), existing.end(), path) != existing.end()) {
                continue;
            }
            m_dll_list->addItem(to_qt(path));
        }
        update_action_states();
    });
    connect(remove_dll_btn, &QPushButton::clicked, this, [this]() {
        delete m_dll_list->takeItem(m_dll_list->currentRow());
        update_action_states();
    });
}

QWidget* MainWindow::build_inject_page() {
    auto* content = new QWidget;
    auto* content_layout = new QVBoxLayout(content);
    content_layout->setContentsMargins(0, 0, 0, 0);
    content_layout->setSpacing(form::kSmallSpacing);

    form::Form form(content);
    std::vector<QLabel*> stacked_captions;

    add_profile_group(form);
    add_game_group(form, stacked_captions);
    add_injection_group(form);

    // without this the trailing form layout absorbs the page's spare height and its last
    // row drifts away from its caption.
    content_layout->addStretch(1);

    std::vector<QLabel*> captions = form.captions();
    captions.insert(captions.end(), stacked_captions.begin(), stacked_captions.end());
    form::align_captions(captions);

    m_inject_btn = form::make_primary(QStringLiteral("Inject DLL"));
    connect(m_inject_btn, &QPushButton::clicked, this, &MainWindow::start_injection);

    m_inject_scroller = form::wrap_scrollable(content);

    auto* page = form::make_page();
    auto* layout = form::page_layout(page);
    layout->addWidget(m_inject_scroller, 1);
    layout->addWidget(m_inject_btn);
    return page;
}

QWidget* MainWindow::build_profiles_page() {
    auto* content = new QWidget;
    auto* content_layout = new QVBoxLayout(content);
    content_layout->setContentsMargins(0, 0, 0, 0);
    content_layout->setSpacing(form::kSmallSpacing);

    content_layout->addWidget(form::make_heading(QStringLiteral("Saved Profiles")));

    m_profile_list = form::make_list(120);
    content_layout->addWidget(m_profile_list, 1);

    auto* refresh_btn = new QPushButton(QStringLiteral("Refresh"));
    auto* load_btn = new QPushButton(QStringLiteral("Load"));
    auto* delete_btn = new QPushButton(QStringLiteral("Delete"));
    auto* open_config_btn = new QPushButton(QStringLiteral("Config Directory"));
    content_layout->addWidget(
        form::make_row({refresh_btn, load_btn, delete_btn, open_config_btn}, false));

    form::Form create(content);
    create.add_group(QStringLiteral("Create New Profile"));

    m_new_profile_name = make_entry(QStringLiteral("Profile name"));
    create.add(QStringLiteral("Name"), m_new_profile_name);

    m_new_app_id = make_entry(QStringLiteral("Steam AppID (optional)"));
    create.add(QStringLiteral("AppID"), m_new_app_id);

    const auto file_row = [this](QLineEdit* entry, const QString& title,
                                 const dialogs::FileFilters& filters) {
        auto* button = new QPushButton(QStringLiteral("Browse…"));
        connect(button, &QPushButton::clicked, this, [this, entry, title, filters]() {
            std::string start = existing_parent_of(from_qt(entry->text()));
            if (start.empty()) {
                start =
                    m_steam.install_dir_for_app(proton_inject::trim(from_qt(m_new_app_id->text())));
            }
            const std::string chosen =
                dialogs::open_file(this, from_qt(title), filters, false, start);
            if (!chosen.empty()) {
                entry->setText(to_qt(chosen));
            }
        });
        return form::make_row({entry, button});
    };

    m_new_exe = make_entry(QStringLiteral("Game .exe path"));
    create.add(QStringLiteral("Game .exe"), file_row(m_new_exe, QStringLiteral("Select Game EXE"),
                                                     {{"Executables", "exe"}, {"All Files", "*"}}));

    m_new_use_loader = new QCheckBox(QStringLiteral("Use embedded mod loader"));
    m_new_use_loader->setChecked(true);
    create.add(QStringLiteral("Mods"), form::make_row({m_new_use_loader}, false));

    m_new_dll = make_entry(QStringLiteral("DLL path (leave empty when using mod loader)"));
    m_new_dll->setEnabled(false);
    create.add(QStringLiteral("DLL"), file_row(m_new_dll, QStringLiteral("Select DLL"),
                                               {{"DLL Files", "dll"}, {"All Files", "*"}}));

    form::align_captions(create.captions());

    auto* create_profile_btn = form::make_primary(QStringLiteral("Create Profile"));

    connect(refresh_btn, &QPushButton::clicked, this, &MainWindow::refresh_profiles);
    connect(load_btn, &QPushButton::clicked, this, [this]() {
        const std::string name = selected_text(*m_profile_list);
        if (!name.empty()) {
            load_profile_by_name(name);
        }
    });
    connect(delete_btn, &QPushButton::clicked, this, &MainWindow::delete_selected_profile);
    connect(open_config_btn, &QPushButton::clicked, this, &MainWindow::open_config_directory);
    connect(create_profile_btn, &QPushButton::clicked, this, &MainWindow::create_profile_from_tab);
    connect(m_new_use_loader, &QCheckBox::toggled, this,
            [this](bool checked) { m_new_dll->setEnabled(!checked); });
    connect(m_profile_list, &QListWidget::itemDoubleClicked, this,
            [this]() { load_profile_by_name(selected_text(*m_profile_list)); });

    auto* page = form::make_page();
    auto* layout = form::page_layout(page);
    layout->addWidget(form::wrap_scrollable(content), 1);
    layout->addWidget(create_profile_btn);
    return page;
}

QWidget* MainWindow::build_loader_page() {
    auto* page = form::make_page();
    auto* layout = form::page_layout(page);

    layout->addWidget(form::make_heading(QStringLiteral("Mods Directory")));

    auto* description = new QLabel(
        QStringLiteral("Scanned from Steam libraries for the current AppID, or from the Wine "
                       "prefix for non-Steam games. Adding a mod creates the folder if the "
                       "loader has not yet."));
    description->setWordWrap(true);
    description->setEnabled(false);
    layout->addWidget(description);

    m_loader_path_label = new QLabel(
        QStringLiteral("Set AppID (or pick Non-Steam with a prefix) on the Inject tab, then "
                       "click Refresh."));
    m_loader_path_label->setWordWrap(true);
    m_loader_path_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_loader_path_label);

    auto* refresh_btn = new QPushButton(QStringLiteral("Refresh"));
    m_add_mod_btn = new QPushButton(QStringLiteral("Add…"));
    m_add_mod_btn->setEnabled(false);
    m_remove_mod_btn = new QPushButton(QStringLiteral("Remove"));
    m_remove_mod_btn->setEnabled(false);
    m_open_mods_btn = new QPushButton(QStringLiteral("Open Mods Directory"));
    m_open_mods_btn->setEnabled(false);
    layout->addWidget(
        form::make_row({refresh_btn, m_add_mod_btn, m_remove_mod_btn, m_open_mods_btn}, false));

    layout->addWidget(form::make_heading(QStringLiteral("DLL Files in Mods Directory")));

    m_loader_mods_list = form::make_list(90);
    layout->addWidget(m_loader_mods_list, 1);

    connect(refresh_btn, &QPushButton::clicked, this, &MainWindow::refresh_loader_mods);
    connect(m_add_mod_btn, &QPushButton::clicked, this, &MainWindow::add_loader_mod);
    connect(m_remove_mod_btn, &QPushButton::clicked, this, &MainWindow::remove_loader_mod);
    connect(m_open_mods_btn, &QPushButton::clicked, this, &MainWindow::open_mods_directory);

    return page;
}

QWidget* MainWindow::build_logs_page() {
    auto* page = form::make_page();
    auto* layout = form::page_layout(page);

    auto* copy_btn = new QPushButton(QStringLiteral("Copy Log"));
    auto* clear_btn = new QPushButton(QStringLiteral("Clear"));
    layout->addWidget(form::make_row({copy_btn, clear_btn}, false));

    m_log_view = new QPlainTextEdit;
    m_log_view->setReadOnly(true);
    m_log_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono = m_log_view->font();
    mono.setStyleHint(QFont::Monospace);
    mono.setFamily(QStringLiteral("monospace"));
    m_log_view->setFont(mono);
    layout->addWidget(m_log_view, 1);

    connect(copy_btn, &QPushButton::clicked, this, &MainWindow::copy_logs);
    connect(clear_btn, &QPushButton::clicked, this, &MainWindow::clear_logs);

    return page;
}

QWidget* MainWindow::build_about_page() {
    auto* page = form::make_page();
    auto* layout = form::page_layout(page);

    layout->addWidget(form::make_heading(QStringLiteral("Proton Inject")));

    auto* version =
        new QLabel(QStringLiteral("Version %1").arg(QString::fromLatin1(proton_inject::kVersion)));
    version->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(version);

    auto* summary =
        new QLabel(QStringLiteral("DLL injector for Proton games, built with C++ and Qt 6."));
    summary->setWordWrap(true);
    layout->addWidget(summary);

    // rich-text labels so the project, license, and credit links stay clickable
    auto* project = new QLabel(QStringLiteral(
        "<a href=\"https://github.com/Jilwer/Proton-Inject\">github.com/Jilwer/Proton-Inject</a>"));
    project->setOpenExternalLinks(true);
    project->setTextInteractionFlags(Qt::TextBrowserInteraction);
    layout->addWidget(project);

    auto* license = new QLabel(QStringLiteral(
        "Licensed under the <a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GNU GPLv3</a>."));
    license->setOpenExternalLinks(true);
    license->setTextInteractionFlags(Qt::TextBrowserInteraction);
    layout->addWidget(license);

    auto* rule = new QFrame;
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    layout->addWidget(rule);

    layout->addWidget(form::make_heading(QStringLiteral("Third-party libraries")));

    auto* credits = new QLabel(
        QStringLiteral("<a href=\"https://github.com/CLIUtils/CLI11\">CLI11</a> (BSD-3-Clause)<br>"
                       "<a href=\"https://github.com/nlohmann/json\">nlohmann/json</a> (MIT)"));
    credits->setOpenExternalLinks(true);
    credits->setTextInteractionFlags(Qt::TextBrowserInteraction);
    layout->addWidget(credits);

    layout->addStretch(1);
    return page;
}

void MainWindow::refresh_profiles() {
    std::vector<std::string> profiles;
    if (m_store.has_value()) {
        if (auto listed = m_store->list_profiles(); listed) {
            profiles = std::move(*listed);
        } else {
            set_status(listed.error());
        }
    }
    const QString previous = m_profile_combo->currentText();

    {
        const QSignalBlocker blocker(m_profile_combo);
        m_profile_combo->clear();
        m_profile_combo->addItem(QStringLiteral("None"));
        for (const std::string& name : profiles) {
            m_profile_combo->addItem(to_qt(name));
        }
        const int index = m_profile_combo->findText(previous);
        m_profile_combo->setCurrentIndex(index < 0 ? 0 : index);
    }

    if (m_profile_list != nullptr) {
        set_items(*m_profile_list, profiles);
    }
}

void MainWindow::on_profile_combo_changed(int index) {
    if (index <= 0) {
        m_selected_profile.clear();
        m_update_profile_btn->setEnabled(false);
        return;
    }
    load_profile_by_name(from_qt(m_profile_combo->itemText(index)));
}

const proton_inject::ConfigStore* MainWindow::store_or_report() {
    if (!m_store.has_value()) {
        dialogs::alert(this, "Profiles Unavailable", m_store_error);
        return nullptr;
    }
    return &*m_store;
}

void MainWindow::load_profile_by_name(const std::string& name) {
    const auto* store = store_or_report();
    if (store == nullptr) {
        return;
    }

    const auto loaded = store->load_profile(name);
    if (!loaded) {
        dialogs::alert(this, "Profile Error", loaded.error());
        return;
    }

    apply_config(from_app_config(*loaded));
    m_selected_profile = name;

    {
        const QSignalBlocker blocker(m_profile_combo);
        const int index = m_profile_combo->findText(to_qt(name));
        if (index >= 0) {
            m_profile_combo->setCurrentIndex(index);
        }
    }

    m_update_profile_btn->setEnabled(true);
    set_status("Loaded profile: " + name);
    m_tabs->setCurrentIndex(0);
}

void MainWindow::apply_config(const InjectionConfig& config) {
    {
        const QSignalBlocker blocker(m_source_combo);
        m_source_combo->setCurrentIndex(config.mode == InjectionMode::NonSteam ? 1 : 0);
    }
    m_app_id_entry->setText(to_qt(config.app_id));
    m_exe_entry->setText(to_qt(config.exe_path));
    m_use_loader_check->setChecked(config.use_loader);
    m_loader_console_combo->setCurrentIndex(
        index_of(kConsoleModes, proton_inject::normalize_console_mode(config.loader_console)));

    m_dll_list->clear();
    if (!config.use_loader) {
        set_items(*m_dll_list, config.dll_paths);
    }

    m_method_combo->setCurrentIndex(
        index_of(kMethods, proton_inject::normalize_method(config.method)));
    m_sleep_entry->setText(config.sleep_ms > 0 ? to_qt(std::to_string(config.sleep_ms))
                                               : QString{});

    m_prefix_entry->setText(to_qt(config.wine_prefix.empty() ? proton_inject::default_wine_prefix()
                                                             : config.wine_prefix));
    m_game_id_entry->setText(to_qt(config.game_id));
    m_proton_path_entry->setText(
        to_qt(InjectRunner::resolve_proton_executable(config.proton_path)));

    if (!config.app_id.empty()) {
        for (std::size_t i = 0; i < m_games.size(); ++i) {
            if (m_games[i].app_id == config.app_id) {
                const QSignalBlocker blocker(m_game_combo);
                m_game_combo->setCurrentIndex(static_cast<int>(i) + 1);
                break;
            }
        }
    }

    sync_source_state();
    sync_method_state();
    sync_dll_input_state();
    sync_loader_console_state();
    sync_game_dir_button();
    update_resolved_proton();
    update_action_states();
}

InjectionConfig MainWindow::collect_config() const {
    InjectionConfig config;
    config.mode = is_non_steam() ? InjectionMode::NonSteam : InjectionMode::Steam;
    config.app_id = proton_inject::trim(from_qt(m_app_id_entry->text()));
    config.game_name = game_name_for(config.app_id);
    config.exe_path = proton_inject::trim(from_qt(m_exe_entry->text()));
    config.use_loader = m_use_loader_check->isChecked();
    config.loader_console = id_at(kConsoleModes, m_loader_console_combo->currentIndex());
    config.method = selected_method();
    config.sleep_ms = sleep_ms_value();
    config.wine_prefix = proton_inject::trim(from_qt(m_prefix_entry->text()));
    config.game_id = proton_inject::trim(from_qt(m_game_id_entry->text()));
    config.proton_path = selected_proton_path();

    if (!config.use_loader) {
        config.dll_paths = items_of(*m_dll_list);
    }

    return config;
}

std::expected<void, std::string> MainWindow::validate_config() const {
    const InjectionConfig config = collect_config();
    // both modes attach to an already-running game; they differ only in the launcher backend
    // (Steam's runinprefix vs umu-run) and where the prefix comes from.
    const bool steam = config.mode == InjectionMode::Steam;

    if (config.exe_path.empty()) {
        return std::unexpected("Please enter a game executable name or path.");
    }

    // Linux IAT methods find the pid from the basename alone, so the launcher-specific checks
    // below do not apply.
    if (!is_linux_iat_method()) {
        if (steam && config.app_id.empty()) {
            return std::unexpected("AppID is required for Steam games (or pick Non-Steam).");
        }

        // a bare name is a process to attach to; anything else has to resolve to a real file.
        const bool needs_real_path = !InjectRunner::is_bare_exe_name(config.exe_path);
        if (needs_real_path && !gui_util::path_exists(config.exe_path) &&
            InjectRunner::resolve_launch_target(config.app_id, config.exe_path).empty()) {
            return std::unexpected(
                "Enter a process name (e.g. Game.exe) or a valid executable path.");
        }

        if (!steam && proton_inject::find_in_path("umu-run").empty()) {
            return std::unexpected("umu-run is required for non-Steam games.");
        }

        if (!InjectRunner::has_valid_proton_path(config.proton_path)) {
            if (!steam) {
                return std::unexpected("Please select a valid Proton installation.");
            }
            // the Proton row already spells out why the AppID did not resolve.
            const std::string reason = from_qt(m_proton_label->text());
            return std::unexpected(reason.empty() ? "Could not resolve Proton for this AppID."
                                                  : reason);
        }
    }

    if (!config.use_loader) {
        if (config.dll_paths.empty()) {
            return std::unexpected("Add at least one DLL, or enable the embedded mod loader.");
        }
        for (const std::string& dll : config.dll_paths) {
            if (!gui_util::path_exists(dll)) {
                return std::unexpected("DLL not found: " + dll);
            }
        }
    }

    return {};
}

void MainWindow::save_current_as_profile() {
    const auto* store = store_or_report();
    if (store == nullptr) {
        return;
    }

    const auto valid = validate_config();
    if (!valid) {
        set_status(valid.error());
        return;
    }

    const std::string name =
        proton_inject::trim(dialogs::prompt_text(this, "Save as Profile", "Profile name:"));
    if (name.empty()) {
        return;
    }

    if (store->profile_exists(name) &&
        !dialogs::confirm(this, "Overwrite Profile",
                          "Profile \"" + name + "\" already exists. Overwrite?", true)) {
        return;
    }

    if (const auto saved = store->save_profile(name, to_app_config(collect_config())); !saved) {
        dialogs::alert(this, "Save Failed", saved.error());
        return;
    }
    set_status("Saved profile: " + name);
    refresh_profiles();
    load_profile_by_name(name);
}

void MainWindow::update_selected_profile() {
    const auto* store = store_or_report();
    if (store == nullptr) {
        return;
    }
    if (m_selected_profile.empty()) {
        set_status("Select a profile to update.");
        return;
    }

    const auto valid = validate_config();
    if (!valid) {
        set_status(valid.error());
        return;
    }

    if (const auto saved = store->save_profile(m_selected_profile, to_app_config(collect_config()));
        !saved) {
        dialogs::alert(this, "Update Failed", saved.error());
        return;
    }
    set_status("Updated profile: " + m_selected_profile);
}

void MainWindow::refresh_history_combo() {
    const int previous = m_history_combo->currentIndex();
    const QSignalBlocker blocker(m_history_combo);

    m_history_combo->clear();
    m_history_combo->addItem(QStringLiteral("None"));
    for (const HistoryEntry& entry : m_history.entries()) {
        m_history_combo->addItem(to_qt(entry.label));
    }
    m_history_combo->setCurrentIndex(previous < m_history_combo->count() ? std::max(previous, 0)
                                                                         : 0);
}

void MainWindow::on_history_combo_changed(int index) {
    if (index <= 0) {
        return;
    }

    auto loaded = m_history.entry_at(static_cast<std::size_t>(index - 1));
    if (!loaded.has_value()) {
        return;
    }

    loaded->proton_path = InjectRunner::resolve_proton_executable(loaded->proton_path);
    apply_config(*loaded);
    set_status("Loaded recent injection.");
}

void MainWindow::refresh_game_combo() {
    m_games = m_steam.installed_games();

    const QSignalBlocker blocker(m_game_combo);
    m_game_combo->clear();
    m_game_combo->addItem(QStringLiteral("(manual AppID entry)"));
    for (const SteamGame& game : m_games) {
        m_game_combo->addItem(to_qt(game.name + " (" + game.app_id + ")"));
    }
    sync_game_dir_button();
}

void MainWindow::on_game_combo_changed(int index) {
    if (index > 0 && static_cast<std::size_t>(index - 1) < m_games.size()) {
        m_app_id_entry->setText(to_qt(m_games[static_cast<std::size_t>(index - 1)].app_id));
    }
    sync_game_dir_button();
}

void MainWindow::browse_game_dir() {
    const std::string dir = steam_game_install_dir();
    if (dir.empty()) {
        set_status("Could not find the install directory for this game.");
        return;
    }

    if (gui_util::open_path(dir)) {
        show_toast("Opened " + dir);
    } else {
        set_status("Failed to open game directory.");
    }
}

std::string MainWindow::game_name_for(const std::string& app_id) const {
    const auto match = std::ranges::find(m_games, app_id, &SteamGame::app_id);
    return match == m_games.end() ? std::string{} : match->name;
}

std::string MainWindow::exe_browse_directory() const {
    // wherever the field already points wins, so re-browsing lands where you left off.
    if (const std::string current = existing_parent_of(from_qt(m_exe_entry->text()));
        !current.empty()) {
        return current;
    }

    if (const std::string install_dir = steam_game_install_dir(); !install_dir.empty()) {
        return install_dir;
    }

    // non-Steam titles live inside the prefix rather than a Steam library.
    if (is_non_steam()) {
        const std::string drive_c =
            proton_inject::trim(from_qt(m_prefix_entry->text())) + "/drive_c";
        if (gui_util::path_exists(drive_c)) {
            return drive_c;
        }
    }

    return {};
}

std::string MainWindow::steam_game_install_dir() const {
    if (is_non_steam()) {
        return {};
    }

    const int index = m_game_combo->currentIndex();
    const std::string app_id = index > 0 && static_cast<std::size_t>(index - 1) < m_games.size()
                                   ? m_games[static_cast<std::size_t>(index - 1)].app_id
                                   : proton_inject::trim(from_qt(m_app_id_entry->text()));

    return app_id.empty() ? std::string{} : m_steam.install_dir_for_app(app_id);
}

void MainWindow::sync_game_dir_button() {
    m_browse_game_dir_btn->setEnabled(!is_non_steam() && m_game_combo->currentIndex() > 0);
}

void MainWindow::update_resolved_proton() {
    // the label lives on the Steam page of the mode stack, and is recomputed on the way
    // back, so there is nothing to update here.
    if (is_non_steam()) {
        m_resolved_proton_path.clear();
        return;
    }

    const std::string app_id = proton_inject::trim(from_qt(m_app_id_entry->text()));
    if (app_id.empty()) {
        m_resolved_proton_path.clear();
        m_proton_label->setText(QStringLiteral("(enter AppID or select a Steam game)"));
        return;
    }

    const auto install = proton_inject::resolve_proton(app_id);
    if (install) {
        m_resolved_proton_path = install->script_path();
        m_proton_label->setText(to_qt(install->name + " (from Steam settings)"));
    } else {
        m_resolved_proton_path.clear();
        m_proton_label->setText(to_qt(install.error()));
    }
}

bool MainWindow::is_non_steam() const {
    return m_source_combo->currentIndex() == 1;
}

std::string MainWindow::selected_method() const {
    return id_at(kMethods, m_method_combo->currentIndex());
}

// Linux IAT methods attach to the game by pid and map the DLL in from Linux, so they never go
// through Steam or Proton and need no AppID, prefix or Proton build.
bool MainWindow::is_linux_iat_method() const {
    return proton_inject::is_linux_iat_method(selected_method());
}

std::string MainWindow::selected_proton_path() const {
    if (is_non_steam()) {
        return proton_inject::trim(from_qt(m_proton_path_entry->text()));
    }
    return m_resolved_proton_path;
}

void MainWindow::sync_source_state() {
    m_mode_stack->setCurrentIndex(is_non_steam() ? 1 : 0);
    sync_game_dir_button();
    update_resolved_proton();
}

// the Game group only feeds the prefix-side injector, so hide it for Linux IAT methods, which
// attach by pid and read none of it.
void MainWindow::sync_method_state() {
    const bool linux_iat = is_linux_iat_method();
    m_game_heading->setVisible(!linux_iat);
    m_source_caption->setVisible(!linux_iat);
    m_source_row->setVisible(!linux_iat);
    m_mode_stack->setVisible(!linux_iat);
    m_exe_entry->setPlaceholderText(linux_iat ? QStringLiteral("Game.exe")
                                              : QStringLiteral("Game.exe or full path"));
}

void MainWindow::on_use_loader_toggled() {
    sync_dll_input_state();
    sync_loader_console_state();
    refresh_loader_mods();
}

// dimmed rather than hidden, so the loader toggle explains what it replaces instead of
// making a row vanish from under the pointer.
void MainWindow::sync_dll_input_state() {
    const bool manual = !m_use_loader_check->isChecked();
    m_dll_caption->setEnabled(manual);
    m_dll_fields->setEnabled(manual);
}

// only the embedded loader has a console to talk about; an arbitrary DLL brings its own.
void MainWindow::sync_loader_console_state() {
    const bool loader = m_use_loader_check->isChecked();
    m_loader_console_label->setEnabled(loader);
    m_loader_console_combo->setEnabled(loader);
}

void MainWindow::start_injection() {
    const auto valid = validate_config();
    if (!valid) {
        set_status(valid.error());
        return;
    }

    const InjectionConfig config = collect_config();

    m_log_view->clear();
    m_inject_btn->setEnabled(false);

    set_status("Attaching to " + InjectRunner::process_name_from_exe(config.exe_path) + "...");

    std::thread([this, config]() {
        const bool ok = m_runner.run(config, [this](const std::string& line) {
            QMetaObject::invokeMethod(
                this, [this, line]() { append_log(line); }, Qt::QueuedConnection);
        });

        QMetaObject::invokeMethod(
            this,
            [this, config, ok]() {
                m_inject_btn->setEnabled(true);
                if (ok) {
                    m_history.save(config);
                    refresh_history_combo();
                    set_status("Injection successful.");
                    show_toast("Injection successful.");
                } else {
                    set_status("Injection failed: " + m_runner.last_error());
                }
                update_action_states();
            },
            Qt::QueuedConnection);
    }).detach();
}

std::string MainWindow::mods_directory() const {
    const InjectionConfig config = collect_config();
    if (config.mode == InjectionMode::NonSteam) {
        return ModsDirectory::for_prefix(config.wine_prefix);
    }
    return config.app_id.empty() ? std::string{} : ModsDirectory::for_app_id(config.app_id);
}

void MainWindow::refresh_loader_mods() {
    m_loader_mods_list->clear();

    const std::string mods_dir = mods_directory();
    // the folder only appears once the loader or an added mod puts something in it, so the
    // path is known well before it exists.
    const bool created = gui_util::path_exists(mods_dir);
    m_add_mod_btn->setEnabled(!mods_dir.empty());
    m_open_mods_btn->setEnabled(created);

    const std::vector<std::string> mods =
        created ? ModsDirectory::list_dlls(mods_dir) : std::vector<std::string>{};
    m_remove_mod_btn->setEnabled(!mods.empty());
    set_items(*m_loader_mods_list,
              mods.empty() ? std::vector<std::string>{"(no DLL files found)"} : mods);

    if (mods_dir.empty()) {
        m_loader_path_label->setText(
            QStringLiteral("No prefix for this game yet.\n"
                           "Run the game once (or inject) to create it, then click Refresh."));
    } else {
        m_loader_path_label->setText(
            to_qt(created ? mods_dir : mods_dir + "\n(created when you add the first mod)"));
    }
}

void MainWindow::add_loader_mod() {
    const std::string mods_dir = mods_directory();
    if (mods_dir.empty()) {
        set_status("No prefix for this game yet. Run the game once, then click Refresh.");
        return;
    }

    const std::string files = dialogs::open_file(this, "Select Mod DLL(s)",
                                                 {{"DLL Files", "dll"}, {"All Files", "*"}}, true);

    int added = 0;
    for (const std::string& path : proton_inject::split(files, '\n')) {
        if (const auto copied = ModsDirectory::add(mods_dir, path); !copied) {
            dialogs::alert(this, "Add Mod Failed", copied.error());
            break;
        }
        ++added;
    }

    if (added > 0) {
        show_toast("Added " + std::to_string(added) + (added == 1 ? " mod." : " mods."));
    }
    refresh_loader_mods();
}

void MainWindow::remove_loader_mod() {
    const std::string name = selected_text(*m_loader_mods_list);
    const std::string mods_dir = mods_directory();
    if (name.empty() || mods_dir.empty()) {
        set_status("Select a mod to remove.");
        return;
    }

    if (!dialogs::confirm(this, "Remove Mod", "Delete \"" + name + "\" from the mods folder?",
                          true)) {
        return;
    }

    if (const auto removed = ModsDirectory::remove(mods_dir, name); !removed) {
        dialogs::alert(this, "Remove Failed", removed.error());
    } else {
        set_status("Removed mod: " + name);
    }
    refresh_loader_mods();
}

void MainWindow::open_mods_directory() {
    const std::string mods_dir = mods_directory();
    if (!gui_util::path_exists(mods_dir)) {
        set_status("No mods directory yet. Add a mod or inject the loader once, then try again.");
        return;
    }

    if (gui_util::open_path(mods_dir)) {
        show_toast("Opened " + mods_dir);
    } else {
        set_status("Failed to open mods directory.");
    }
}

void MainWindow::copy_logs() {
    const QString text = m_log_view->toPlainText();
    if (text.isEmpty()) {
        set_status("Nothing to copy.");
        return;
    }

    QGuiApplication::clipboard()->setText(text);
    show_toast("Log copied to clipboard.");
}

void MainWindow::clear_logs() {
    m_log_view->clear();
}

void MainWindow::on_tab_changed(int index) {
    if (index == 1) {
        refresh_profiles();
    } else if (index == 2) {
        refresh_loader_mods();
    }
}

void MainWindow::create_profile_from_tab() {
    const auto* store = store_or_report();
    if (store == nullptr) {
        return;
    }

    InjectionConfig config;
    config.exe_path = proton_inject::trim(from_qt(m_new_exe->text()));
    config.app_id = proton_inject::trim(from_qt(m_new_app_id->text()));
    config.use_loader = m_new_use_loader->isChecked();
    if (!config.use_loader) {
        const std::string dll = proton_inject::trim(from_qt(m_new_dll->text()));
        if (!dll.empty()) {
            config.dll_paths = {dll};
        }
    }

    const std::string name = proton_inject::trim(from_qt(m_new_profile_name->text()));
    if (const auto created = store->create_profile(name, to_app_config(config)); !created) {
        dialogs::alert(this, "Create Failed", created.error());
        return;
    }

    m_new_profile_name->clear();
    m_new_app_id->clear();
    m_new_exe->clear();
    m_new_dll->clear();
    set_status("Profile '" + name + "' created");
    refresh_profiles();
}

void MainWindow::delete_selected_profile() {
    const auto* store = store_or_report();
    if (store == nullptr) {
        return;
    }

    const std::string name = selected_text(*m_profile_list);
    if (name.empty()) {
        return;
    }

    if (!dialogs::confirm(this, "Delete Profile", "Delete profile \"" + name + "\"?", true)) {
        return;
    }

    if (const auto removed = store->delete_profile(name); !removed) {
        dialogs::alert(this, "Delete Failed", removed.error());
        return;
    }

    if (m_selected_profile == name) {
        m_selected_profile.clear();
        m_update_profile_btn->setEnabled(false);
        const QSignalBlocker blocker(m_profile_combo);
        m_profile_combo->setCurrentIndex(0);
    }

    set_status("Deleted profile: " + name);
    refresh_profiles();
}

void MainWindow::open_config_directory() {
    const auto* store = store_or_report();
    if (store == nullptr) {
        return;
    }

    const std::string& dir = store->config_dir();
    if (gui_util::open_path(dir)) {
        show_toast("Opened " + dir);
    } else {
        set_status("Failed to open config directory.");
    }
}

void MainWindow::set_status(const std::string& text) {
    m_status_label->setText(to_qt(text));
    m_status_label->setToolTip(to_qt(text));
}

void MainWindow::show_toast(const std::string& message) {
    statusBar()->showMessage(to_qt(message), 4000);
}

void MainWindow::update_action_states() {
    if (m_inject_btn != nullptr) {
        m_inject_btn->setEnabled(validate_config().has_value());
    }
}

int MainWindow::sleep_ms_value() const {
    bool numeric = false;
    const int value = m_sleep_entry->text().trimmed().toInt(&numeric);
    // non-numeric text means no delay rather than a failed injection.
    return numeric ? std::max(0, value) : 0;
}

void MainWindow::append_log(const std::string& line) {
    m_log_view->appendPlainText(to_qt(line));
}
