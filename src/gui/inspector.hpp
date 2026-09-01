#pragma once

#include "sxpe/commands/bus.hpp"

#include <QWidget>
#include <nlohmann/json.hpp>

class QLabel;
class QPlainTextEdit;
class QTreeWidget;
class QTableWidget;
class QTabWidget;

namespace sxpe::gui {

class Inspector final : public QWidget {
    Q_OBJECT
public:
    explicit Inspector(sxpe::commands::Bus& bus, QWidget* parent = nullptr);
    void set_session(QString session_id);
    void show_resource(const sxpe::commands::UiRow& row);
    void clear();

    QWidget* clone_preview() const;

private:
    void load_preview(const nlohmann::json& rid);
    void load_hex(const nlohmann::json& rid);
    void load_graph(const nlohmann::json& rid);
    void load_text(const nlohmann::json& rid);

    sxpe::commands::Bus& bus_;
    QString session_;
    QTabWidget* tabs_{};
    QLabel* preview_{};
    QPlainTextEdit* hex_{};
    QTreeWidget* graph_{};
    QTableWidget* stbl_{};
    QPlainTextEdit* text_{};
};

}  // namespace sxpe::gui
