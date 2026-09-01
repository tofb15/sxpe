#pragma once

#include "sxpe/commands/bus.hpp"

#include <QWidget>
#include <nlohmann/json.hpp>

class QLabel;
class QPlainTextEdit;
class QTreeWidget;
class QTableWidget;
class QTabWidget;
class QTimer;

namespace sxpe::gui {

class Inspector final : public QWidget {
    Q_OBJECT
public:
    explicit Inspector(sxpe::commands::Bus& bus, QWidget* parent = nullptr);
    void set_session(QString session_id);
    void show_resource(std::uint32_t type, std::uint32_t mem_size, nlohmann::json rid);
    void clear();

    QWidget* clone_preview() const;

private:
    void flush();
    void load_visible();
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
    QTimer* debounce_{};
    nlohmann::json pending_rid_;
    std::uint32_t pending_type_{0};
    std::uint32_t pending_mem_{0};
    int load_gen_{0};
};

}  // namespace sxpe::gui
