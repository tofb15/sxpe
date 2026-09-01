#include "package_tab.hpp"

#include "sxpe/resources/types.hpp"

#include <nlohmann/json.hpp>

#include <QComboBox>
#include <QDialog>
#include <QFont>
#include <QFontMetrics>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include <thread>

namespace sxpe::gui {

PackageTab::PackageTab(sxpe::commands::Bus& bus, QString session_id, QWidget* parent)
    : QWidget(parent), bus_(bus), session_(std::move(session_id)) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    auto* filter_row = new QHBoxLayout;
    filter_ = new QLineEdit;
    filter_->setPlaceholderText(tr("Filter name, tag, or type hex"));
    filter_->setClearButtonEnabled(true);
    tag_ = new QComboBox;
    tag_->addItem(tr("All"));
    for (const auto& t : sxpe::resources::kTypes) {
        const auto tag = QString::fromUtf8(t.tag.data(), static_cast<int>(t.tag.size()));
        if (tag_->findText(tag) < 0) {
            tag_->addItem(tag);
        }
    }
    filter_row->addWidget(new QLabel(tr("Tag")));
    filter_row->addWidget(tag_);
    filter_row->addWidget(filter_, 1);
    root->addLayout(filter_row);

    auto* split = new QSplitter(Qt::Horizontal);
    table_ = new QTableView;
    model_ = new ResourceModel(this);
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
    table_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(22);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(true);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideNone);
    {
        QFont mono;
        mono.setStyleHint(QFont::Monospace);
        mono.setFamily(QStringLiteral("Consolas"));
        const QFontMetrics fm(mono);
        const int hex8 = fm.horizontalAdvance(QStringLiteral("00000000")) + 20;
        const int hex16 = fm.horizontalAdvance(QStringLiteral("0000000000000000")) + 20;
        auto* hdr = table_->horizontalHeader();
        hdr->setSectionResizeMode(QHeaderView::Interactive);
        hdr->setMinimumSectionSize(24);
        hdr->setStretchLastSection(false);
        table_->setColumnWidth(ResourceModel::Tag, fm.horizontalAdvance(QStringLiteral("_IMG")) + 28);
        table_->setColumnWidth(ResourceModel::Name, 180);
        table_->setColumnWidth(ResourceModel::Type, hex8);
        table_->setColumnWidth(ResourceModel::Group, hex8);
        table_->setColumnWidth(ResourceModel::Instance, hex16);
        table_->setColumnWidth(ResourceModel::Ordinal, fm.horizontalAdvance(QStringLiteral("000")) + 16);
        table_->setColumnWidth(ResourceModel::Size, fm.horizontalAdvance(QStringLiteral("00000000")) + 16);
        table_->setColumnWidth(ResourceModel::Compressed, fm.horizontalAdvance(QStringLiteral("Cmp")) + 16);
        hdr->setSortIndicatorShown(true);
        hdr->setSectionsClickable(true);
    }
    table_->setSortingEnabled(true);
    inspector_ = new Inspector(bus_, this);
    inspector_->set_session(session_);
    split->addWidget(table_);
    split->addWidget(inspector_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    root->addWidget(split, 1);

    auto* debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(40);
    connect(filter_, &QLineEdit::textChanged, debounce, [debounce] { debounce->start(); });
    connect(tag_, &QComboBox::currentTextChanged, debounce, [debounce] { debounce->start(); });
    connect(debounce, &QTimer::timeout, this, &PackageTab::apply_filter);
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& cur, const QModelIndex&) {
                if (const auto* r = model_->row_at(cur.row())) {
                    inspector_->show_resource(r->type, r->mem_size,
                                             nlohmann::json{{"type", r->type},
                                                            {"group", r->group},
                                                            {"instance", r->instance},
                                                            {"ordinal", r->ordinal}});
                }
            });

    reload();
}

void PackageTab::reload() {
    auto rows = bus_.ui_index(session_.toStdString());
    if (!rows) {
        return;
    }
    model_->set_rows(std::move(*rows));
    inspector_->set_session(session_);
    emit status_changed();
}

void PackageTab::apply_filter() {
    const auto text = filter_->text();
    const auto tag = tag_->currentText();
    const int gen = ++filter_gen_;
    const auto snapshot = model_->all();  // QString implicit-share, cheap
    std::thread([this, text, tag, gen, snapshot]() {
        auto vis = filter_rows(snapshot, text, tag);
        QMetaObject::invokeMethod(
            this,
            [this, vis = std::move(vis), gen]() {
                if (gen == filter_gen_) {
                    model_->set_visible(vis);
                    emit status_changed();
                }
            },
            Qt::QueuedConnection);
    }).detach();
}

const DisplayRow* PackageTab::current() const {
    return model_->row_at(table_->currentIndex().row());
}

bool PackageTab::export_selected(const QString& path, bool raw) {
    const auto* r = current();
    if (!r) {
        return false;
    }
    nlohmann::json rid{{"type", r->type},
                       {"group", r->group},
                       {"instance", r->instance},
                       {"ordinal", r->ordinal}};
    auto env = bus_.execute("resource.export", {{"sessionId", session_.toStdString()},
                                                {"resourceId", rid},
                                                {"path", path.toStdString()},
                                                {"raw", raw},
                                                {"force", true}});
    return env.value("ok", false);
}

void PackageTab::float_preview() {
    const auto* r = current();
    if (!r) {
        return;
    }
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("Preview"));
    dlg->setWindowFlag(Qt::Tool);
    auto* lay = new QVBoxLayout(dlg);
    auto* ins = new Inspector(bus_, dlg);
    ins->set_session(session_);
    ins->show_resource(r->type, r->mem_size,
                       nlohmann::json{{"type", r->type},
                                      {"group", r->group},
                                      {"instance", r->instance},
                                      {"ordinal", r->ordinal}});
    lay->addWidget(ins);
    dlg->resize(420, 360);
    dlg->show();
}

void PackageTab::select_all() { table_->selectAll(); }

}  // namespace sxpe::gui
