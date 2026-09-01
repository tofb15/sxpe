#include "inspector.hpp"

#include "sxpe/core/caps.hpp"
#include "sxpe/resources/dds.hpp"
#include "sxpe/resources/types.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSettings>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryFile>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <fstream>
#include <iterator>

namespace sxpe::gui {
namespace {

}  // namespace

Inspector::Inspector(sxpe::commands::Bus& bus, QWidget* parent) : QWidget(parent), bus_(bus) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget;
    preview_ = new QLabel(tr("No selection"));
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumSize(160, 120);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(preview_);
    hex_ = new QPlainTextEdit;
    hex_->setReadOnly(true);
    hex_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    graph_ = new QTreeWidget;
    graph_->setHeaderLabels({tr("Field"), tr("Value")});
    graph_->header()->setStretchLastSection(true);
    stbl_ = new QTableWidget(0, 2);
    stbl_->setHorizontalHeaderLabels({tr("Id (hex)"), tr("Text")});
    stbl_->horizontalHeader()->setStretchLastSection(true);
    stbl_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                           QAbstractItemView::SelectedClicked);
    stbl_->setVisible(false);
    connect(stbl_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (stbl_loading_ || !item || item->column() != 1 || session_.isEmpty() ||
            pending_rid_.is_null()) {
            return;
        }
        auto* id_it = stbl_->item(item->row(), 0);
        if (!id_it) {
            return;
        }
        const auto id = id_it->text().toULongLong(nullptr, 16);
        auto env = bus_.execute("stbl.set", {{"sessionId", session_.toStdString()},
                                             {"resourceId", pending_rid_},
                                             {"id", id},
                                             {"text", item->text().toStdString()}});
        if (env.value("ok", false)) {
            emit mutated();
        }
    });
    text_ = new QPlainTextEdit;
    text_->setReadOnly(true);
    auto* text_wrap = new QWidget;
    auto* tl = new QVBoxLayout(text_wrap);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->addWidget(stbl_);
    tl->addWidget(text_);
    tabs_->addTab(scroll, tr("Preview"));
    tabs_->addTab(hex_, tr("Hex"));
    tabs_->addTab(graph_, tr("Graph"));
    tabs_->addTab(text_wrap, tr("Text"));
    lay->addWidget(tabs_);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& p) {
        QMenu m(this);
        m.addAction(tr("Copy preview"), this, [this] { copy_visible(); });
        m.exec(mapToGlobal(p));
    });
    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(50);
    connect(debounce_, &QTimer::timeout, this, &Inspector::flush);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int) { load_visible(); });
}

void Inspector::set_session(QString session_id) { session_ = std::move(session_id); }

void Inspector::clear() {
    preview_->setPixmap({});
    preview_->setText(tr("No selection"));
    hex_->clear();
    graph_->clear();
    stbl_->setRowCount(0);
    text_->clear();
}

void Inspector::show_resource(std::uint32_t type, std::uint32_t mem_size, nlohmann::json rid) {
    pending_type_ = type;
    pending_mem_ = mem_size;
    pending_rid_ = std::move(rid);
    preview_->setText(tr("…"));
    debounce_->start();
}

void Inspector::flush() { load_visible(); }

void Inspector::load_visible() {
    if (session_.isEmpty() || pending_rid_.is_null()) {
        return;
    }
    const int pane = tabs_->currentIndex();
    const auto& rid = pending_rid_;
    QSettings st(QStringLiteral("SXPE"), QStringLiteral("SXPE"));
    if (pane == 0 && !st.value("preview/dds", true).toBool()) {
        preview_->setPixmap({});
        preview_->setText(tr("DDS preview is off (Settings)."));
        return;
    }
    if (pane == 1 && !st.value("preview/hex", true).toBool()) {
        hex_->setPlainText(tr("Hex preview is off (Settings)."));
        return;
    }
    if (pane == 3 && !st.value("preview/text", true).toBool()) {
        stbl_->setVisible(false);
        text_->setVisible(true);
        text_->setPlainText(tr("Text preview is off (Settings)."));
        return;
    }
    if (pending_mem_ > sxpe::core::caps::kMaxLivePreviewBytes) {
        const auto msg = tr("Resource is %1 MB — live preview skipped.")
                             .arg(pending_mem_ / (1024.0 * 1024.0), 0, 'f', 1);
        if (pane == 0) {
            preview_->setPixmap({});
            preview_->setText(msg);
        } else if (pane == 1) {
            hex_->setPlainText(msg);
        } else if (pane == 2) {
            graph_->clear();
        } else {
            text_->setPlainText(msg);
            stbl_->setVisible(false);
            text_->setVisible(true);
        }
        return;
    }
    if (pane == 0) {
        if (pending_type_ == sxpe::resources::kImg || pending_type_ == sxpe::resources::kImgAlt) {
            load_preview(rid);
        } else {
            preview_->setPixmap({});
            preview_->setText(tr("No image preview"));
        }
    } else if (pane == 1) {
        load_hex(rid);
    } else if (pane == 2) {
        load_graph(rid);
    } else {
        load_text(rid);
    }
}

void Inspector::load_preview(const nlohmann::json& rid) {
    preview_->setPixmap({});
    preview_->setText(tr("No image preview"));
    QTemporaryFile tmp(QDir::tempPath() + "/sxpe-prev-XXXXXX.dds");
    tmp.setAutoRemove(true);
    if (!tmp.open()) {
        return;
    }
    const auto path = tmp.fileName().toStdString();
    auto exp = bus_.execute("resource.export", {{"sessionId", session_.toStdString()},
                                                {"resourceId", rid},
                                                {"path", path},
                                                {"force", true}});
    if (!exp.value("ok", false)) {
        return;
    }
    std::ifstream f(path, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(raw[i]));
    }
    auto pix = sxpe::resources::decode_dds_rgba(bytes);
    if (!pix) {
        auto info = sxpe::resources::parse_dds(bytes);
        if (info) {
            preview_->setText(tr("%1×%2 %3 (no pixel decode)")
                                  .arg(info->width)
                                  .arg(info->height)
                                  .arg(QString::fromStdString(info->format)));
        }
        return;
    }
    auto inf = sxpe::resources::parse_dds(bytes);
    if (!inf) {
        return;
    }
    QImage img(reinterpret_cast<const uchar*>(pix->data()), static_cast<int>(inf->width),
               static_cast<int>(inf->height), static_cast<int>(inf->width * 4),
               QImage::Format_RGBA8888);
    QPixmap pm = QPixmap::fromImage(img.copy());
    if (pm.width() > 512 || pm.height() > 512) {
        pm = pm.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    preview_->setPixmap(pm);
    preview_->setText({});
}

void Inspector::load_hex(const nlohmann::json& rid) {
    auto env = bus_.execute("hex.get", {{"sessionId", session_.toStdString()},
                                        {"resourceId", rid},
                                        {"maxBytes", 4096}});
    if (!env.value("ok", false)) {
        hex_->setPlainText(QString::fromStdString(env.dump()));
        return;
    }
    const auto hs = QString::fromStdString(env["data"].value("hex", ""));
    QString dump;
    for (int i = 0; i + 1 < hs.size(); i += 2) {
        if (i && (i / 2) % 16 == 0) {
            dump += '\n';
        }
        dump += hs.mid(i, 2);
        dump += ' ';
    }
    hex_->setPlainText(dump);
}

void Inspector::load_graph(const nlohmann::json& rid) {
    graph_->clear();
    auto env = bus_.execute("graph.get", {{"sessionId", session_.toStdString()}, {"resourceId", rid}});
    if (!env.value("ok", false)) {
        return;
    }
    const auto& data = env["data"];
    auto* root = new QTreeWidgetItem(graph_);
    root->setText(0, QString::fromStdString(data.value("type", "resource")));
    root->setText(1, QString::number(data.value("rawSize", 0)));
    if (data.contains("nodes")) {
        for (const auto& n : data["nodes"]) {
            auto* it = new QTreeWidgetItem(root);
            it->setText(0, QString::fromStdString(n.value("label", n.value("id", ""))));
            if (n["value"].is_string()) {
                it->setText(1, QString::fromStdString(n["value"].get<std::string>()));
            } else {
                it->setText(1, QString::fromStdString(n["value"].dump()));
            }
        }
    }
    graph_->expandAll();
}

void Inspector::load_text(const nlohmann::json& rid) {
    stbl_->setRowCount(0);
    text_->clear();
    auto st = bus_.execute("stbl.get", {{"sessionId", session_.toStdString()}, {"resourceId", rid}});
    if (st.value("ok", false) && st["data"].contains("entries")) {
        stbl_loading_ = true;
        stbl_->setVisible(true);
        text_->setVisible(false);
        const auto& ents = st["data"]["entries"];
        stbl_->setRowCount(static_cast<int>(ents.size()));
        int row = 0;
        for (const auto& e : ents) {
            auto* id_it = new QTableWidgetItem(QString("%1")
                                                   .arg(e.value("id", 0ull), 16, 16, QLatin1Char('0'))
                                                   .toUpper());
            id_it->setFlags(id_it->flags() & ~Qt::ItemIsEditable);
            stbl_->setItem(row, 0, id_it);
            stbl_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(e.value("text", ""))));
            ++row;
        }
        stbl_loading_ = false;
        return;
    }
    stbl_->setVisible(false);
    text_->setVisible(true);
    auto env = bus_.execute("text.get", {{"sessionId", session_.toStdString()},
                                         {"resourceId", rid},
                                         {"maxBytes", 8192}});
    if (env.value("ok", false)) {
        text_->setPlainText(QString::fromStdString(env["data"].value("text", "")));
    }
}

void Inspector::copy_visible() {
    auto* cb = QApplication::clipboard();
    if (!cb) {
        return;
    }
    const int pane = tabs_->currentIndex();
    if (pane == 0) {
        const QPixmap pm = preview_->pixmap();
        if (!pm.isNull()) {
            cb->setPixmap(pm);
            return;
        }
        cb->setText(preview_->text());
        return;
    }
    if (pane == 1) {
        cb->setText(hex_->toPlainText());
        return;
    }
    if (pane == 2) {
        auto* it = graph_->currentItem();
        cb->setText(it ? (it->text(0) + '\t' + it->text(1)) : QString());
        return;
    }
    if (stbl_->isVisible()) {
        QString tsv;
        for (int row = 0; row < stbl_->rowCount(); ++row) {
            const auto* a = stbl_->item(row, 0);
            const auto* b = stbl_->item(row, 1);
            tsv += (a ? a->text() : QString()) + '\t' + (b ? b->text() : QString()) + '\n';
        }
        cb->setText(tsv);
        return;
    }
    cb->setText(text_->toPlainText());
}

bool Inspector::save_visible(const QString& path) {
    if (path.isEmpty()) {
        return false;
    }
    const int pane = tabs_->currentIndex();
    if (pane == 0) {
        const QPixmap pm = preview_->pixmap();
        if (!pm.isNull()) {
            return pm.save(path);
        }
    }
    QString body;
    if (pane == 1) {
        body = hex_->toPlainText();
    } else if (pane == 2) {
        for (int i = 0; i < graph_->topLevelItemCount(); ++i) {
            auto* it = graph_->topLevelItem(i);
            body += it->text(0) + '\t' + it->text(1) + '\n';
            for (int c = 0; c < it->childCount(); ++c) {
                auto* ch = it->child(c);
                body += ch->text(0) + '\t' + ch->text(1) + '\n';
            }
        }
    } else if (stbl_->isVisible()) {
        for (int row = 0; row < stbl_->rowCount(); ++row) {
            const auto* a = stbl_->item(row, 0);
            const auto* b = stbl_->item(row, 1);
            body += (a ? a->text() : QString()) + '\t' + (b ? b->text() : QString()) + '\n';
        }
    } else {
        body = text_->toPlainText();
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    f.write(body.toUtf8());
    return true;
}

QWidget* Inspector::clone_preview() const { return nullptr; }

}  // namespace sxpe::gui
