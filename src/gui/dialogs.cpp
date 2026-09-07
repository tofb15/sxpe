#include "dialogs.hpp"

#include "sxpe/resources/png.hpp"
#include "sxpe/resources/types.hpp"

#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QColor>
#include <QImage>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QGuiApplication>
#include <QClipboard>
#include <QTextBrowser>

#include <algorithm>
#include <span>

namespace sxpe::gui {

void show_fnv_dialog(QWidget* parent, sxpe::commands::Bus& bus) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("FNV-1 / CLIP"));
    auto* form = new QFormLayout(&dlg);
    auto* text = new QLineEdit;
    auto* out32 = new QLineEdit;
    auto* out64 = new QLineEdit;
    auto* outclip = new QLineEdit;
    out32->setReadOnly(true);
    out64->setReadOnly(true);
    outclip->setReadOnly(true);
    form->addRow(QObject::tr("Text"), text);
    form->addRow(QObject::tr("FNV-1 32"), out32);
    form->addRow(QObject::tr("FNV-1 64"), out64);
    form->addRow(QObject::tr("CLIP"), outclip);
    auto run = [&] {
        const auto s = text->text().toStdString();
        auto a = bus.execute("hash.fnv", {{"text", s}, {"width", 32}});
        auto b = bus.execute("hash.fnv", {{"text", s}, {"width", 64}});
        auto c = bus.execute("hash.fnv", {{"text", s}, {"clip", true}});
        auto fmt = [](const nlohmann::json& e) {
            if (!e.value("ok", false)) {
                return QString();
            }
            return QString::number(e["data"].value("value", 0ull), 16).toUpper();
        };
        out32->setText(fmt(a));
        out64->setText(fmt(b));
        outclip->setText(fmt(c));
    };
    QObject::connect(text, &QLineEdit::textChanged, &dlg, run);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(box);
    dlg.exec();
}

void show_details_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                         std::uint32_t type_id, std::uint32_t group_id, std::uint64_t instance,
                         std::uint32_t ordinal, const QString& name_s, bool compressed,
                         bool deleted) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Resource details"));
    auto* form = new QFormLayout(&dlg);
    auto* type = new QLineEdit(QString("%1").arg(type_id, 8, 16, QLatin1Char('0')).toUpper());
    auto* group = new QLineEdit(QString("%1").arg(group_id, 8, 16, QLatin1Char('0')).toUpper());
    auto* inst =
        new QLineEdit(QString("%1").arg(instance, 16, 16, QLatin1Char('0')).toUpper());
    auto* name = new QLineEdit(name_s);
    auto* cmp = new QCheckBox(QObject::tr("Compressed"));
    auto* del = new QCheckBox(QObject::tr("Deleted"));
    cmp->setChecked(compressed);
    del->setChecked(deleted);
    form->addRow(QObject::tr("Type"), type);
    form->addRow(QObject::tr("Group"), group);
    form->addRow(QObject::tr("Instance"), inst);
    form->addRow(QObject::tr("Name"), name);
    form->addRow(cmp);
    form->addRow(del);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(box);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        nlohmann::json rid{{"type", type_id},
                           {"group", group_id},
                           {"instance", instance},
                           {"ordinal", ordinal}};
        bool ok = true;
        const auto nt = type->text().toUInt(nullptr, 16);
        const auto ng = group->text().toUInt(nullptr, 16);
        const auto ni = inst->text().toULongLong(nullptr, 16);
        if (nt != type_id || ng != group_id || ni != instance) {
            auto e = bus.execute("resource.rekey",
                                 {{"sessionId", session.toStdString()},
                                  {"resourceId", rid},
                                  {"newId", {{"type", nt}, {"group", ng}, {"instance", ni}}}});
            ok = e.value("ok", false);
            rid = {{"type", nt}, {"group", ng}, {"instance", ni}, {"ordinal", 0}};
        }
        auto f = bus.execute("resource.setFlags", {{"sessionId", session.toStdString()},
                                                   {"resourceId", rid},
                                                   {"compressed", cmp->isChecked()},
                                                   {"deleted", del->isChecked()}});
        ok = ok && f.value("ok", false);
        if (!name->text().isEmpty()) {
            auto nm = bus.execute("resource.rename",
                                  {{"sessionId", session.toStdString()},
                                   {"resourceId", rid},
                                   {"name", name->text().toStdString()}});
            ok = ok && nm.value("ok", false);
        }
        if (ok) {
            dlg.accept();
        } else {
            QMessageBox::warning(&dlg, QObject::tr("SXPE"), QObject::tr("Could not apply changes."));
        }
    });
    dlg.exec();
}

void show_search_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Search"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* text = new QLineEdit;
    text->setPlaceholderText(QObject::tr("Text or hex (prefix 0x)"));
    auto* out = new QPlainTextEdit;
    out->setReadOnly(true);
    auto* go = new QPushButton(QObject::tr("Search"));
    lay->addWidget(text);
    lay->addWidget(go);
    lay->addWidget(out);
    QObject::connect(go, &QPushButton::clicked, &dlg, [&] {
        nlohmann::json args{{"sessionId", session.toStdString()}, {"limit", 100}};
        const auto s = text->text().trimmed();
        if (s.startsWith("0x")) {
            args["hex"] = s.mid(2).toStdString();
        } else {
            args["text"] = s.toStdString();
        }
        auto env = bus.execute("search.bytes", args);
        out->setPlainText(QString::fromStdString(env.dump(2)));
    });
    dlg.resize(480, 320);
    dlg.exec();
}

void show_import_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                        bool dbc) {
    const auto paths = QFileDialog::getOpenFileNames(
        parent,
        dbc ? QObject::tr("Import DBC (Shift+click or Ctrl+click to select several)")
            : QObject::tr("Import packages (Shift+click or Ctrl+click to select several)"),
        {},
        dbc ? QObject::tr("DBC (*.dbc *.package);;All (*.*)")
            : QObject::tr("Packages (*.package *.dbc *.world *.nhd);;All (*.*)"));
    if (paths.isEmpty()) {
        return;
    }
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : paths) {
        arr.push_back(p.toStdString());
    }
    const char* cmd = dbc ? "resource.importDbc" : "resource.importPackage";
    auto env = bus.execute(cmd, {{"sessionId", session.toStdString()},
                                 {"paths", arr},
                                 {"force", true}});
    if (!env.value("ok", false)) {
        QMessageBox::warning(parent, QObject::tr("SXPE"),
                             QString::fromStdString(env["error"].value("message", env.dump())));
        return;
    }
    const auto imported = env["data"].value("imported", 0);
    const auto pkgs = env["data"].value("packages", 0);
    const auto failed = env["data"].value("failed", 0);
    QString msg = QObject::tr("Imported %1 resource(s) from %2 package(s).").arg(imported).arg(pkgs);
    if (failed > 0) {
        msg += QLatin1Char('\n') + QObject::tr("%1 file(s) failed.").arg(failed);
        QMessageBox::warning(parent, QObject::tr("SXPE"), msg);
    } else if (paths.size() > 1) {
        QMessageBox::information(parent, QObject::tr("SXPE"), msg);
    }
}

void show_handlers_dialog(QWidget* parent, sxpe::commands::Bus& bus) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Built-in handlers"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* list = new QListWidget;
    auto env = bus.execute("handler.list", nlohmann::json::object());
    if (env.value("ok", false)) {
        for (const auto& h : env["data"]["handlers"]) {
            list->addItem(QString("%1  %2")
                              .arg(QString::fromStdString(h.value("tag", "")))
                              .arg(QString::fromStdString(h.value("name", ""))));
        }
    }
    lay->addWidget(new QLabel(QObject::tr(
        "Compiled first-party type handlers (always on).")));
    lay->addWidget(list);
    lay->addWidget(new QLabel(QObject::tr(
        "Third-party GUI plugins are not supported in this build.")));
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(box);
    dlg.exec();
}

bool show_add_resource_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                              bool replace, std::uint32_t type, std::uint32_t group,
                              std::uint64_t instance, std::uint32_t ordinal,
                              const QString& file_filter) {
    QDialog dlg(parent);
    dlg.setWindowTitle(replace ? QObject::tr("Replace resource") : QObject::tr("Add resource"));
    auto* form = new QFormLayout(&dlg);
    auto* path = new QLineEdit;
    auto* browse = new QPushButton(QObject::tr("Browse…"));
    auto* path_row = new QWidget;
    auto* hl = new QHBoxLayout(path_row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->addWidget(path, 1);
    hl->addWidget(browse);
    auto* type_e = new QLineEdit(QString("%1").arg(type, 8, 16, QLatin1Char('0')).toUpper());
    auto* group_e = new QLineEdit(QString("%1").arg(group, 8, 16, QLatin1Char('0')).toUpper());
    auto* inst_e = new QLineEdit(QString("%1").arg(instance, 16, 16, QLatin1Char('0')).toUpper());
    if (replace) {
        type_e->setReadOnly(true);
        group_e->setReadOnly(true);
        inst_e->setReadOnly(true);
    }
    auto* compress = new QCheckBox(QObject::tr("Compress (RefPack)"));
    form->addRow(QObject::tr("File"), path_row);
    form->addRow(QObject::tr("Type"), type_e);
    form->addRow(QObject::tr("Group"), group_e);
    form->addRow(QObject::tr("Instance"), inst_e);
    form->addRow(compress);
    QObject::connect(browse, &QPushButton::clicked, &dlg, [&] {
        const auto p = QFileDialog::getOpenFileName(
            &dlg, QObject::tr("Resource file"), {},
            file_filter.isEmpty() ? QObject::tr("All files (*.*)") : file_filter);
        if (!p.isEmpty()) {
            path->setText(p);
        }
    });
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(box);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        if (path->text().isEmpty()) {
            QMessageBox::warning(&dlg, QObject::tr("SXPE"), QObject::tr("Choose a file."));
            return;
        }
        nlohmann::json args{{"sessionId", session.toStdString()},
                            {"path", path->text().toStdString()},
                            {"force", true},
                            {"compress", compress->isChecked()}};
        args["resourceId"] = {{"type", type_e->text().toUInt(nullptr, 16)},
                              {"group", group_e->text().toUInt(nullptr, 16)},
                              {"instance", inst_e->text().toULongLong(nullptr, 16)},
                              {"ordinal", ordinal}};
        auto env = bus.execute("resource.importFiles", args);
        if (!env.value("ok", false)) {
            QMessageBox::warning(&dlg, QObject::tr("SXPE"),
                                 QString::fromStdString(env["error"].value("message", env.dump())));
            return;
        }
        dlg.accept();
    });
    return dlg.exec() == QDialog::Accepted;
}

bool show_stbl_editor(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                      std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                      std::uint32_t ordinal) {
    nlohmann::json rid{{"type", type}, {"group", group}, {"instance", instance}, {"ordinal", ordinal}};
    auto got = bus.execute("stbl.get", {{"sessionId", session.toStdString()}, {"resourceId", rid}});
    if (!got.value("ok", false)) {
        QMessageBox::warning(parent, QObject::tr("SXPE"),
                             QObject::tr("This resource is not a string table (STBL)."));
        return false;
    }
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("String table"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* table = new QTableWidget(0, 2);
    table->setHorizontalHeaderLabels({QObject::tr("Id (hex)"), QObject::tr("Text")});
    table->horizontalHeader()->setStretchLastSection(true);
    for (const auto& e : got["data"]["entries"]) {
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0,
                       new QTableWidgetItem(QString("%1").arg(e.value("id", 0ull), 16, 16,
                                                              QLatin1Char('0')).toUpper()));
        table->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(e.value("text", ""))));
    }
    auto* btns = new QHBoxLayout;
    auto* add = new QPushButton(QObject::tr("Add"));
    auto* del = new QPushButton(QObject::tr("Delete"));
    btns->addWidget(add);
    btns->addWidget(del);
    btns->addStretch();
    QObject::connect(add, &QPushButton::clicked, &dlg, [table] {
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(QStringLiteral("0000000000000000")));
        table->setItem(row, 1, new QTableWidgetItem());
    });
    QObject::connect(del, &QPushButton::clicked, &dlg, [table] {
        table->removeRow(table->currentRow());
    });
    lay->addLayout(btns);
    lay->addWidget(table, 1);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    lay->addWidget(box);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        std::vector<std::uint64_t> keep;
        for (int row = 0; row < table->rowCount(); ++row) {
            const auto id_s = table->item(row, 0) ? table->item(row, 0)->text() : QString();
            const auto text = table->item(row, 1) ? table->item(row, 1)->text() : QString();
            const auto id = id_s.toULongLong(nullptr, 16);
            keep.push_back(id);
            auto env = bus.execute("stbl.set", {{"sessionId", session.toStdString()},
                                                {"resourceId", rid},
                                                {"id", id},
                                                {"text", text.toStdString()}});
            if (!env.value("ok", false)) {
                QMessageBox::warning(&dlg, QObject::tr("SXPE"),
                                     QString::fromStdString(env["error"].value("message", "")));
                return;
            }
        }
        for (const auto& e : got["data"]["entries"]) {
            const auto id = e.value("id", 0ull);
            if (std::find(keep.begin(), keep.end(), id) == keep.end()) {
                bus.execute("stbl.delete", {{"sessionId", session.toStdString()},
                                            {"resourceId", rid},
                                            {"id", id}});
            }
        }
        dlg.accept();
    });
    dlg.resize(640, 420);
    return dlg.exec() == QDialog::Accepted;
}

bool show_xml_editor(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                     std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                     std::uint32_t ordinal) {
    nlohmann::json rid{{"type", type}, {"group", group}, {"instance", instance}, {"ordinal", ordinal}};
    auto got = bus.execute("xml.get", {{"sessionId", session.toStdString()}, {"resourceId", rid}});
    if (!got.value("ok", false)) {
        QMessageBox::warning(
            parent, QObject::tr("SXPE"),
            QString::fromStdString(got.contains("error")
                                       ? got["error"].value("message", "Not an XML / ITUN resource.")
                                       : "Not an XML / ITUN resource."));
        return false;
    }
    QDialog dlg(parent);
    const auto tag = QString::fromStdString(got["data"].value("tag", std::string{"XML"}));
    const auto encoding = QString::fromStdString(got["data"].value("encoding", std::string{"utf-8"}));
    dlg.setWindowTitle(QObject::tr("XML editor — %1 (%2)").arg(tag, encoding));
    auto* lay = new QVBoxLayout(&dlg);
    auto* info = new QLabel(QObject::tr("Encoding on save: %1 (sniffed; write preserves UTF-8 / UTF-16).")
                                .arg(encoding));
    info->setWordWrap(true);
    lay->addWidget(info);
    auto* edit = new QPlainTextEdit;
    edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    edit->setPlainText(QString::fromStdString(got["data"].value("text", std::string{})));
    lay->addWidget(edit, 1);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    lay->addWidget(box);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        auto env = bus.execute("xml.set", {{"sessionId", session.toStdString()},
                                           {"resourceId", rid},
                                           {"text", edit->toPlainText().toStdString()},
                                           {"encoding", encoding.toStdString()}});
        if (!env.value("ok", false)) {
            QMessageBox::warning(&dlg, QObject::tr("SXPE"),
                                 QString::fromStdString(env["error"].value("message", "")));
            return;
        }
        dlg.accept();
    });
    dlg.resize(720, 520);
    return dlg.exec() == QDialog::Accepted;
}

bool show_clip_export_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                             std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                             std::uint32_t ordinal) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("CLIP export as new name"));
    auto* form = new QFormLayout(&dlg);
    auto* name = new QLineEdit;
    form->addRow(QObject::tr("New clip name"), name);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(box);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        if (name->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dlg, QObject::tr("SXPE"), QObject::tr("Enter a clip name."));
            return;
        }
        nlohmann::json rid{{"type", type},
                           {"group", group},
                           {"instance", instance},
                           {"ordinal", ordinal}};
        auto env = bus.execute("clip.exportAs", {{"sessionId", session.toStdString()},
                                                 {"resourceId", rid},
                                                 {"name", name->text().toStdString()}});
        if (!env.value("ok", false)) {
            QMessageBox::warning(&dlg, QObject::tr("SXPE"),
                                 QString::fromStdString(env["error"].value("message", "")));
            return;
        }
        dlg.accept();
    });
    return dlg.exec() == QDialog::Accepted;
}

namespace {

void posterize_rgb(QImage* im, int shift) {
    if (!im || shift <= 0) {
        return;
    }
    im->detach();
    for (int y = 0; y < im->height(); ++y) {
        auto* p = reinterpret_cast<QRgb*>(im->scanLine(y));
        for (int x = 0; x < im->width(); ++x) {
            const int r = (qRed(p[x]) >> shift) << shift;
            const int g = (qGreen(p[x]) >> shift) << shift;
            const int b = (qBlue(p[x]) >> shift) << shift;
            p[x] = qRgba(r, g, b, qAlpha(p[x]));
        }
    }
}

quint32 png_crc(const QByteArray& data) {
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[n] = c;
        }
        ready = true;
    }
    quint32 c = 0xFFFFFFFFu;
    for (unsigned char b : data) {
        c = table[(c ^ b) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

void png_be32(QByteArray* a, quint32 v) {
    a->append(static_cast<char>((v >> 24) & 0xFF));
    a->append(static_cast<char>((v >> 16) & 0xFF));
    a->append(static_cast<char>((v >> 8) & 0xFF));
    a->append(static_cast<char>(v & 0xFF));
}

void png_chunk(QByteArray* png, const char type[4], const QByteArray& data) {
    png_be32(png, static_cast<quint32>(data.size()));
    QByteArray td;
    td.append(type, 4);
    td += data;
    *png += td;
    png_be32(png, png_crc(td));
}

QByteArray encode_game_png(const QImage& im) {
    const QImage rgba = im.convertToFormat(QImage::Format_RGBA8888);
    const int w = rgba.width();
    const int h = rgba.height();
    QByteArray raw;
    raw.reserve((w * 4 + 1) * h);
    for (int y = 0; y < h; ++y) {
        raw.append('\0');
        raw.append(reinterpret_cast<const char*>(rgba.constScanLine(y)), w * 4);
    }
    QByteArray z = qCompress(raw, 9);
    if (z.size() < 6) {
        return {};
    }
    z = z.mid(4);
    QByteArray png;
    png.append("\x89PNG\r\n\x1a\n", 8);
    QByteArray ihdr;
    png_be32(&ihdr, static_cast<quint32>(w));
    png_be32(&ihdr, static_cast<quint32>(h));
    ihdr.append('\x08');
    ihdr.append('\x06');
    ihdr.append('\0');
    ihdr.append('\0');
    ihdr.append('\0');
    png_chunk(&png, "IHDR", ihdr);
    png_chunk(&png, "IDAT", z);
    png_chunk(&png, "IEND", {});
    return png;
}

}  // namespace

bool show_replace_snap_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                              std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                              std::uint32_t ordinal, std::uint32_t max_bytes) {
    nlohmann::json rid{{"type", type}, {"group", group}, {"instance", instance}, {"ordinal", ordinal}};
    const auto path = QFileDialog::getOpenFileName(
        parent, QObject::tr("Replace SNAP with PNG"), {},
        QObject::tr("PNG (*.png);;All files (*.*)"));
    if (path.isEmpty()) {
        return false;
    }
    int tw = 128;
    int th = 128;
    const auto cur = QDir::temp().filePath(QStringLiteral("sxpe-snap-cur.png"));
    auto exp = bus.execute("resource.export", {{"sessionId", session.toStdString()},
                                               {"resourceId", rid},
                                               {"path", cur.toStdString()},
                                               {"force", true}});
    if (exp.value("ok", false)) {
        QImage orig(cur);
        if (!orig.isNull() && orig.width() > 0 && orig.height() > 0) {
            tw = orig.width();
            th = orig.height();
        }
        QFile::remove(cur);
    }
    if (max_bytes < 256) {
        max_bytes = 256;
    }
    QImage im(path);
    if (im.isNull()) {
        QMessageBox::warning(parent, QObject::tr("SXPE"), QObject::tr("Could not read that image."));
        return false;
    }
    im = im.convertToFormat(QImage::Format_RGBA8888);
    if (im.width() != tw || im.height() != th) {
        im = im.scaled(tw, th, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    QByteArray fitted;
    QFile srcf(path);
    if (srcf.open(QIODevice::ReadOnly)) {
        const QByteArray raw = srcf.readAll();
        srcf.close();
        const auto sp = std::span<const std::byte>(reinterpret_cast<const std::byte*>(raw.constData()),
                                                   static_cast<std::size_t>(raw.size()));
        if (sxpe::resources::png_is_game_snap(sp)) {
            if (auto ih = sxpe::resources::parse_png_ihdr(sp);
                ih && ih->width == static_cast<std::uint32_t>(tw) &&
                ih->height == static_cast<std::uint32_t>(th) && ih->bit_depth == 8 &&
                ih->color_type == 6 && static_cast<std::uint32_t>(raw.size()) <= max_bytes) {
                fitted = raw;
            }
        }
    }
    if (fitted.isEmpty()) {
        for (int shift = 0; shift <= 5; ++shift) {
            QImage q = im;
            posterize_rgb(&q, shift);
            fitted = encode_game_png(q);
            const auto sp =
                std::span<const std::byte>(reinterpret_cast<const std::byte*>(fitted.constData()),
                                           static_cast<std::size_t>(fitted.size()));
            if (!fitted.isEmpty() && sxpe::resources::png_is_game_snap(sp) &&
                static_cast<std::uint32_t>(fitted.size()) <= max_bytes) {
                QImage check;
                if (check.loadFromData(fitted, "PNG") && check.width() == tw &&
                    check.height() == th) {
                    break;
                }
            }
            fitted.clear();
        }
    }
    if (fitted.isEmpty()) {
        QMessageBox::warning(
            parent, QObject::tr("SXPE"),
            QObject::tr("Could not compress the image to %1 bytes (the original SNAP size). "
                        "Use a simpler 128×128 PNG.")
                .arg(max_bytes));
        return false;
    }
    // Do not pad zeros after IEND. The game reads file_size bytes as a PNG;
    // trailing garbage after IEND makes the neighborhood refuse to load.
    // File-Save writes this exact PNG and updates that one index size.
    const auto fitted_path = QDir::temp().filePath(QStringLiteral("sxpe-snap-fit.bin"));
    QFile out(fitted_path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        out.write(fitted) != fitted.size()) {
        QMessageBox::warning(parent, QObject::tr("SXPE"), QObject::tr("Could not write a temp PNG."));
        return false;
    }
    out.close();
    auto env = bus.execute("resource.importFiles",
                           {{"sessionId", session.toStdString()},
                            {"resourceId", rid},
                            {"path", fitted_path.toStdString()},
                            {"force", true},
                            {"compress", false}});
    QFile::remove(fitted_path);
    if (!env.value("ok", false)) {
        QString msg = QObject::tr("Could not stage SNAP replace.");
        if (env.contains("error") && env["error"].contains("message")) {
            msg = QString::fromStdString(env["error"]["message"].get<std::string>());
        }
        QMessageBox::warning(parent, QObject::tr("SXPE"), msg);
        return false;
    }
    return true;
}

void show_bookmarks_dialog(QWidget* parent, QStringList* bookmarks) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Bookmarks"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* list = new QListWidget;
    list->addItems(*bookmarks);
    auto* btns = new QHBoxLayout;
    auto* add = new QPushButton(QObject::tr("Add…"));
    auto* del = new QPushButton(QObject::tr("Remove"));
    btns->addWidget(add);
    btns->addWidget(del);
    btns->addStretch();
    QObject::connect(add, &QPushButton::clicked, &dlg, [list, &dlg] {
        const auto p = QFileDialog::getOpenFileName(
            &dlg, QObject::tr("Bookmark package"), {},
            QObject::tr("Sims 3 packages (*.package *.world *.dbc *.nhd);;All files (*.*)"));
        if (!p.isEmpty()) {
            list->addItem(p);
        }
    });
    QObject::connect(del, &QPushButton::clicked, &dlg, [list] {
        delete list->takeItem(list->currentRow());
    });
    lay->addWidget(list, 1);
    lay->addLayout(btns);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    lay->addWidget(box);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        bookmarks->clear();
        for (int i = 0; i < list->count(); ++i) {
            bookmarks->push_back(list->item(i)->text());
        }
        dlg.accept();
    });
    dlg.exec();
}

void show_external_programs_dialog(QWidget* parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("External programs"));
    auto* form = new QFormLayout(&dlg);
    QSettings s("SXPE", "SXPE");
    auto* hex = new QLineEdit(s.value("ext/hex").toString());
    auto* text = new QLineEdit(s.value("ext/text").toString());
    hex->setPlaceholderText(QObject::tr("e.g. C:\\Tools\\hex.exe {path}"));
    text->setPlaceholderText(QObject::tr("e.g. notepad {path}"));
    form->addRow(QObject::tr("Hex editor"), hex);
    form->addRow(QObject::tr("Text editor"), text);
    form->addRow(new QLabel(QObject::tr("{path} is replaced with the exported file.")));
    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(box);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        s.setValue("ext/hex", hex->text());
        s.setValue("ext/text", text->text());
        dlg.accept();
    });
    dlg.exec();
}

void show_contents_dialog(QWidget* parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Contents"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* view = new QTextBrowser;
    view->setOpenExternalLinks(true);
    view->setHtml(QObject::tr(
        "<h2>SXPE</h2>"
        "<p>SXPE edits Sims 3 DBPF packages (.package, .world, .dbc, .nhd).</p>"
        "<h3>File</h3>"
        "<p>New, open (read-write or read-only), save / save as / save copy as, close, "
        "recent files, bookmarks, exit.</p>"
        "<ul>"
        "<li><b>New</b> — Ctrl+N</li>"
        "<li><b>Open…</b> — Ctrl+O</li>"
        "<li><b>Save</b> — Ctrl+S</li>"
        "<li><b>Save As…</b> — Ctrl+Shift+S</li>"
        "<li><b>Close</b> — Ctrl+W</li>"
        "<li><b>Exit</b> — Ctrl+Q</li>"
        "</ul>"
        "<h3>Edit</h3>"
        "<p>Undo/redo, copy/save/float preview, open in text editor, select all, command palette.</p>"
        "<ul>"
        "<li><b>Undo</b> — Ctrl+Z</li>"
        "<li><b>Redo</b> — Ctrl+Y / Ctrl+Shift+Z</li>"
        "<li><b>Select All</b> — Ctrl+A</li>"
        "<li><b>Command palette…</b> — Ctrl+K</li>"
        "</ul>"
        "<h3>View</h3>"
        "<p>Show or hide resource-list columns (same as right-clicking column headers). "
        "Autofit and reset widths are available from the header menu. The last visible "
        "column cannot be hidden.</p>"
        "<h3>Resource</h3>"
        "<p>Add, copy, paste, duplicate, replace; compression and deleted flags; details; "
        "copy TGI key; import/export (file, package, DBC); typed editors (STBL, XML/ITUN, S3SA DLL, "
        "CLIP, DDS, SNAP PNG, VID); open in hex/text editor; delete.</p>"
        "<ul>"
        "<li><b>Add…</b> — Ctrl+I</li>"
        "<li><b>Copy</b> — Ctrl+C</li>"
        "<li><b>Paste</b> — Ctrl+V</li>"
        "<li><b>Duplicate</b> — Ctrl+D</li>"
        "<li><b>Copy resource key</b> — Ctrl+Shift+C</li>"
        "<li><b>Delete</b> — Delete</li>"
        "</ul>"
        "<h3>Tools</h3>"
        "<p>FNV-1 / CLIP hash, compare packages, un-merge package, byte search, validate, compact / save.</p>"
        "<ul>"
        "<li><b>Search…</b> — Ctrl+F</li>"
        "</ul>"
        "<h3>Settings</h3>"
        "<p>Preview toggles (DDS / text / hex), DBC import checkpoint, bookmarks, "
        "built-in handlers, external programs, save settings.</p>"
        "<h3>Help</h3>"
        "<p>Contents (this window), About, Warranty, Licence.</p>"
        "<h3>Context menus</h3>"
        "<p>Right-click the resource list for Resource actions. Right-click a package tab "
        "to save, close (this / others / left / right), or bookmark. Right-click column "
        "headers to show or hide columns.</p>"));
    lay->addWidget(view, 1);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(box);
    dlg.resize(720, 560);
    dlg.exec();
}

void show_validate_dialog(QWidget* parent, const nlohmann::json& envelope) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Validate"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* summary = new QPlainTextEdit;
    summary->setReadOnly(true);
    summary->setLineWrapMode(QPlainTextEdit::WidgetWidth);

    QStringList lines;
    if (!envelope.value("ok", false)) {
        QString msg = QObject::tr("Command failed.");
        if (envelope.contains("error") && envelope["error"].is_object()) {
            const auto& err = envelope["error"];
            msg = QString::fromStdString(err.value("message", msg.toStdString()));
            const auto code = err.value("code", std::string{});
            if (!code.empty()) {
                lines << QObject::tr("Error: %1 (%2)")
                             .arg(msg, QString::fromStdString(code));
            } else {
                lines << QObject::tr("Error: %1").arg(msg);
            }
        } else {
            lines << msg;
        }
    } else {
        const auto& data = envelope.contains("data") && envelope["data"].is_object()
                               ? envelope["data"]
                               : envelope;
        if (data.contains("summary") && data["summary"].is_array() && !data["summary"].empty()) {
            for (const auto& line : data["summary"]) {
                if (line.is_string()) {
                    lines << QString::fromStdString(line.get<std::string>());
                } else {
                    lines << QString::fromStdString(line.dump());
                }
            }
        } else {
            const bool valid = data.value("ok", false);
            lines << (valid ? QObject::tr("Result: OK — no issues found.")
                            : QObject::tr("Result: issues found."));
            if (data.contains("indexCount")) {
                lines << QObject::tr("Resources (index): %1")
                             .arg(static_cast<qulonglong>(data.value("indexCount", 0ull)));
            }
            if (data.contains("dir") && data["dir"].is_object()) {
                const auto& dir = data["dir"];
                if (!dir.value("present", false)) {
                    lines << QObject::tr("DIR: not present");
                } else {
                    lines << QObject::tr("DIR: present");
                    if (dir.contains("records")) {
                        lines << QObject::tr("  Records: %1")
                                     .arg(static_cast<qulonglong>(dir.value("records", 0ull)));
                    }
                    if (dir.contains("recordBytes")) {
                        lines << QObject::tr("  Record size: %1 bytes")
                                     .arg(static_cast<qulonglong>(dir.value("recordBytes", 0ull)));
                    }
                    if (dir.contains("unmatched")) {
                        lines << QObject::tr("  Unmatched: %1")
                                     .arg(static_cast<qulonglong>(dir.value("unmatched", 0ull)));
                    }
                }
            }
            if (data.contains("issues") && data["issues"].is_array()) {
                const auto& issues = data["issues"];
                if (issues.empty()) {
                    lines << QObject::tr("Issues: none");
                } else {
                    lines << QObject::tr("Issues (%1):").arg(static_cast<int>(issues.size()));
                    for (const auto& issue : issues) {
                        if (issue.is_string()) {
                            lines << QStringLiteral("  • %1")
                                         .arg(QString::fromStdString(issue.get<std::string>()));
                        } else {
                            lines << QStringLiteral("  • %1")
                                         .arg(QString::fromStdString(issue.dump()));
                        }
                    }
                }
            }
        }
        for (const char* key : {"errors", "warnings"}) {
            if (!data.contains(key) || !data[key].is_array()) {
                continue;
            }
            const auto& arr = data[key];
            const auto title = QString::fromUtf8(key);
            lines << QObject::tr("%1 (%2):").arg(title).arg(static_cast<int>(arr.size()));
            for (const auto& item : arr) {
                if (item.is_string()) {
                    lines << QStringLiteral("  • %1")
                                 .arg(QString::fromStdString(item.get<std::string>()));
                } else {
                    lines << QStringLiteral("  • %1").arg(QString::fromStdString(item.dump()));
                }
            }
        }
    }
    summary->setPlainText(lines.join(QLatin1Char('\n')));
    lay->addWidget(summary, 1);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* copy = box->addButton(QObject::tr("Copy JSON"), QDialogButtonBox::ActionRole);
    QObject::connect(copy, &QPushButton::clicked, &dlg, [envelope] {
        if (auto* cb = QGuiApplication::clipboard()) {
            cb->setText(QString::fromStdString(envelope.dump(2)));
        }
    });
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(box);
    dlg.resize(520, 360);
    dlg.exec();
}

void show_package_diff_dialog(
    QWidget* parent, sxpe::commands::Bus& bus,
    const std::function<void(const QString& path, std::uint32_t type, std::uint32_t group,
                             std::uint64_t instance, std::uint32_t ordinal)>& open_hit) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Compare packages"));
    auto* lay = new QVBoxLayout(&dlg);

    auto* form = new QFormLayout;
    auto* path_a = new QLineEdit;
    auto* path_b = new QLineEdit;
    auto* browse_a = new QPushButton(QObject::tr("Browse…"));
    auto* browse_b = new QPushButton(QObject::tr("Browse…"));
    auto* row_a = new QHBoxLayout;
    row_a->addWidget(path_a, 1);
    row_a->addWidget(browse_a);
    auto* row_b = new QHBoxLayout;
    row_b->addWidget(path_b, 1);
    row_b->addWidget(browse_b);
    form->addRow(QObject::tr("Package A"), row_a);
    form->addRow(QObject::tr("Package B"), row_b);
    lay->addLayout(form);

    auto* summary = new QPlainTextEdit;
    summary->setReadOnly(true);
    summary->setMaximumHeight(120);
    lay->addWidget(summary);

    auto* table = new QTableWidget(0, 6);
    table->setHorizontalHeaderLabels({QObject::tr("Side"), QObject::tr("Type"),
                                      QObject::tr("Group"), QObject::tr("Instance"),
                                      QObject::tr("Ord"), QObject::tr("Detail")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    lay->addWidget(table, 1);

    auto* hint = new QLabel(
        QObject::tr("Double-click a row (or Open selected) to open that package and select the "
                    "resource. Payload equality uses SHA-256 of uncompressed bytes."));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    nlohmann::json last_env = nlohmann::json::object();
    QString last_a;
    QString last_b;

    auto fill = [&](const nlohmann::json& env) {
        last_env = env;
        table->setRowCount(0);
        QStringList lines;
        if (!env.value("ok", false)) {
            QString msg = QObject::tr("Command failed.");
            if (env.contains("error") && env["error"].is_object()) {
                msg = QString::fromStdString(env["error"].value("message", msg.toStdString()));
            }
            lines << msg;
            summary->setPlainText(lines.join(QLatin1Char('\n')));
            return;
        }
        const auto& data = env["data"];
        last_a = QString::fromStdString(data.value("pathA", std::string{}));
        last_b = QString::fromStdString(data.value("pathB", std::string{}));
        if (data.contains("summary") && data["summary"].is_array()) {
            for (const auto& line : data["summary"]) {
                if (line.is_string()) {
                    lines << QString::fromStdString(line.get<std::string>());
                }
            }
        }
        summary->setPlainText(lines.join(QLatin1Char('\n')));

        auto add_rows = [&](const char* side, const nlohmann::json& arr, const char* detail_key) {
            if (!arr.is_array()) {
                return;
            }
            for (const auto& it : arr) {
                const int r = table->rowCount();
                table->insertRow(r);
                auto put = [&](int c, const QString& s) {
                    auto* item = new QTableWidgetItem(s);
                    item->setData(Qt::UserRole, QString::fromStdString(side));
                    item->setData(Qt::UserRole + 1, static_cast<qulonglong>(it.value("type", 0u)));
                    item->setData(Qt::UserRole + 2, static_cast<qulonglong>(it.value("group", 0u)));
                    item->setData(Qt::UserRole + 3,
                                  static_cast<qulonglong>(it.value("instance", 0ull)));
                    item->setData(Qt::UserRole + 4, static_cast<qulonglong>(it.value("ordinal", 0u)));
                    table->setItem(r, c, item);
                };
                put(0, QString::fromUtf8(side));
                put(1, QString::fromStdString(it.value("typeHex", std::to_string(it.value("type", 0u)))));
                put(2, QString::fromStdString(it.value("groupHex", std::to_string(it.value("group", 0u)))));
                put(3, QString::fromStdString(
                           it.value("instanceHex", std::to_string(it.value("instance", 0ull)))));
                put(4, QString::number(static_cast<qulonglong>(it.value("ordinal", 0u))));
                QString detail;
                if (std::string(detail_key) == "different") {
                    detail = QObject::tr("payload differs");
                    if (it.contains("hashA") && it.contains("hashB")) {
                        const auto ha = it.value("hashA", std::string{});
                        const auto hb = it.value("hashB", std::string{});
                        detail += QLatin1String("  A=") +
                                  QString::fromStdString(ha.substr(0, std::min<std::size_t>(12, ha.size()))) +
                                  QLatin1String("… B=") +
                                  QString::fromStdString(hb.substr(0, std::min<std::size_t>(12, hb.size()))) +
                                  QStringLiteral("…");
                    }
                } else if (it.contains("hash")) {
                    const auto h = it.value("hash", std::string{});
                    detail = QString::fromStdString(h.substr(0, std::min<std::size_t>(16, h.size()))) +
                             QStringLiteral("…");
                }
                put(5, detail);
            }
        };
        add_rows("A only", data.value("onlyInA", nlohmann::json::array()), "onlyA");
        add_rows("B only", data.value("onlyInB", nlohmann::json::array()), "onlyB");
        add_rows("Different", data.value("different", nlohmann::json::array()), "different");
    };

    auto run_diff = [&] {
        const auto a = path_a->text().trimmed();
        const auto b = path_b->text().trimmed();
        if (a.isEmpty() || b.isEmpty()) {
            QMessageBox::warning(&dlg, QObject::tr("Compare packages"),
                                 QObject::tr("Choose both package A and package B."));
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        auto env = bus.execute("package.diff",
                               {{"pathA", a.toStdString()}, {"pathB", b.toStdString()}});
        QApplication::restoreOverrideCursor();
        fill(env);
    };

    auto open_selected = [&] {
        const auto rows = table->selectionModel() ? table->selectionModel()->selectedRows()
                                                  : QModelIndexList{};
        if (rows.isEmpty()) {
            return;
        }
        const auto* item = table->item(rows.first().row(), 0);
        if (!item || !open_hit) {
            return;
        }
        const auto side = item->data(Qt::UserRole).toString();
        QString path = last_a;
        if (side.startsWith(QLatin1String("B"))) {
            path = last_b;
        }
        // "Different" → open A by default (user can open B via context if needed)
        if (side.startsWith(QLatin1String("Different"))) {
            path = last_a;
        }
        open_hit(path, static_cast<std::uint32_t>(item->data(Qt::UserRole + 1).toULongLong()),
                 static_cast<std::uint32_t>(item->data(Qt::UserRole + 2).toULongLong()),
                 static_cast<std::uint64_t>(item->data(Qt::UserRole + 3).toULongLong()),
                 static_cast<std::uint32_t>(item->data(Qt::UserRole + 4).toULongLong()));
    };

    QObject::connect(browse_a, &QPushButton::clicked, &dlg, [path_a, &dlg] {
        const auto p = QFileDialog::getOpenFileName(
            &dlg, QObject::tr("Package A"), path_a->text(),
            QObject::tr("Packages (*.package);;All files (*.*)"));
        if (!p.isEmpty()) {
            path_a->setText(p);
        }
    });
    QObject::connect(browse_b, &QPushButton::clicked, &dlg, [path_b, &dlg] {
        const auto p = QFileDialog::getOpenFileName(
            &dlg, QObject::tr("Package B"), path_b->text(),
            QObject::tr("Packages (*.package);;All files (*.*)"));
        if (!p.isEmpty()) {
            path_b->setText(p);
        }
    });
    QObject::connect(table, &QTableWidget::cellDoubleClicked, &dlg, [&](int, int) { open_selected(); });

    auto* box = new QDialogButtonBox;
    auto* compare = box->addButton(QObject::tr("Compare"), QDialogButtonBox::ActionRole);
    auto* open_a = box->addButton(QObject::tr("Open selected in A"), QDialogButtonBox::ActionRole);
    auto* open_b = box->addButton(QObject::tr("Open selected in B"), QDialogButtonBox::ActionRole);
    auto* copy = box->addButton(QObject::tr("Copy JSON"), QDialogButtonBox::ActionRole);
    box->addButton(QDialogButtonBox::Close);
    QObject::connect(compare, &QPushButton::clicked, &dlg, run_diff);
    QObject::connect(open_a, &QPushButton::clicked, &dlg, [&] {
        const auto rows = table->selectionModel() ? table->selectionModel()->selectedRows()
                                                  : QModelIndexList{};
        if (rows.isEmpty() || !open_hit) {
            return;
        }
        const auto* item = table->item(rows.first().row(), 0);
        if (!item) {
            return;
        }
        open_hit(last_a, static_cast<std::uint32_t>(item->data(Qt::UserRole + 1).toULongLong()),
                 static_cast<std::uint32_t>(item->data(Qt::UserRole + 2).toULongLong()),
                 static_cast<std::uint64_t>(item->data(Qt::UserRole + 3).toULongLong()),
                 static_cast<std::uint32_t>(item->data(Qt::UserRole + 4).toULongLong()));
    });
    QObject::connect(open_b, &QPushButton::clicked, &dlg, [&] {
        const auto rows = table->selectionModel() ? table->selectionModel()->selectedRows()
                                                  : QModelIndexList{};
        if (rows.isEmpty() || !open_hit) {
            return;
        }
        const auto* item = table->item(rows.first().row(), 0);
        if (!item) {
            return;
        }
        open_hit(last_b, static_cast<std::uint32_t>(item->data(Qt::UserRole + 1).toULongLong()),
                 static_cast<std::uint32_t>(item->data(Qt::UserRole + 2).toULongLong()),
                 static_cast<std::uint64_t>(item->data(Qt::UserRole + 3).toULongLong()),
                 static_cast<std::uint32_t>(item->data(Qt::UserRole + 4).toULongLong()));
    });
    QObject::connect(copy, &QPushButton::clicked, &dlg, [&last_env] {
        if (auto* cb = QGuiApplication::clipboard()) {
            cb->setText(QString::fromStdString(last_env.dump(2)));
        }
    });
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(box);
    dlg.resize(820, 560);
    dlg.exec();
}


}  // namespace sxpe::gui
