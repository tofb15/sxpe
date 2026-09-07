#pragma once

#include "inspector.hpp"
#include "resource_model.hpp"
#include "sxpe/commands/bus.hpp"

#include <QPoint>
#include <QVector>
#include <QWidget>
#include <atomic>

class QComboBox;
class QLineEdit;
class QTableView;

namespace sxpe::gui {

class PackageTab final : public QWidget {
    Q_OBJECT
public:
    PackageTab(sxpe::commands::Bus& bus, QString session_id, QWidget* parent = nullptr);
    [[nodiscard]] QString session_id() const { return session_; }
    void reload();
    void apply_filter();
    bool export_selected(const QString& path, bool raw);
    int export_selected_to_dir(const QString& dir);
    const DisplayRow* current() const;
    QVector<const DisplayRow*> selected() const;
    nlohmann::json current_rid() const;
    void float_preview();
    void copy_preview();
    bool save_preview(const QString& path);
    void select_all();
    /// Clear filters and select the first row matching the TGI key (for Compare jump-to).
    bool select_resource(std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                         std::uint32_t ordinal);
    void apply_column_mask(ColumnMask m);
    [[nodiscard]] int visible_count() const { return model_ ? model_->visible_count() : 0; }

signals:
    void status_changed();
    void resource_context_menu(const QPoint& global);
    void columns_changed();

private:
    sxpe::commands::Bus& bus_;
    QString session_;
    ResourceModel* model_{};
    QTableView* table_{};
    QLineEdit* filter_{};
    QComboBox* tag_{};
    Inspector* inspector_{};
    std::atomic<int> filter_gen_{0};
};

}  // namespace sxpe::gui
