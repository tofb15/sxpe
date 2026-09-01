#include "palette.hpp"

#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace sxpe::gui {

CommandPalette::CommandPalette(sxpe::commands::Bus& bus, QWidget* parent)
    : QDialog(parent), bus_(bus) {
    setWindowTitle(tr("Command palette"));
    setModal(true);
    resize(480, 320);
    auto* lay = new QVBoxLayout(this);
    edit_ = new QLineEdit;
    edit_->setPlaceholderText(tr("Type a command…"));
    list_ = new QListWidget;
    lay->addWidget(edit_);
    lay->addWidget(list_);
    connect(edit_, &QLineEdit::textChanged, this, [this](const QString& t) { refill(t); });
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) {
        selected_ = it->data(Qt::UserRole).toString();
        accept();
    });
    refill({});
    edit_->setFocus();
}

void CommandPalette::refill(const QString& needle) {
    list_->clear();
    for (const auto& t : bus_.tools()) {
        const auto id = QString::fromStdString(t.id);
        const auto title = QString::fromStdString(t.title);
        if (!needle.isEmpty() && !id.contains(needle, Qt::CaseInsensitive) &&
            !title.contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        auto* it = new QListWidgetItem(id + " — " + title);
        it->setData(Qt::UserRole, id);
        list_->addItem(it);
    }
    if (list_->count() > 0) {
        list_->setCurrentRow(0);
    }
}

}  // namespace sxpe::gui
