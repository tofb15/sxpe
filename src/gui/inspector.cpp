#include "inspector.hpp"

#include "sxpe/resources/dds.hpp"
#include "sxpe/resources/types.hpp"

#include <QDir>
#include <QFontDatabase>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryFile>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <fstream>
#include <iterator>

namespace sxpe::gui {
namespace {

nlohmann::json rid_json(const sxpe::commands::UiRow& r) {
    return {{"type", r.type},
            {"group", r.group},
            {"instance", r.instance},
            {"ordinal", r.ordinal}};
}

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
    stbl_->setHorizontalHeaderLabels({tr("Id"), tr("Text")});
    stbl_->horizontalHeader()->setStretchLastSection(true);
    stbl_->setVisible(false);
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

void Inspector::show_resource(const sxpe::commands::UiRow& row) {
    if (session_.isEmpty()) {
        return;
    }
    const auto rid = rid_json(row);
    load_preview(rid);
    load_hex(rid);
    load_graph(rid);
    load_text(rid);
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
        stbl_->setVisible(true);
        text_->setVisible(false);
        const auto& ents = st["data"]["entries"];
        stbl_->setRowCount(static_cast<int>(ents.size()));
        int row = 0;
        for (const auto& e : ents) {
            stbl_->setItem(row, 0, new QTableWidgetItem(QString::number(e.value("id", 0ull))));
            stbl_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(e.value("text", ""))));
            ++row;
        }
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

QWidget* Inspector::clone_preview() const { return nullptr; }

}  // namespace sxpe::gui
