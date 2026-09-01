#include "dialogs.hpp"

#include "sxpe/resources/png.hpp"
#include "sxpe/resources/types.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
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
            bus.execute("nmap.set", {{"sessionId", session.toStdString()},
                                     {"instance", ni},
                                     {"name", name->text().toStdString()}});
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
    const auto path = QFileDialog::getOpenFileName(
        parent, dbc ? QObject::tr("Import DBC") : QObject::tr("Import package or files"), {},
        dbc ? QObject::tr("DBC (*.dbc *.package);;All (*.*)")
            : QObject::tr("Packages (*.package *.dbc *.world *.nhd);;All (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    const char* cmd = dbc ? "resource.importDbc" : "resource.importPackage";
    auto env = bus.execute(cmd, {{"sessionId", session.toStdString()},
                                 {"path", path.toStdString()},
                                 {"force", true}});
    if (!env.value("ok", false)) {
        QMessageBox::warning(parent, QObject::tr("SXPE"),
                             QString::fromStdString(env.dump()));
    }
}

void show_handlers_dialog(QWidget* parent, sxpe::commands::Bus& bus, PluginHost& host) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Handlers and plugins"));
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
    host.scan();
    for (const auto& p : host.plugins()) {
        list->addItem(QObject::tr("plugin: %1 (%2)").arg(p.label, p.kind));
    }
    lay->addWidget(new QLabel(QObject::tr("First-party handlers are always on. GUI plugins load only from the plugins folders.")));
    lay->addWidget(list);
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
    // Pad to the original blob length so the index size fields stay unchanged.
    if (static_cast<std::uint32_t>(fitted.size()) < max_bytes) {
        fitted.append(QByteArray(static_cast<int>(max_bytes) - fitted.size(), '\0'));
    }
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

}  // namespace sxpe::gui
