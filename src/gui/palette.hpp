#pragma once

#include "sxpe/commands/bus.hpp"

#include <QDialog>

class QLineEdit;
class QListWidget;

namespace sxpe::gui {

class CommandPalette final : public QDialog {
    Q_OBJECT
public:
    explicit CommandPalette(sxpe::commands::Bus& bus, QWidget* parent = nullptr);
    [[nodiscard]] QString selected_id() const { return selected_; }

private:
    void refill(const QString& needle);

    sxpe::commands::Bus& bus_;
    QLineEdit* edit_{};
    QListWidget* list_{};
    QString selected_;
};

}  // namespace sxpe::gui
