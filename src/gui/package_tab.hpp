#pragma once

#include "inspector.hpp"
#include "resource_model.hpp"
#include "sxpe/commands/bus.hpp"

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
    const DisplayRow* current() const;
    void float_preview();
    void select_all();
    [[nodiscard]] int visible_count() const { return model_ ? model_->visible_count() : 0; }

signals:
    void status_changed();

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
