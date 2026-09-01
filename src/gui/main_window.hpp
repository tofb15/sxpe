#pragma once

#include "plugin_host.hpp"
#include "sxpe/commands/bus.hpp"

#include <QMainWindow>
#include <QStringList>

class QTabWidget;
class QLabel;
class QMenu;
class QAction;

namespace sxpe::gui {

class PackageTab;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    bool open_path(const QString& path, bool writable = true);
    void new_package();
    bool smoke_filter(const QString& text);

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    PackageTab* current_tab() const;
    void add_tab(const QString& session_id, const QString& title);
    void merge_dropped_packages(const QStringList& paths);
    void open_dialog();
    void open_readonly_dialog();
    bool save(bool as_copy, bool save_as);
    bool save_tab(PackageTab* tab, bool as_copy, bool save_as);
    bool close_tab(int index);
    void close_other_tabs(int keep);
    void close_tabs_right(int index);
    void close_tabs_left(int index);
    void show_tab_context(const QPoint& local);
    [[nodiscard]] bool is_bookmarked(const QString& path) const;
    void bookmark_path(const QString& path);
    [[nodiscard]] QString package_path(PackageTab* tab);
    void update_status();
    void refresh_tab_chrome(PackageTab* tab);
    int tab_index_for_session(const QString& session_id) const;
    bool path_is_open(const QString& path, const PackageTab* except = nullptr);
    void run_palette();
    void remember_mru(const QString& path);
    void rebuild_mru();
    void rebuild_bookmarks();
    void persist_lists();
    nlohmann::json run(const char* id, nlohmann::json args);
    void warn_if_err(const nlohmann::json& env);
    void copy_resources();
    void paste_resources();
    void add_resource();
    void replace_resource();
    void export_resource();
    void export_to_package();
    void import_files();
    void replace_from_package();
    void copy_resource_key();
    void set_compressed(bool on);
    void set_deleted(bool on);
    void delete_resource();
    void duplicate_resource();
    void details_resource();
    void open_stbl();
    void export_s3sa();
    void clip_export();
    void replace_dds();
    void replace_snap();
    void export_vid();
    void copy_preview();
    void save_preview();
    void bookmark_current();
    void organise_bookmarks();
    void show_resource_context(const QPoint& global);
    void sync_flag_actions();
    void open_external(bool hex);
    void show_licence();
    void show_warranty();
    void show_contents();
    void rebuild_columns_menu();
    void apply_columns_all();
    QString current_package_path();

    sxpe::commands::Bus bus_;
    PluginHost plugins_;
    QTabWidget* tabs_{};
    QLabel* status_path_{};
    QLabel* status_counts_{};
    QMenu* mru_menu_{};
    QMenu* bookmarks_menu_{};
    QMenu* columns_menu_{};
    QAction* compressed_act_{};
    QAction* deleted_act_{};
    QAction* preview_dds_act_{};
    QAction* preview_text_act_{};
    QAction* preview_hex_act_{};
    QAction* dbc_checkpoint_act_{};
    QStringList mru_;
    QStringList bookmarks_;
};

}  // namespace sxpe::gui
