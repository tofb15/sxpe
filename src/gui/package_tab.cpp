#include "package_tab.hpp"

#include "sxpe/games/sims3/tgi.hpp"
#include "sxpe/resources/types.hpp"

#include <nlohmann/json.hpp>

#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFont>
#include <QFontMetrics>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSplitter>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <thread>
#include <vector>

namespace sxpe::gui {
namespace {

class ResourceTableView final : public QTableView {
public:
    explicit ResourceTableView(QWidget* parent = nullptr) : QTableView(parent) {
        auto* hdr = horizontalHeader();
        connect(hdr, &QHeaderView::sectionResized, this, &ResourceTableView::on_section_resized);
        hdr->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(hdr, &QWidget::customContextMenuRequested, this,
                &ResourceTableView::on_header_menu);
    }

    void set_mins(std::array<int, ResourceModel::Count_> mins) {
        mins_ = mins;
        int sum = 0;
        for (int m : mins_) {
            sum += m;
        }
        setMinimumWidth(sum + 24);
    }

    void set_defaults(std::array<int, ResourceModel::Count_> defaults) { defaults_ = defaults; }

    void reset_column(int logical) {
        if (logical < 0 || logical >= ResourceModel::Count_) {
            return;
        }
        apply_user_width(logical, defaults_[static_cast<size_t>(logical)]);
    }

    void autofit_column(int logical) {
        auto* m = qobject_cast<ResourceModel*>(model());
        if (!m || logical < 0 || logical >= ResourceModel::Count_) {
            return;
        }
        const int w = std::max(min_for(logical), m->hint_width(logical, fontMetrics()));
        apply_user_width(logical, w);
    }

    void autofit_all() {
        auto* m = qobject_cast<ResourceModel*>(model());
        if (!m || filling_) {
            return;
        }
        filling_ = true;
        const int n = m->columnCount();
        const QFontMetrics fm = fontMetrics();
        std::vector<int> w(static_cast<size_t>(n));
        int sum = 0;
        for (int i = 0; i < n; ++i) {
            w[static_cast<size_t>(i)] = std::max(min_for(i), m->hint_width(i, fm));
            sum += w[static_cast<size_t>(i)];
        }
        const int vw = std::max(1, viewport()->width());
        if (sum > vw) {
            int need = sum - vw;
            while (need > 0) {
                int flexible = 0;
                for (int i = 0; i < n; ++i) {
                    if (w[static_cast<size_t>(i)] > min_for(i)) {
                        ++flexible;
                    }
                }
                if (flexible == 0) {
                    break;
                }
                const int share = std::max(1, need / flexible);
                for (int i = 0; i < n && need > 0; ++i) {
                    const int room = w[static_cast<size_t>(i)] - min_for(i);
                    if (room <= 0) {
                        continue;
                    }
                    const int take = std::min(share, std::min(room, need));
                    w[static_cast<size_t>(i)] -= take;
                    need -= take;
                }
            }
        } else if (sum < vw) {
            int extra = vw - sum;
            const int base = extra / n;
            int rem = extra % n;
            for (int i = 0; i < n; ++i) {
                w[static_cast<size_t>(i)] += base + (rem > 0 ? 1 : 0);
                if (rem > 0) {
                    --rem;
                }
            }
        }
        for (int i = 0; i < n; ++i) {
            setColumnWidth(i, w[static_cast<size_t>(i)]);
        }
        filling_ = false;
        old_vw_ = viewport()->width();
        viewport()->update();
    }

    void reset_all() {
        if (filling_ || !model()) {
            return;
        }
        filling_ = true;
        const int n = model()->columnCount();
        for (int i = 0; i < n; ++i) {
            setColumnWidth(i, std::max(min_for(i), defaults_[static_cast<size_t>(i)]));
        }
        filling_ = false;
        old_vw_ = -1;
        distribute_delta(viewport()->width() - current_sum());
        old_vw_ = viewport()->width();
        viewport()->update();
    }

protected:
    void resizeEvent(QResizeEvent* e) override {
        QTableView::resizeEvent(e);
        const int vw = viewport()->width();
        if (old_vw_ < 0) {
            distribute_delta(vw - current_sum());
        } else if (vw != old_vw_) {
            distribute_delta(vw - old_vw_);
        }
        old_vw_ = viewport()->width();
        viewport()->update();
    }
    void showEvent(QShowEvent* e) override {
        QTableView::showEvent(e);
        if (old_vw_ < 0) {
            distribute_delta(viewport()->width() - current_sum());
            old_vw_ = viewport()->width();
        }
    }
    void paintEvent(QPaintEvent* e) override {
        QPainter bg(viewport());
        bg.fillRect(e->rect(), palette().color(QPalette::Base));
        bg.end();
        QTableView::paintEvent(e);
    }

private:
    int min_for(int col) const {
        if (col < 0 || col >= ResourceModel::Count_) {
            return 32;
        }
        return mins_[static_cast<size_t>(col)];
    }

    int current_sum() const {
        int s = 0;
        const int n = model() ? model()->columnCount() : 0;
        for (int i = 0; i < n; ++i) {
            s += columnWidth(i);
        }
        return s;
    }

    void distribute_delta(int delta) {
        if (filling_ || !model() || delta == 0) {
            return;
        }
        filling_ = true;
        const int n = model()->columnCount();
        std::vector<int> w(static_cast<size_t>(n));
        std::vector<int> mn(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            mn[static_cast<size_t>(i)] = min_for(i);
            w[static_cast<size_t>(i)] = std::max(columnWidth(i), mn[static_cast<size_t>(i)]);
        }
        if (delta > 0) {
            const int base = delta / n;
            int rem = delta % n;
            for (int i = 0; i < n; ++i) {
                w[static_cast<size_t>(i)] += base + (rem > 0 ? 1 : 0);
                if (rem > 0) {
                    --rem;
                }
            }
        } else {
            int need = -delta;
            while (need > 0) {
                int flexible = 0;
                for (int i = 0; i < n; ++i) {
                    if (w[static_cast<size_t>(i)] > mn[static_cast<size_t>(i)]) {
                        ++flexible;
                    }
                }
                if (flexible == 0) {
                    break;
                }
                const int share = std::max(1, need / flexible);
                for (int i = 0; i < n && need > 0; ++i) {
                    const int room = w[static_cast<size_t>(i)] - mn[static_cast<size_t>(i)];
                    if (room <= 0) {
                        continue;
                    }
                    const int take = std::min(share, std::min(room, need));
                    w[static_cast<size_t>(i)] -= take;
                    need -= take;
                }
            }
        }
        for (int i = 0; i < n; ++i) {
            setColumnWidth(i, w[static_cast<size_t>(i)]);
        }
        filling_ = false;
    }

    void apply_user_width(int logical, int new_size) {
        if (!model()) {
            return;
        }
        filling_ = true;
        const int n = model()->columnCount();
        const int old_size = columnWidth(logical);
        const int mn = min_for(logical);
        new_size = std::max(new_size, mn);
        int delta = new_size - old_size;
        setColumnWidth(logical, new_size);
        auto steal = [&](int i) {
            if (delta == 0 || i < 0 || i >= n || i == logical) {
                return;
            }
            if (delta > 0) {
                const int room = columnWidth(i) - min_for(i);
                const int take = std::min(room, delta);
                if (take > 0) {
                    setColumnWidth(i, columnWidth(i) - take);
                    delta -= take;
                }
            } else {
                setColumnWidth(i, columnWidth(i) - delta);
                delta = 0;
            }
        };
        for (int i = logical + 1; i < n; ++i) {
            steal(i);
        }
        for (int i = logical - 1; i >= 0; --i) {
            steal(i);
        }
        if (delta > 0) {
            setColumnWidth(logical, columnWidth(logical) - delta);
        }
        filling_ = false;
    }

    void on_section_resized(int logical, int /*old_size*/, int new_size) {
        if (filling_) {
            return;
        }
        apply_user_width(logical, new_size);
    }

    void on_header_menu(const QPoint& pos) {
        auto* hdr = horizontalHeader();
        const int col = hdr->logicalIndexAt(pos);
        QMenu menu(this);
        auto* fit_col = menu.addAction(tr("Autofit column"));
        fit_col->setEnabled(col >= 0);
        auto* fit_all = menu.addAction(tr("Autofit all"));
        auto* reset = menu.addAction(tr("Reset"));
        auto* chosen = menu.exec(hdr->mapToGlobal(pos));
        if (chosen == fit_col) {
            autofit_column(col);
        } else if (chosen == fit_all) {
            autofit_all();
        } else if (chosen == reset) {
            reset_all();
        }
    }

    std::array<int, ResourceModel::Count_> mins_{};
    std::array<int, ResourceModel::Count_> defaults_{};
    int old_vw_{-1};
    bool filling_{false};
};

}  // namespace

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
    split->setChildrenCollapsible(false);
    split->setOpaqueResize(true);
    split->setHandleWidth(8);
    table_ = new ResourceTableView;
    model_ = new ResourceModel(this);
    table_->setModel(model_);
    table_->setItemDelegate(new ResourcePaintDelegate(table_));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setTabKeyNavigation(false);
    table_->setShowGrid(false);
    table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideNone);
    table_->setAlternatingRowColors(false);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->verticalHeader()->setDefaultSectionSize(22);
    table_->verticalHeader()->setMinimumSectionSize(22);
    table_->viewport()->setAutoFillBackground(true);
    table_->setMouseTracking(false);
    {
        QFont mono;
        mono.setStyleHint(QFont::Monospace);
        mono.setFamily(QStringLiteral("Consolas"));
        mono.setPointSize(9);
        table_->setFont(mono);
        const QFontMetrics fm(mono);
        const int hex8 = fm.horizontalAdvance(QStringLiteral("00000000")) + 20;
        const int hex16 = fm.horizontalAdvance(QStringLiteral("0000000000000000")) + 20;
        auto* hdr = table_->horizontalHeader();
        hdr->setSectionResizeMode(QHeaderView::Interactive);
        hdr->setCascadingSectionResizes(false);
        hdr->setStretchLastSection(false);
        hdr->setSectionsMovable(false);
        hdr->setHighlightSections(false);
        hdr->setSortIndicatorShown(true);
        hdr->setSectionsClickable(true);
        const int id_w = fm.horizontalAdvance(QStringLiteral("00000")) + 20;
        const int tag_w = fm.horizontalAdvance(QStringLiteral("_IMG")) + 28;
        const int name_w = 180;
        const int ord_w = fm.horizontalAdvance(QStringLiteral("000")) + 20;
        const int size_w = fm.horizontalAdvance(QStringLiteral("00000000")) + 20;
        const int cmp_w = fm.horizontalAdvance(QStringLiteral("Cmp")) + 20;
        std::array<int, ResourceModel::Count_> mins{};
        mins[ResourceModel::Id] = id_w;
        mins[ResourceModel::Tag] = tag_w;
        mins[ResourceModel::Name] = 72;
        mins[ResourceModel::Type] = hex8;
        mins[ResourceModel::Group] = hex8;
        mins[ResourceModel::Instance] = hex16;
        mins[ResourceModel::Ordinal] = ord_w;
        mins[ResourceModel::Size] = size_w;
        mins[ResourceModel::Compressed] = cmp_w;
        std::array<int, ResourceModel::Count_> defs = mins;
        defs[ResourceModel::Name] = name_w;
        auto* grid = static_cast<ResourceTableView*>(table_);
        grid->set_mins(mins);
        grid->set_defaults(defs);
        hdr->setMinimumSectionSize(32);
        hdr->setSortIndicator(-1, Qt::AscendingOrder);
        table_->setColumnWidth(ResourceModel::Id, id_w);
        table_->setColumnWidth(ResourceModel::Tag, tag_w);
        table_->setColumnWidth(ResourceModel::Name, name_w);
        table_->setColumnWidth(ResourceModel::Type, hex8);
        table_->setColumnWidth(ResourceModel::Group, hex8);
        table_->setColumnWidth(ResourceModel::Instance, hex16);
        table_->setColumnWidth(ResourceModel::Ordinal, ord_w);
        table_->setColumnWidth(ResourceModel::Size, size_w);
        table_->setColumnWidth(ResourceModel::Compressed, cmp_w);
        connect(hdr, &QHeaderView::sectionHandleDoubleClicked, table_,
                [grid](int logical) { grid->reset_column(logical); });
    }
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint& p) {
        emit resource_context_menu(table_->viewport()->mapToGlobal(p));
    });
    inspector_ = new Inspector(bus_, this);
    inspector_->setMinimumWidth(220);
    inspector_->set_session(session_);
    connect(inspector_, &Inspector::mutated, this, &PackageTab::status_changed);
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
                                                            {"ordinal", r->ordinal}},
                                             r->name);
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

QVector<const DisplayRow*> PackageTab::selected() const {
    QVector<const DisplayRow*> out;
    if (!table_->selectionModel()) {
        return out;
    }
    for (const auto& idx : table_->selectionModel()->selectedRows()) {
        if (const auto* r = model_->row_at(idx.row())) {
            out.push_back(r);
        }
    }
    if (out.isEmpty()) {
        if (const auto* r = current()) {
            out.push_back(r);
        }
    }
    return out;
}

nlohmann::json PackageTab::current_rid() const {
    const auto* r = current();
    if (!r) {
        return nlohmann::json();
    }
    return {{"type", r->type},
            {"group", r->group},
            {"instance", r->instance},
            {"ordinal", r->ordinal}};
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

int PackageTab::export_selected_to_dir(const QString& dir) {
    int n = 0;
    for (const auto* r : selected()) {
        sxpe::games::sims3::Tgi tgi{r->type, r->group, r->instance};
        const auto ext = r->tag.isEmpty() ? "bin" : r->tag.toStdString();
        const auto name = r->name.isEmpty() ? "resource" : r->name.toStdString();
        const auto fn = QString::fromStdString(sxpe::games::sims3::community_filename(tgi, name, ext));
        const auto path = QDir(dir).filePath(fn);
        nlohmann::json rid{{"type", r->type},
                           {"group", r->group},
                           {"instance", r->instance},
                           {"ordinal", r->ordinal}};
        auto env = bus_.execute("resource.export", {{"sessionId", session_.toStdString()},
                                                    {"resourceId", rid},
                                                    {"path", path.toStdString()},
                                                    {"force", true}});
        if (env.value("ok", false)) {
            ++n;
        }
    }
    return n;
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
                                      {"ordinal", r->ordinal}},
                       r->name);
    lay->addWidget(ins);
    dlg->resize(420, 360);
    dlg->show();
}

void PackageTab::copy_preview() {
    if (inspector_) {
        inspector_->copy_visible();
    }
}

bool PackageTab::save_preview(const QString& path) {
    return inspector_ ? inspector_->save_visible(path) : false;
}

void PackageTab::select_all() { table_->selectAll(); }

}  // namespace sxpe::gui
