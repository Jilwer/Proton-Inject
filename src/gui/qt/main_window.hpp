#pragma once

#include "history_manager.hpp"
#include "inject_runner.hpp"
#include "injection_config.hpp"
#include "profile_manager.hpp"
#include "steam_service.hpp"

#include "qt/form.hpp"

#include <QMainWindow>

#include <expected>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTabWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

private:
    QWidget* build_inject_page();
    void add_profile_group(form::Form& form);
    void add_game_group(form::Form& form, std::vector<QLabel*>& captions);
    void add_injection_group(form::Form& form);
    QWidget* build_steam_fields(std::vector<QLabel*>& captions);
    QWidget* build_non_steam_fields(std::vector<QLabel*>& captions);
    QWidget* make_directory_row(QLineEdit* entry, const QString& title);
    QWidget* build_profiles_page();
    QWidget* build_loader_page();
    QWidget* build_logs_page();
    QWidget* build_about_page();

    void setup_ui();
    void size_to_content();
    void refresh_profiles();
    void on_profile_combo_changed(int index);
    void load_profile_by_name(const std::string& name);
    void save_current_as_profile();
    void update_selected_profile();
    void on_history_combo_changed(int index);
    void on_game_combo_changed(int index);
    void browse_game_dir();
    void on_use_loader_toggled();
    void start_injection();
    void refresh_loader_mods();
    void add_loader_mod();
    void remove_loader_mod();
    void open_mods_directory();
    void clear_logs();
    void copy_logs();
    void on_tab_changed(int index);
    void create_profile_from_tab();
    void delete_selected_profile();
    void open_config_directory();
    void update_action_states();

    [[nodiscard]] InjectionConfig collect_config() const;
    [[nodiscard]] std::expected<void, std::string> validate_config() const;
    void apply_config(const InjectionConfig& config);
    void set_status(const std::string& text);
    void show_toast(const std::string& message);
    void sync_source_state();
    void sync_method_state();
    void sync_dll_input_state();
    void sync_loader_console_state();
    void refresh_history_combo();
    void refresh_game_combo();
    void sync_game_dir_button();
    void update_resolved_proton();
    [[nodiscard]] bool is_non_steam() const;
    [[nodiscard]] std::string selected_method() const;
    [[nodiscard]] bool is_linux_iat_method() const;
    [[nodiscard]] std::string selected_proton_path() const;
    [[nodiscard]] std::string game_name_for(const std::string& app_id) const;
    [[nodiscard]] std::string steam_game_install_dir() const;
    [[nodiscard]] std::string exe_browse_directory() const;
    [[nodiscard]] std::string mods_directory() const;
    void append_log(const std::string& line);
    [[nodiscard]] int sleep_ms_value() const;

    QTabWidget* m_tabs = nullptr;
    QScrollArea* m_inject_scroller = nullptr;
    QLabel* m_status_label = nullptr;

    QComboBox* m_profile_combo = nullptr;
    QComboBox* m_history_combo = nullptr;
    QLabel* m_game_heading = nullptr;
    QLabel* m_source_caption = nullptr;
    QWidget* m_source_row = nullptr;
    QComboBox* m_source_combo = nullptr;
    QStackedWidget* m_mode_stack = nullptr;
    QComboBox* m_game_combo = nullptr;
    QPushButton* m_browse_game_dir_btn = nullptr;
    QLabel* m_proton_label = nullptr;
    QComboBox* m_method_combo = nullptr;
    QLineEdit* m_app_id_entry = nullptr;
    QLineEdit* m_exe_entry = nullptr;
    QListWidget* m_dll_list = nullptr;
    QCheckBox* m_use_loader_check = nullptr;
    QLabel* m_loader_console_label = nullptr;
    QComboBox* m_loader_console_combo = nullptr;
    QLabel* m_dll_caption = nullptr;
    QWidget* m_dll_fields = nullptr;
    QLineEdit* m_sleep_entry = nullptr;
    QLineEdit* m_proton_path_entry = nullptr;
    QLineEdit* m_prefix_entry = nullptr;
    QLineEdit* m_game_id_entry = nullptr;
    QPushButton* m_inject_btn = nullptr;
    QPushButton* m_update_profile_btn = nullptr;

    QListWidget* m_profile_list = nullptr;
    QLineEdit* m_new_profile_name = nullptr;
    QLineEdit* m_new_app_id = nullptr;
    QLineEdit* m_new_exe = nullptr;
    QLineEdit* m_new_dll = nullptr;
    QCheckBox* m_new_use_loader = nullptr;

    QLabel* m_loader_path_label = nullptr;
    QPushButton* m_add_mod_btn = nullptr;
    QPushButton* m_remove_mod_btn = nullptr;
    QPushButton* m_open_mods_btn = nullptr;
    QListWidget* m_loader_mods_list = nullptr;
    QPlainTextEdit* m_log_view = nullptr;

    SteamService m_steam;
    HistoryManager m_history;
    ProfileManager m_profiles;
    InjectRunner m_runner;

    std::string m_selected_profile;
    std::vector<SteamGame> m_games;
    std::string m_resolved_proton_path;
};
