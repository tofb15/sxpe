#pragma once

#include "plugin_host.hpp"
#include "sxpe/commands/bus.hpp"

#include <QMainWindow>
#include <QStringList>

class QTabWidget;
class QLabel;
class QMenu;

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
    void open_dialog();
    bool save(bool as_copy, bool save_as);
    void close_tab(int index);
    void update_status();
    void run_palette();
    void remember_mru(const QString& path);
    void rebuild_mru();
    nlohmann::json run(const char* id, nlohmann::json args);
    void warn_if_err(const nlohmann::json& env);
    void export_resource();
    void delete_resource();
    void duplicate_resource();
    void open_external(bool hex);

    sxpe::commands::Bus bus_;
    PluginHost plugins_;
    QTabWidget* tabs_{};
    QLabel* status_path_{};
    QLabel* status_counts_{};
    QMenu* mru_menu_{};
    QStringList mru_;
};

}  // namespace sxpe::gui
