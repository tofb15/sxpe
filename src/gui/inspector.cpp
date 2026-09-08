#include "inspector.hpp"

#include "sxpe/core/caps.hpp"
#include "sxpe/games/sims3/tgi.hpp"
#include "sxpe/resources/dds.hpp"
#include "sxpe/resources/types.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSettings>
#include <QSizePolicy>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryFile>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace sxpe::gui {
namespace {

QString hex32(std::uint32_t v) { return QString("%1").arg(v, 8, 16, QLatin1Char('0')).toUpper(); }
QString hex64(std::uint64_t v) { return QString("%1").arg(v, 16, 16, QLatin1Char('0')).toUpper(); }

QString from_sv(std::string_view s) {
    return QString::fromUtf8(s.data(), static_cast<int>(s.size()));
}

bool looks_png(const std::vector<char>& raw) {
    return raw.size() >= 8 && static_cast<unsigned char>(raw[0]) == 0x89 && raw[1] == 'P' &&
           raw[2] == 'N' && raw[3] == 'G';
}

bool looks_jpeg(const std::vector<char>& raw) {
    return raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xFF &&
           static_cast<unsigned char>(raw[1]) == 0xD8 &&
           static_cast<unsigned char>(raw[2]) == 0xFF;
}

bool looks_dds(const std::vector<char>& raw) {
    return raw.size() >= 4 && raw[0] == 'D' && raw[1] == 'D' && raw[2] == 'S' && raw[3] == ' ';
}

bool looks_mz(const std::vector<char>& raw) {
    return raw.size() >= 2 && raw[0] == 'M' && raw[1] == 'Z';
}

QString decode_preview_bytes(const std::vector<char>& raw) {
    const auto n = raw.size();
    const auto* p = reinterpret_cast<const unsigned char*>(raw.data());
    if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        return QString::fromUtf16(reinterpret_cast<const char16_t*>(p + 2),
                                  static_cast<int>((n - 2) / 2));
    }
    if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
        QString out;
        out.resize(static_cast<int>((n - 2) / 2));
        for (int i = 0; i < out.size(); ++i) {
            out[i] = QChar(static_cast<char16_t>((p[2 + 2 * i] << 8) | p[3 + 2 * i]));
        }
        return out;
    }
    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        return QString::fromUtf8(raw.data() + 3, static_cast<int>(n - 3));
    }
    if (n >= 8) {
        const int sample = static_cast<int>(std::min<std::size_t>(n, 64));
        int zeros = 0;
        for (int i = 1; i < sample; i += 2) {
            if (p[i] == 0) {
                ++zeros;
            }
        }
        if (zeros >= sample / 4) {
            return QString::fromUtf16(reinterpret_cast<const char16_t*>(p), static_cast<int>(n / 2));
        }
    }
    return QString::fromUtf8(raw.data(), static_cast<int>(n));
}

QString pretty_xml_excerpt(QString xml, int max_chars) {
    xml.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    xml.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    xml = xml.trimmed();
    if (xml.contains(QLatin1Char('\n'))) {
        if (xml.size() > max_chars) {
            xml.truncate(max_chars);
            xml += QChar(0x2026);
        }
        return xml;
    }
    QString out;
    int depth = 0;
    int i = 0;
    while (i < xml.size() && out.size() < max_chars) {
        if (xml[i] == QLatin1Char('<')) {
            const int end = xml.indexOf(QLatin1Char('>'), i);
            if (end < 0) {
                out += xml.mid(i);
                break;
            }
            const QString tag = xml.mid(i, end - i + 1);
            const bool close = tag.startsWith(QLatin1String("</"));
            const bool self = tag.endsWith(QLatin1String("/>")) ||
                              tag.startsWith(QLatin1String("<?")) ||
                              tag.startsWith(QLatin1String("<!"));
            if (close && depth > 0) {
                --depth;
            }
            if (!out.isEmpty() && !out.endsWith(QLatin1Char('\n'))) {
                out += QLatin1Char('\n');
            }
            out += QString(depth * 2, QLatin1Char(' '));
            out += tag;
            if (!close && !self) {
                ++depth;
            }
            i = end + 1;
        } else {
            int next = xml.indexOf(QLatin1Char('<'), i);
            if (next < 0) {
                next = xml.size();
            }
            const QString text = xml.mid(i, next - i).trimmed();
            if (!text.isEmpty()) {
                if (!out.isEmpty() && !out.endsWith(QLatin1Char('\n'))) {
                    out += QLatin1Char('\n');
                }
                out += QString(depth * 2, QLatin1Char(' '));
                out += text;
            }
            i = next;
        }
    }
    if (i < xml.size()) {
        out += QChar(0x2026);
    }
    return out;
}

QString sniff_kind(const std::vector<char>& raw) {
    if (looks_png(raw)) {
        return QStringLiteral("PNG");
    }
    if (looks_jpeg(raw)) {
        return QStringLiteral("JPEG");
    }
    if (looks_dds(raw)) {
        return QStringLiteral("DDS");
    }
    if (looks_mz(raw)) {
        return QStringLiteral("PE (MZ)");
    }
    if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFF &&
        static_cast<unsigned char>(raw[1]) == 0xFE) {
        return QStringLiteral("UTF-16LE");
    }
    if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFE &&
        static_cast<unsigned char>(raw[1]) == 0xFF) {
        return QStringLiteral("UTF-16BE");
    }
    const QString t = decode_preview_bytes(raw).trimmed();
    if (t.startsWith(QLatin1Char('<')) || t.startsWith(QLatin1String("<?xml"))) {
        return QStringLiteral("XML");
    }
    return {};
}

QString hex_excerpt(const std::vector<char>& raw, int max_bytes) {
    const int n = std::min(max_bytes, static_cast<int>(raw.size()));
    QString dump;
    dump.reserve(n * 3);
    for (int i = 0; i < n; ++i) {
        if (i && (i % 16) == 0) {
            dump += QLatin1Char('\n');
        }
        dump += QString("%1 ").arg(static_cast<unsigned char>(raw[static_cast<std::size_t>(i)]), 2,
                                   16, QLatin1Char('0'))
                    .toUpper();
    }
    if (static_cast<int>(raw.size()) > n) {
        dump += QChar(0x2026);
    }
    return dump;
}

bool mostly_text(const QString& s) {
    if (s.isEmpty()) {
        return false;
    }
    int bad = 0;
    for (const QChar c : s) {
        if (!c.isPrint() && !c.isSpace()) {
            ++bad;
        }
    }
    return bad * 10 < s.size();
}

QString clip_line(const QString& s, int max_len) {
    if (s.size() <= max_len) {
        return s;
    }
    return s.left(max_len) + QChar(0x2026);
}

}  // namespace

Inspector::Inspector(sxpe::commands::Bus& bus, QWidget* parent) : QWidget(parent), bus_(bus) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget;
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    auto* preview_pane = new QWidget;
    auto* preview_lay = new QVBoxLayout(preview_pane);
    preview_lay->setContentsMargins(0, 0, 0, 0);
    preview_card_ = new QLabel(tr("No selection"));
    preview_card_->setWordWrap(true);
    preview_card_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    preview_card_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    preview_card_->setFont(mono);
    preview_card_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    preview_ = new QLabel;
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumSize(160, 120);
    preview_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(preview_, &QWidget::customContextMenuRequested, this, &Inspector::popup_image_menu);
    preview_scroll_ = new QScrollArea;
    preview_scroll_->setWidgetResizable(true);
    preview_scroll_->setWidget(preview_);
    preview_scroll_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(preview_scroll_, &QWidget::customContextMenuRequested, this, &Inspector::popup_image_menu);
    preview_scroll_->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(preview_scroll_->viewport(), &QWidget::customContextMenuRequested, this,
            &Inspector::popup_image_menu);
    preview_scroll_->hide();
    preview_body_ = new QPlainTextEdit;
    preview_body_->setReadOnly(true);
    preview_body_->setFont(mono);
    preview_body_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    preview_body_->hide();
    preview_lay->addWidget(preview_card_);
    preview_lay->addWidget(preview_scroll_, 1);
    preview_lay->addWidget(preview_body_, 1);
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
    tabs_->addTab(preview_pane, tr("Preview"));
    tabs_->addTab(hex_, tr("Hex"));
    tabs_->addTab(graph_, tr("Graph"));
    tabs_->addTab(text_wrap, tr("Text"));
    lay->addWidget(tabs_);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& p) {
        QMenu m(this);
        m.addAction(tr("Copy preview"), this, [this] { copy_visible(); });
        auto* save = m.addAction(tr("Save As…"), this, [this] { save_image_as(); });
        save->setEnabled(!preview_->pixmap().isNull());
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
    preview_card_->setText(tr("No selection"));
    preview_->setPixmap({});
    preview_->setText({});
    preview_scroll_->hide();
    preview_body_->hide();
    preview_body_->clear();
    hex_->clear();
    graph_->clear();
    stbl_->setRowCount(0);
    text_->clear();
}

void Inspector::show_resource(std::uint32_t type, std::uint32_t mem_size, nlohmann::json rid,
                             QString name) {
    pending_type_ = type;
    pending_mem_ = mem_size;
    pending_rid_ = std::move(rid);
    pending_name_ = std::move(name);
    preview_card_->setText(tr("…"));
    preview_->setPixmap({});
    preview_->setText({});
    preview_scroll_->hide();
    preview_body_->hide();
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
        const auto msg = tr("Resource is %1 MB — live preview refused (hard cap %2 MB).")
                             .arg(pending_mem_ / (1024.0 * 1024.0), 0, 'f', 1)
                             .arg(sxpe::core::caps::kMaxLivePreviewBytes / (1024.0 * 1024.0), 0, 'f', 0);
        if (pane == 0) {
            load_preview(rid);
            return;
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
        load_preview(rid);
    } else if (pane == 1) {
        load_hex(rid);
    } else if (pane == 2) {
        load_graph(rid);
    } else {
        load_text(rid);
    }
}

QString Inspector::identity_card() const {
    const auto tag = sxpe::resources::tag_for(pending_type_);
    const auto desc = sxpe::resources::name_for(pending_type_);
    const QString tag_s = tag.empty() ? tr("(untagged)") : from_sv(tag);
    const auto type = pending_rid_.value("type", pending_type_);
    const auto group = pending_rid_.value("group", 0u);
    const auto inst = pending_rid_.value("instance", 0ull);
    const auto ord = pending_rid_.value("ordinal", 0u);
    QStringList lines;
    if (desc.empty()) {
        lines << tag_s;
    } else {
        lines << QStringLiteral("%1 — %2").arg(tag_s, from_sv(desc));
    }
    lines << QStringLiteral("%1-%2-%3").arg(hex32(type), hex32(group), hex64(inst));
    if (ord != 0) {
        lines << tr("Ordinal %1").arg(ord);
    }
    lines << tr("%1 bytes").arg(pending_mem_);
    if (!pending_name_.isEmpty()) {
        lines << tr("Name: %1").arg(pending_name_);
    }
    return lines.join(QLatin1Char('\n'));
}

void Inspector::show_preview_image(const QPixmap& pm) {
    preview_->setPixmap(pm);
    preview_->setText({});
    preview_scroll_->show();
    preview_body_->hide();
    preview_body_->clear();
}

void Inspector::show_preview_body(const QString& text) {
    preview_->setPixmap({});
    preview_->setText({});
    preview_scroll_->hide();
    preview_body_->setPlainText(text);
    preview_body_->setVisible(!text.isEmpty());
}

void Inspector::load_preview(const nlohmann::json& rid) {
    preview_->setPixmap({});
    preview_->setText({});
    preview_scroll_->hide();
    preview_body_->hide();
    preview_body_->clear();
    preview_card_->setText(identity_card());

    if (pending_mem_ > sxpe::core::caps::kMaxLivePreviewBytes) {
        show_preview_body(tr("Resource is %1 MB — live preview refused (hard cap %2 MB).")
                              .arg(pending_mem_ / (1024.0 * 1024.0), 0, 'f', 1)
                              .arg(sxpe::core::caps::kMaxLivePreviewBytes / (1024.0 * 1024.0), 0, 'f', 0));
        return;
    }

    const auto sid = session_.toStdString();
    constexpr int kPreviewRows = 20;
    constexpr int kXmlBytes = 4096;

    if (pending_type_ == sxpe::resources::kStbl) {
        auto st = bus_.execute("stbl.get", {{"sessionId", sid}, {"resourceId", rid}});
        if (st.value("ok", false) && st["data"].contains("entries")) {
            const auto& ents = st["data"]["entries"];
            QStringList lines;
            lines << tr("%1 strings").arg(ents.size());
            const int n = std::min(kPreviewRows, static_cast<int>(ents.size()));
            for (int i = 0; i < n; ++i) {
                const auto& e = ents[static_cast<std::size_t>(i)];
                lines << QStringLiteral("%1  %2")
                             .arg(hex64(e.value("id", 0ull)),
                                  clip_line(QString::fromStdString(e.value("text", "")), 96));
            }
            if (static_cast<int>(ents.size()) > n) {
                lines << QChar(0x2026);
            }
            show_preview_body(lines.join(QLatin1Char('\n')));
            return;
        }
    }

    if (pending_type_ == sxpe::resources::kNmap) {
        auto ng = bus_.execute("nmap.get", {{"sessionId", sid}, {"resourceId", rid}});
        if (ng.value("ok", false) && ng["data"].contains("entries")) {
            const auto& ents = ng["data"]["entries"];
            QStringList lines;
            lines << tr("%1 names").arg(ents.size());
            const int n = std::min(kPreviewRows, static_cast<int>(ents.size()));
            for (int i = 0; i < n; ++i) {
                const auto& e = ents[static_cast<std::size_t>(i)];
                lines << QStringLiteral("%1  %2")
                             .arg(hex64(e.value("instance", 0ull)),
                                  clip_line(QString::fromStdString(e.value("name", "")), 96));
            }
            if (static_cast<int>(ents.size()) > n) {
                lines << QChar(0x2026);
            }
            show_preview_body(lines.join(QLatin1Char('\n')));
            return;
        }
    }

    if (pending_type_ == sxpe::resources::kObjk) {
        auto info = bus_.execute("objk.get", {{"sessionId", sid}, {"resourceId", rid}});
        if (info.value("ok", false)) {
            const auto& d = info["data"];
            QStringList lines;
            lines << tr("OBJK version %1").arg(d.value("version", 0));
            lines << tr("%1 components").arg(d.contains("components") ? d["components"].size() : 0);
            if (d.contains("components")) {
                for (const auto& c : d["components"]) {
                    lines << QStringLiteral("  component %1").arg(hex32(c.get<std::uint32_t>()));
                }
            }
            if (d.contains("data")) {
                for (const auto& row : d["data"]) {
                    const auto key = QString::fromStdString(row.value("key", ""));
                    if (row.contains("text")) {
                        lines << key + QStringLiteral(" = ") +
                                     QString::fromStdString(row.value("text", ""));
                    } else {
                        lines << key + QStringLiteral(" = ") +
                                     QString::number(row.value("number", 0));
                    }
                }
            }
            lines << tr("Visibility %1").arg(d.value("visibility", 0));
            lines << tr("TGI count %1").arg(d.value("tgiCount", 0));
            show_preview_body(lines.join(QLatin1Char('\n')));
            return;
        }
    }

    if (pending_type_ == sxpe::resources::kVpxy) {
        auto info = bus_.execute("vpxy.get", {{"sessionId", sid}, {"resourceId", rid}});
        if (info.value("ok", false)) {
            const auto& d = info["data"];
            QStringList lines;
            lines << tr("VPXY version %1").arg(d.value("version", 0));
            lines << tr("%1 entries").arg(d.contains("entries") ? d["entries"].size() : 0);
            if (d.contains("entries")) {
                int i = 0;
                for (const auto& e : d["entries"]) {
                    lines << tr("  [%1] type %2 id %3")
                                 .arg(i++)
                                 .arg(e.value("type", 0))
                                 .arg(e.value("id", 0));
                }
            }
            if (d.contains("bbox") && d["bbox"].is_array() && d["bbox"].size() == 6) {
                lines << tr("BBox %1,%2,%3 .. %4,%5,%6")
                             .arg(d["bbox"][0].get<double>())
                             .arg(d["bbox"][1].get<double>())
                             .arg(d["bbox"][2].get<double>())
                             .arg(d["bbox"][3].get<double>())
                             .arg(d["bbox"][4].get<double>())
                             .arg(d["bbox"][5].get<double>());
            }
            if (d.value("modular", false)) {
                lines << tr("Modular");
            }
            lines << tr("TGI count %1").arg(d.value("tgiCount", 0));
            show_preview_body(lines.join(QLatin1Char('\n')));
            return;
        }
    }

    if (pending_type_ == sxpe::resources::kObjd) {
        auto info = bus_.execute("objd.get", {{"sessionId", sid}, {"resourceId", rid}});
        if (info.value("ok", false)) {
            const auto& d = info["data"];
            QStringList lines;
            lines << tr("OBJD version %1 (common %2)")
                         .arg(d.value("version", 0))
                         .arg(d.value("commonVersion", 0));
            lines << tr("Name GUID %1").arg(hex64(d.value("nameGuid", 0ull)));
            lines << tr("Desc GUID %1").arg(hex64(d.value("descGuid", 0ull)));
            const auto iname = QString::fromStdString(d.value("internalName", std::string()));
            if (!iname.isEmpty()) {
                lines << tr("Internal name: %1").arg(iname);
            }
            const auto inst = QString::fromStdString(d.value("instanceName", std::string()));
            if (!inst.isEmpty()) {
                lines << tr("Instance: %1").arg(inst);
            }
            lines << tr("Price %1").arg(d.value("price", 0.0));
            lines << tr("Thumb IID %1").arg(hex64(d.value("thumbIid", 0ull)));
            if (d.value("partial", false)) {
                lines << tr("(partial parse)");
            }
            show_preview_body(lines.join(QLatin1Char('\n')));
            return;
        }
    }

    if (pending_type_ == sxpe::resources::kCasp) {
        auto info = bus_.execute("casp.get", {{"sessionId", sid}, {"resourceId", rid}});
        if (info.value("ok", false)) {
            const auto& d = info["data"];
            QStringList lines;
            lines << tr("CASP version %1").arg(d.value("version", 0));
            const auto name = QString::fromStdString(d.value("name", std::string()));
            if (!name.isEmpty()) {
                lines << tr("Name: %1").arg(name);
            }
            const auto ctn = QString::fromStdString(d.value("clothingTypeName", std::string()));
            lines << tr("Clothing type %1%2")
                         .arg(d.value("clothingType", 0))
                         .arg(ctn.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(ctn));
            QStringList ages;
            if (d.contains("ages")) {
                for (const auto& a : d["ages"]) {
                    ages << QString::fromStdString(a.get<std::string>());
                }
            }
            lines << tr("Ages: %1").arg(ages.isEmpty() ? tr("(none)") : ages.join(QLatin1String(", ")));
            const auto spn = QString::fromStdString(d.value("speciesName", std::string()));
            lines << tr("Species %1%2")
                         .arg(d.value("species", 0))
                         .arg(spn.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(spn));
            QStringList genders;
            if (d.contains("genders")) {
                for (const auto& g : d["genders"]) {
                    genders << QString::fromStdString(g.get<std::string>());
                }
            }
            lines << tr("Gender: %1")
                         .arg(genders.isEmpty() ? tr("(none)") : genders.join(QLatin1String(", ")));
            lines << tr("Category flags 0x%1")
                         .arg(d.value("clothingCategory", 0u), 8, 16, QLatin1Char('0'));
            if (d.value("partial", false)) {
                lines << tr("(partial parse — public layout best-effort)");
            }
            show_preview_body(lines.join(QLatin1Char('\n')));
            return;
        }
    }

    if (pending_type_ == sxpe::resources::kClip) {
        auto info = bus_.execute("clip.info", {{"sessionId", sid}, {"resourceId", rid}});
        if (info.value("ok", false)) {
            const auto& d = info["data"];
            QStringList lines;
            lines << tr("CLIP version %1").arg(d.value("version", 0));
            lines << tr("Duration %1 s (%2 frames × %3)")
                         .arg(d.value("durationSeconds", 0.0), 0, 'f', 3)
                         .arg(d.value("frameCount", 0))
                         .arg(d.value("frameDuration", 0.0), 0, 'f', 6);
            const auto anim = QString::fromStdString(d.value("animName", std::string()));
            if (!anim.isEmpty()) {
                lines << tr("Anim: %1").arg(anim);
            }
            const auto src = QString::fromStdString(d.value("sourceFile", std::string()));
            if (!src.isEmpty()) {
                lines << tr("Source: %1").arg(src);
            }
            const auto actor = QString::fromStdString(d.value("actorName", std::string()));
            if (!actor.isEmpty()) {
                lines << tr("Actor: %1").arg(actor);
            }
            lines << tr("%1 tracks").arg(d.value("trackCount", 0));
            if (d.contains("trackHashes")) {
                int n = 0;
                for (const auto& h : d["trackHashes"]) {
                    if (n++ >= 12) {
                        lines << QChar(0x2026);
                        break;
                    }
                    lines << QStringLiteral("  hash %1").arg(hex32(h.get<std::uint32_t>()));
                }
            }
            if (d.value("partial", false)) {
                lines << tr("(partial parse)");
            }
            show_preview_body(lines.join(QLatin1Char('\n')));
            return;
        }
    }

    if (pending_type_ == sxpe::resources::kModl || pending_type_ == sxpe::resources::kMlod ||
        pending_type_ == sxpe::resources::kGeom) {
        auto info = bus_.execute("rcol.summary", {{"sessionId", sid}, {"resourceId", rid}});
        if (info.value("ok", false)) {
            const auto& d = info["data"];
            QStringList lines;
            lines << tr("RCOL version %1").arg(d.value("version", 0));
            lines << tr("%1 internal / %2 external")
                         .arg(d.value("internalCount", 0))
                         .arg(d.value("externalCount", 0));
            if (d.value("lodGroups", 0) > 0) {
                lines << tr("LOD groups %1").arg(d.value("lodGroups", 0));
            }
            if (d.value("totalVertices", 0) > 0 || d.value("totalFaces", 0) > 0) {
                lines << tr("Vertices %1 / faces %2")
                             .arg(d.value("totalVertices", 0))
                             .arg(d.value("totalFaces", 0));
            }
            if (d.contains("chunks")) {
                for (const auto& ch : d["chunks"]) {
                    const auto tag = QString::fromStdString(ch.value("tag", std::string()));
                    QString row = tag.isEmpty() ? hex32(ch.value("type", 0u)) : tag;
                    row += QStringLiteral("  %1 B").arg(ch.value("size", 0));
                    if (ch.value("groupCount", 0) > 0) {
                        row += tr("  groups %1").arg(ch.value("groupCount", 0));
                    }
                    if (ch.value("vertexCount", 0) > 0 || ch.value("faceCount", 0) > 0) {
                        row += tr("  v%1/f%2")
                                   .arg(ch.value("vertexCount", 0))
                                   .arg(ch.value("faceCount", 0));
                    }
                    lines << row;
                }
            }
            if (d.value("partial", false)) {
                lines << tr("(partial parse)");
            }
            show_preview_body(lines.join(QLatin1Char('\n')));
            return;
        }
    }

    if (pending_type_ == sxpe::resources::kS3sa) {
        auto info = bus_.execute("s3sa.info", {{"sessionId", sid}, {"resourceId", rid}});
        if (info.value("ok", false)) {
            const auto& d = info["data"];
            QStringList lines;
            lines << tr("Size: %1 bytes").arg(d.value("size", 0));
            if (d.value("parsed", false)) {
                lines << tr("S3SA version %1").arg(d.value("version", 0));
                lines << tr("Blocks: %1").arg(d.value("blockCount", 0));
                lines << (d.value("keyTableZero", false) ? tr("Key table: zeros (community)")
                                                         : tr("Key table: present"));
                lines << tr("Assembly: %1 bytes").arg(d.value("assemblyBytes", 0));
            }
            if (d.contains("peOffset")) {
                lines << tr("PE (MZ) at offset %1 (decrypted)").arg(d.value("peOffset", 0));
            } else {
                lines << tr("No MZ signature found");
            }
            const auto hint = QString::fromStdString(d.value("moduleHint", std::string()));
            if (!hint.isEmpty()) {
                lines << tr("Module: %1").arg(hint);
            }
            lines << tr("Import never LoadLibrarys this PE.");
            show_preview_body(lines.join(QLatin1Char('\n')));
            return;
        }
    }

    if (pending_type_ == sxpe::resources::kXml || pending_type_ == sxpe::resources::kItun) {
        auto env = bus_.execute("text.get", {{"sessionId", sid},
                                             {"resourceId", rid},
                                             {"maxBytes", kXmlBytes}});
        if (env.value("ok", false)) {
            const auto raw_s = env["data"].value("text", std::string());
            std::vector<char> raw(raw_s.begin(), raw_s.end());
            const QString decoded = decode_preview_bytes(raw);
            QString head;
            if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFF &&
                static_cast<unsigned char>(raw[1]) == 0xFE) {
                head = tr("UTF-16LE") + QLatin1Char('\n');
            } else if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFE &&
                       static_cast<unsigned char>(raw[1]) == 0xFF) {
                head = tr("UTF-16BE") + QLatin1Char('\n');
            } else if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
                       static_cast<unsigned char>(raw[1]) == 0xBB &&
                       static_cast<unsigned char>(raw[2]) == 0xBF) {
                head = tr("UTF-8 BOM") + QLatin1Char('\n');
            }
            show_preview_body(head + pretty_xml_excerpt(decoded, kXmlBytes));
            return;
        }
    }

    QTemporaryFile tmp(QDir::tempPath() + "/sxpe-prev-XXXXXX.bin");
    tmp.setAutoRemove(true);
    if (!tmp.open()) {
        return;
    }
    const auto path = tmp.fileName().toStdString();
    auto exp = bus_.execute("resource.export", {{"sessionId", sid},
                                                {"resourceId", rid},
                                                {"path", path},
                                                {"force", true}});
    if (!exp.value("ok", false)) {
        return;
    }
    std::ifstream f(path, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    const bool png = looks_png(raw);
    const bool jpeg = looks_jpeg(raw);
    const bool dds = looks_dds(raw);
    if (png || jpeg || !dds) {
        QImage img;
        if (img.loadFromData(reinterpret_cast<const uchar*>(raw.data()),
                             static_cast<int>(raw.size()))) {
            QPixmap pm = QPixmap::fromImage(img);
            if (pm.width() > 512 || pm.height() > 512) {
                pm = pm.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            show_preview_image(pm);
            return;
        }
        if (png || jpeg) {
            show_preview_body(png ? tr("PNG (decode failed)") : tr("JPEG (decode failed)"));
            return;
        }
    }

    QSettings st(QStringLiteral("SXPE"), QStringLiteral("SXPE"));
    const bool dds_on = st.value("preview/dds", true).toBool();
    std::vector<std::byte> bytes(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(raw[i]));
    }
    if (dds || sxpe::resources::parse_dds(bytes)) {
        auto inf = sxpe::resources::parse_dds(bytes);
        if (!dds_on) {
            if (inf) {
                show_preview_body(tr("%1×%2 %3\nDDS preview is off (Settings).")
                                      .arg(inf->width)
                                      .arg(inf->height)
                                      .arg(QString::fromStdString(inf->format)));
            } else {
                show_preview_body(tr("DDS preview is off (Settings)."));
            }
            return;
        }
        auto pix = sxpe::resources::decode_dds_rgba(bytes);
        if (!pix) {
            if (inf) {
                QString detail = QString::fromStdString(pix.error().message);
                if (inf->cubemap) {
                    detail = tr("cubemap refused (2D only)");
                } else if (inf->volume) {
                    detail = tr("volume/3D refused (2D only)");
                } else if (!inf->decode_supported) {
                    detail = tr("unsupported format (see docs/spec/dds.md)");
                }
                show_preview_body(tr("%1×%2 %3 — %4")
                                      .arg(inf->width)
                                      .arg(inf->height)
                                      .arg(QString::fromStdString(inf->format))
                                      .arg(detail));
            }
            return;
        }
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
        show_preview_image(pm);
        return;
    }

    const QString kind = sniff_kind(raw);
    const QString decoded = decode_preview_bytes(raw);
    QString body;
    if (!kind.isEmpty()) {
        body += kind;
        body += QLatin1Char('\n');
    }
    if (kind == QLatin1String("XML") || decoded.trimmed().startsWith(QLatin1Char('<'))) {
        if (!body.isEmpty()) {
            body += QLatin1Char('\n');
        }
        body += pretty_xml_excerpt(decoded, kXmlBytes);
    } else if (mostly_text(decoded)) {
        if (!body.isEmpty()) {
            body += QLatin1Char('\n');
        }
        body += decoded.left(kXmlBytes);
        if (decoded.size() > kXmlBytes) {
            body += QChar(0x2026);
        }
    } else if (!raw.empty()) {
        if (!body.isEmpty()) {
            body += QLatin1Char('\n');
        }
        body += hex_excerpt(raw, 256);
    }
    show_preview_body(body);
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

void Inspector::popup_image_menu(const QPoint& local) {
    auto* origin = qobject_cast<QWidget*>(sender());
    if (!origin) {
        origin = preview_;
    }
    QMenu m(this);
    const bool has = !preview_->pixmap().isNull();
    auto* copy = m.addAction(tr("&Copy"), this, [this] { copy_visible(); });
    copy->setEnabled(has);
    auto* save = m.addAction(tr("Save &As…"), this, [this] { save_image_as(); });
    save->setEnabled(has);
    m.exec(origin->mapToGlobal(local));
}

bool Inspector::save_image_as() {
    if (preview_->pixmap().isNull()) {
        return false;
    }
    QString ext = QStringLiteral("png");
    QString filter = tr("PNG (*.png);;JPEG (*.jpg *.jpeg);;DDS (*.dds);;All files (*.*)");
    if (sxpe::resources::is_dds_image(pending_type_)) {
        ext = QStringLiteral("dds");
        filter = tr("DDS (*.dds);;PNG (*.png);;JPEG (*.jpg *.jpeg);;All files (*.*)");
    } else if (pending_type_ == sxpe::resources::kImagJpeg) {
        ext = QStringLiteral("jpg");
        filter = tr("JPEG (*.jpg *.jpeg);;PNG (*.png);;DDS (*.dds);;All files (*.*)");
    }
    const auto type = pending_rid_.value("type", pending_type_);
    const auto group = pending_rid_.value("group", 0u);
    const auto inst = pending_rid_.value("instance", 0ull);
    const sxpe::games::sims3::Tgi tgi{type, group, inst};
    const auto suggested = QString::fromStdString(sxpe::games::sims3::community_filename(
        tgi, pending_name_.toStdString(), ext.toStdString()));
    QString selected;
    auto path = QFileDialog::getSaveFileName(this, tr("Save image"), suggested, filter, &selected);
    if (path.isEmpty()) {
        return false;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        if (selected.contains(QLatin1String("DDS"), Qt::CaseInsensitive)) {
            path += QStringLiteral(".dds");
        } else if (selected.contains(QLatin1String("JPEG"), Qt::CaseInsensitive)) {
            path += QStringLiteral(".jpg");
        } else {
            path += QStringLiteral(".png");
        }
    }
    if (!write_preview_image(path)) {
        QMessageBox::warning(this, tr("SXPE"), tr("Could not save image."));
        return false;
    }
    return true;
}

bool Inspector::write_preview_image(const QString& path) {
    if (path.isEmpty() || preview_->pixmap().isNull()) {
        return false;
    }
    const auto suffix = QFileInfo(path).suffix().toLower();
    auto save_pixmap = [&] { return preview_->pixmap().save(path); };
    if (session_.isEmpty() || pending_rid_.is_null()) {
        return save_pixmap();
    }
    QTemporaryFile tmp(QDir::tempPath() + "/sxpe-img-XXXXXX.bin");
    tmp.setAutoRemove(true);
    if (!tmp.open()) {
        return save_pixmap();
    }
    const auto tmp_path = tmp.fileName().toStdString();
    auto exp = bus_.execute("resource.export", {{"sessionId", session_.toStdString()},
                                                {"resourceId", pending_rid_},
                                                {"path", tmp_path},
                                                {"force", true}});
    if (!exp.value("ok", false)) {
        return save_pixmap();
    }
    std::ifstream f(tmp_path, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const bool png = looks_png(raw);
    const bool jpeg = looks_jpeg(raw);
    const bool dds = looks_dds(raw);
    const bool same = (suffix == QLatin1String("png") && png) ||
                      ((suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")) && jpeg) ||
                      (suffix == QLatin1String("dds") && dds);
    if (same) {
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        return out.write(raw.data(), static_cast<qint64>(raw.size())) ==
               static_cast<qint64>(raw.size());
    }
    QImage img;
    if (png || jpeg) {
        img.loadFromData(reinterpret_cast<const uchar*>(raw.data()), static_cast<int>(raw.size()));
    } else {
        std::vector<std::byte> bytes(raw.size());
        for (std::size_t i = 0; i < raw.size(); ++i) {
            bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(raw[i]));
        }
        auto pix = sxpe::resources::decode_dds_rgba(bytes);
        auto inf = sxpe::resources::parse_dds(bytes);
        if (pix && inf) {
            QImage decoded(reinterpret_cast<const uchar*>(pix->data()), static_cast<int>(inf->width),
                           static_cast<int>(inf->height), static_cast<int>(inf->width * 4),
                           QImage::Format_RGBA8888);
            img = decoded.copy();
        }
    }
    if (img.isNull()) {
        return save_pixmap();
    }
    return img.save(path);
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
        QString t = preview_card_->text();
        if (preview_body_->isVisible() && !preview_body_->toPlainText().isEmpty()) {
            if (!t.isEmpty()) {
                t += QLatin1String("\n\n");
            }
            t += preview_body_->toPlainText();
        }
        cb->setText(t);
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
        if (!preview_->pixmap().isNull()) {
            return write_preview_image(path);
        }
        QString t = preview_card_->text();
        if (preview_body_->isVisible() && !preview_body_->toPlainText().isEmpty()) {
            if (!t.isEmpty()) {
                t += QLatin1String("\n\n");
            }
            t += preview_body_->toPlainText();
        }
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        f.write(t.toUtf8());
        return true;
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
