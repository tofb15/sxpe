#include "dialogs.hpp"
#include "sxpe/commands/find_refs_report.hpp"
#include "sxpe/commands/folder_scan_report.hpp"
#include "sxpe/commands/sims3pack_report.hpp"

#include "sxpe/resources/png.hpp"
#include "sxpe/resources/types.hpp"

#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QRadioButton>
#include <QRegularExpression>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
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
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QGuiApplication>
#include <QClipboard>
#include <QTextBrowser>
#include <QTimer>
#include <QEventLoop>
#include <QJsonObject>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkAccessManager>

#include <algorithm>
#include <span>
#include <unordered_map>
#include <unordered_set>

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
        dbc ? QObject::tr("Import as DBC into this package (Shift/Ctrl+click for several)")
            : QObject::tr("Import packages into this package (Shift/Ctrl+click for several)"),
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
    QProgressDialog progress(
        dbc ? QObject::tr("Importing DBC…") : QObject::tr("Importing packages…"),
        QObject::tr("Cancel"), 0, paths.size(), parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);
    progress.setCancelButtonText(QObject::tr("Cancel"));
    bus.clear_cancel();
    bus.set_cancel_check([&] { return progress.wasCanceled(); });
    bus.set_progress_handler([&](const nlohmann::json& ev) {
        const int done = ev.value("packagesDone", 0);
        const int total = ev.value("packagesTotal", paths.size());
        progress.setMaximum(std::max(1, total));
        progress.setValue(std::min(done, progress.maximum()));
        if (ev.contains("path") && ev["path"].is_string()) {
            progress.setLabelText(
                QObject::tr("Importing %1 (%2 / %3)")
                    .arg(QString::fromStdString(ev["path"].get<std::string>()))
                    .arg(done)
                    .arg(total));
        }
        QApplication::processEvents();
    });
    auto env = bus.execute(cmd, {{"sessionId", session.toStdString()},
                                 {"paths", arr},
                                 {"force", true},
                                 {"leftoverManifestPolicy", "strip"},
                                 {"duplicateTgiPolicy", "force"},
                                 {"reportProgress", true}});
    bus.clear_progress_handler();
    bus.clear_cancel_check();
    bus.clear_cancel();
    progress.setValue(progress.maximum());
    if (!env.value("ok", false)) {
        const bool cancelled = env.contains("data") && env["data"].value("cancelled", false);
        if (cancelled) {
            QMessageBox::information(
                parent, QObject::tr("SXPE"),
                QObject::tr("Import cancelled. The open package was rolled back to its "
                            "pre-import state."));
            return;
        }
        QMessageBox::warning(parent, QObject::tr("SXPE"),
                             QString::fromStdString(env["error"].value("message", env.dump())));
        return;
    }
    const auto imported = env["data"].value("imported", 0);
    const auto pkgs = env["data"].value("packages", 0);
    const auto failed = env["data"].value("failed", 0);
    const auto stripped = env["data"].contains("strippedLeftovers") &&
                                  env["data"]["strippedLeftovers"].is_array()
                              ? env["data"]["strippedLeftovers"].size()
                              : 0;
    const auto dups = env["data"].contains("duplicates") && env["data"]["duplicates"].is_array()
                          ? env["data"]["duplicates"].size()
                          : 0;
    QString msg = QObject::tr("Imported %1 resource(s) from %2 package(s).").arg(imported).arg(pkgs);
    if (stripped > 0) {
        msg += QLatin1Char('\n') +
               QObject::tr("Stripped %1 leftover Sims3Pack manifest resource(s).").arg(stripped);
    }
    if (dups > 0) {
        msg += QLatin1Char('\n') +
               QObject::tr("%1 duplicate TGI(s) overwritten (policy force).").arg(dups);
    }
    if (failed > 0) {
        msg += QLatin1Char('\n') + QObject::tr("%1 file(s) failed.").arg(failed);
        QMessageBox::warning(parent, QObject::tr("SXPE"), msg);
    } else if (paths.size() > 1 || stripped > 0 || dups > 0) {
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
        "Third-party GUI plugins / DLL Handlers are permanently unsupported (no plugin SDK).")));
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


bool show_nmap_editor(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                      const nlohmann::json* resource_id) {
    nlohmann::json args{{"sessionId", session.toStdString()}};
    if (resource_id) {
        args["resourceId"] = *resource_id;
    }
    auto got = bus.execute("nmap.get", args);
    if (!got.value("ok", false)) {
        QMessageBox::warning(
            parent, QObject::tr("SXPE"),
            QString::fromStdString(got.contains("error")
                                       ? got["error"].value("message", "No name map (NMAP).")
                                       : "No name map (NMAP)."));
        return false;
    }
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Name map"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* warn = new QLabel;
    warn->setWordWrap(true);
    warn->setStyleSheet(QStringLiteral("color: #a60;"));
    auto refresh_warn = [warn](const nlohmann::json& dups) {
        if (!dups.is_array() || dups.empty()) {
            warn->setText(QObject::tr(
                "Duplicate instance rows are kept after merge concat; the Name column uses "
                "last-wins."));
            return;
        }
        QStringList bits;
        for (const auto& d : dups) {
            bits << QObject::tr("%1× %2 → “%3”")
                        .arg(d.value("count", 0u))
                        .arg(QString("%1").arg(d.value("instance", 0ull), 16, 16, QLatin1Char('0')).toUpper())
                        .arg(QString::fromStdString(d.value("effectiveName", "")));
        }
        warn->setText(QObject::tr("Warning: duplicate instance rows (display last-wins): %1")
                          .arg(bits.join(QStringLiteral("; "))));
    };
    refresh_warn(got["data"].value("duplicates", nlohmann::json::array()));
    lay->addWidget(warn);

    auto* search = new QLineEdit;
    search->setPlaceholderText(QObject::tr("Search instance or name…"));
    lay->addWidget(search);

    auto* table = new QTableWidget(0, 2);
    table->setHorizontalHeaderLabels({QObject::tr("Instance (hex)"), QObject::tr("Name")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    auto fill_row = [](QTableWidget* table, int row, std::uint64_t inst, const QString& name) {
        table->setItem(row, 0,
                       new QTableWidgetItem(QString("%1").arg(inst, 16, 16, QLatin1Char('0')).toUpper()));
        table->setItem(row, 1, new QTableWidgetItem(name));
    };
    for (const auto& e : got["data"]["entries"]) {
        const int row = table->rowCount();
        table->insertRow(row);
        fill_row(table, row, e.value("instance", 0ull),
                 QString::fromStdString(e.value("name", "")));
    }
    auto recompute_dups = [table, refresh_warn] {
        nlohmann::json entries = nlohmann::json::array();
        for (int row = 0; row < table->rowCount(); ++row) {
            const auto inst_s = table->item(row, 0) ? table->item(row, 0)->text() : QString();
            const auto name = table->item(row, 1) ? table->item(row, 1)->text() : QString();
            entries.push_back({{"instance", inst_s.toULongLong(nullptr, 16)},
                               {"name", name.toStdString()}});
        }
        // Local last-wins duplicate scan (same policy as nmap_duplicates).
        std::unordered_map<std::uint64_t, std::uint32_t> counts;
        std::unordered_map<std::uint64_t, std::string> last;
        for (const auto& e : entries) {
            const auto inst = e.value("instance", 0ull);
            counts[inst] += 1;
            last.insert_or_assign(inst, e.value("name", ""));
        }
        nlohmann::json dups = nlohmann::json::array();
        std::unordered_set<std::uint64_t> seen;
        for (const auto& e : entries) {
            const auto inst = e.value("instance", 0ull);
            if (counts[inst] < 2 || !seen.insert(inst).second) {
                continue;
            }
            dups.push_back({{"instance", inst},
                            {"count", counts[inst]},
                            {"effectiveName", last[inst]}});
        }
        refresh_warn(dups);
    };
    QObject::connect(table, &QTableWidget::itemChanged, &dlg, [recompute_dups](QTableWidgetItem*) {
        recompute_dups();
    });

    auto apply_filter = [table, search] {
        const auto q = search->text().trimmed().toLower();
        for (int row = 0; row < table->rowCount(); ++row) {
            if (q.isEmpty()) {
                table->setRowHidden(row, false);
                continue;
            }
            const auto a = table->item(row, 0) ? table->item(row, 0)->text().toLower() : QString();
            const auto b = table->item(row, 1) ? table->item(row, 1)->text().toLower() : QString();
            table->setRowHidden(row, !(a.contains(q) || b.contains(q)));
        }
    };
    QObject::connect(search, &QLineEdit::textChanged, &dlg, [apply_filter](const QString&) {
        apply_filter();
    });

    auto* btns = new QHBoxLayout;
    auto* add = new QPushButton(QObject::tr("Add"));
    auto* del = new QPushButton(QObject::tr("Delete"));
    btns->addWidget(add);
    btns->addWidget(del);
    btns->addStretch();
    QObject::connect(add, &QPushButton::clicked, &dlg, [table, recompute_dups, apply_filter] {
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(QStringLiteral("0000000000000000")));
        table->setItem(row, 1, new QTableWidgetItem());
        recompute_dups();
        apply_filter();
    });
    QObject::connect(del, &QPushButton::clicked, &dlg, [table, recompute_dups] {
        const int row = table->currentRow();
        if (row >= 0) {
            table->removeRow(row);
            recompute_dups();
        }
    });
    lay->addLayout(btns);
    lay->addWidget(table, 1);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    lay->addWidget(box);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        nlohmann::json entries = nlohmann::json::array();
        for (int row = 0; row < table->rowCount(); ++row) {
            const auto inst_s = table->item(row, 0) ? table->item(row, 0)->text() : QString();
            const auto name = table->item(row, 1) ? table->item(row, 1)->text() : QString();
            entries.push_back({{"instance", inst_s.toULongLong(nullptr, 16)},
                               {"name", name.toStdString()}});
        }
        nlohmann::json save{{"sessionId", session.toStdString()}, {"entries", entries}};
        if (resource_id) {
            save["resourceId"] = *resource_id;
        }
        auto env = bus.execute("nmap.replace", save);
        if (!env.value("ok", false)) {
            QMessageBox::warning(&dlg, QObject::tr("SXPE"),
                                 QString::fromStdString(env["error"].value("message", "")));
            return;
        }
        dlg.accept();
    });
    dlg.resize(720, 480);
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


bool show_objd_editor(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                      std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                      std::uint32_t ordinal) {
    nlohmann::json rid{{"type", type}, {"group", group}, {"instance", instance}, {"ordinal", ordinal}};
    auto got = bus.execute("objd.get", {{"sessionId", session.toStdString()}, {"resourceId", rid}});
    if (!got.value("ok", false)) {
        QMessageBox::warning(parent, QObject::tr("SXPE"),
                             QObject::tr("This resource is not a catalog object (OBJD), or it failed to parse."));
        return false;
    }
    const auto& d = got["data"];
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Catalog object (OBJD)"));
    auto* form = new QFormLayout(&dlg);
    auto* name_guid = new QLineEdit(QString("%1").arg(d.value("nameGuid", 0ull), 16, 16, QLatin1Char('0')).toUpper());
    auto* desc_guid = new QLineEdit(QString("%1").arg(d.value("descGuid", 0ull), 16, 16, QLatin1Char('0')).toUpper());
    auto* iname = new QLineEdit(QString::fromStdString(d.value("internalName", std::string())));
    auto* idesc = new QLineEdit(QString::fromStdString(d.value("internalDesc", std::string())));
    auto* price = new QDoubleSpinBox;
    price->setRange(0.0, 1e9);
    price->setDecimals(3);
    price->setValue(d.value("price", 0.0));
    auto* thumb = new QLineEdit(QString("%1").arg(d.value("thumbIid", 0ull), 16, 16, QLatin1Char('0')).toUpper());
    auto* inst = new QLineEdit(QString::fromStdString(d.value("instanceName", std::string())));
    form->addRow(QObject::tr("Name GUID"), name_guid);
    form->addRow(QObject::tr("Desc GUID"), desc_guid);
    form->addRow(QObject::tr("Internal name"), iname);
    form->addRow(QObject::tr("Internal desc"), idesc);
    form->addRow(QObject::tr("Price"), price);
    form->addRow(QObject::tr("Thumb IID"), thumb);
    form->addRow(QObject::tr("Instance name"), inst);
    auto* note = new QLabel(QObject::tr("Materials and unknown trailing bytes are preserved. Layout: docs/spec/objd.md"));
    note->setWordWrap(true);
    form->addRow(note);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(box);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        auto parse_hex = [](const QString& s, bool* ok) -> quint64 {
            return s.trimmed().toULongLong(ok, 16);
        };
        bool ok = false;
        const auto ng = parse_hex(name_guid->text(), &ok);
        if (!ok) {
            QMessageBox::warning(&dlg, QObject::tr("SXPE"), QObject::tr("Invalid name GUID hex."));
            return;
        }
        const auto dg = parse_hex(desc_guid->text(), &ok);
        if (!ok) {
            QMessageBox::warning(&dlg, QObject::tr("SXPE"), QObject::tr("Invalid desc GUID hex."));
            return;
        }
        const auto th = parse_hex(thumb->text(), &ok);
        if (!ok) {
            QMessageBox::warning(&dlg, QObject::tr("SXPE"), QObject::tr("Invalid thumb IID hex."));
            return;
        }
        nlohmann::json args{{"sessionId", session.toStdString()},
                            {"resourceId", rid},
                            {"nameGuid", ng},
                            {"descGuid", dg},
                            {"internalName", iname->text().toStdString()},
                            {"internalDesc", idesc->text().toStdString()},
                            {"price", price->value()},
                            {"thumbIid", th}};
        if (!inst->text().isEmpty() || d.contains("instanceName")) {
            args["instanceName"] = inst->text().toStdString();
        }
        auto env = bus.execute("objd.set", args);
        if (!env.value("ok", false)) {
            QMessageBox::warning(&dlg, QObject::tr("SXPE"),
                                 QString::fromStdString(env["error"].value("message", "")));
            return;
        }
        dlg.accept();
    });
    dlg.resize(520, 360);
    return dlg.exec() == QDialog::Accepted;
}

bool show_casp_editor(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                      std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                      std::uint32_t ordinal) {
    nlohmann::json rid{{"type", type}, {"group", group}, {"instance", instance}, {"ordinal", ordinal}};
    auto got = bus.execute("casp.get", {{"sessionId", session.toStdString()}, {"resourceId", rid}});
    if (!got.value("ok", false)) {
        QMessageBox::warning(parent, QObject::tr("SXPE"),
                             QObject::tr("This resource is not a CAS part (CASP), or it failed to parse."));
        return false;
    }
    const auto& d = got["data"];
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("CAS part (CASP)"));
    auto* form = new QFormLayout(&dlg);
    auto* name = new QLineEdit(QString::fromStdString(d.value("name", std::string())));
    auto* sort = new QDoubleSpinBox;
    sort->setRange(-1e9, 1e9);
    sort->setDecimals(3);
    sort->setValue(d.value("sortPriority", 0.0));
    auto* clothing = new QSpinBox;
    clothing->setRange(0, 0x7fffffff);
    clothing->setValue(static_cast<int>(d.value("clothingType", 0u)));
    auto* type_flags = new QSpinBox;
    type_flags->setRange(0, 0x7fffffff);
    type_flags->setValue(static_cast<int>(d.value("typeFlags", 0u)));
    auto* age_flags = new QSpinBox;
    age_flags->setRange(0, 255);
    age_flags->setValue(static_cast<int>(d.value("ageFlags", 0u)));
    auto* species = new QSpinBox;
    species->setRange(0, 15);
    species->setValue(static_cast<int>(d.value("species", 0u)));
    auto* gender = new QSpinBox;
    gender->setRange(0, 15);
    gender->setValue(static_cast<int>(d.value("genderFlags", 0u)));
    auto* category = new QSpinBox;
    category->setRange(0, 0x7fffffff);
    category->setValue(static_cast<int>(d.value("clothingCategory", 0u)));
    auto* tgis = new QPlainTextEdit;
    tgis->setPlaceholderText(QObject::tr("One TGI per line: type group instance (hex or decimal)"));
    QStringList tgi_lines;
    if (d.contains("tgis") && d["tgis"].is_array()) {
        for (const auto& row : d["tgis"]) {
            tgi_lines << QString("0x%1 0x%2 0x%3")
                             .arg(row.value("type", 0u), 8, 16, QLatin1Char('0'))
                             .arg(row.value("group", 0u), 8, 16, QLatin1Char('0'))
                             .arg(row.value("instance", 0ull), 16, 16, QLatin1Char('0'));
        }
    }
    tgis->setPlainText(tgi_lines.join(QLatin1Char('\n')));
    form->addRow(QObject::tr("Name"), name);
    form->addRow(QObject::tr("Sort priority"), sort);
    form->addRow(QObject::tr("Clothing type"), clothing);
    form->addRow(QObject::tr("Type flags"), type_flags);
    form->addRow(QObject::tr("Age flags"), age_flags);
    form->addRow(QObject::tr("Species"), species);
    form->addRow(QObject::tr("Gender flags"), gender);
    form->addRow(QObject::tr("Category"), category);
    form->addRow(QObject::tr("TGI refs"), tgis);
    auto* note = new QLabel(QObject::tr("Presets and unknown mid bytes are preserved. Layout: docs/spec/casp.md"));
    note->setWordWrap(true);
    form->addRow(note);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(box);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        nlohmann::json tgi_arr = nlohmann::json::array();
        const auto lines = tgis->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const auto& line : lines) {
            const auto parts = line.simplified().split(QLatin1Char(' '));
            if (parts.size() < 3) {
                QMessageBox::warning(&dlg, QObject::tr("SXPE"),
                                     QObject::tr("Each TGI line needs type group instance."));
                return;
            }
            bool ok1 = false, ok2 = false, ok3 = false;
            const auto ty = parts[0].toULongLong(&ok1, 0);
            const auto gr = parts[1].toULongLong(&ok2, 0);
            const auto in = parts[2].toULongLong(&ok3, 0);
            if (!ok1 || !ok2 || !ok3) {
                QMessageBox::warning(&dlg, QObject::tr("SXPE"), QObject::tr("Invalid TGI number."));
                return;
            }
            tgi_arr.push_back({{"type", ty}, {"group", gr}, {"instance", in}});
        }
        nlohmann::json args{{"sessionId", session.toStdString()},
                            {"resourceId", rid},
                            {"name", name->text().toStdString()},
                            {"sortPriority", sort->value()},
                            {"clothingType", clothing->value()},
                            {"typeFlags", type_flags->value()},
                            {"ageFlags", age_flags->value()},
                            {"species", species->value()},
                            {"genderFlags", gender->value()},
                            {"clothingCategory", category->value()},
                            {"tgis", tgi_arr}};
        auto env = bus.execute("casp.set", args);
        if (!env.value("ok", false)) {
            QMessageBox::warning(&dlg, QObject::tr("SXPE"),
                                 QString::fromStdString(env["error"].value("message", "")));
            return;
        }
        dlg.accept();
    });
    dlg.resize(560, 520);
    return dlg.exec() == QDialog::Accepted;
}

bool show_refs_editor(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                      std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                      std::uint32_t ordinal) {
    nlohmann::json rid{{"type", type}, {"group", group}, {"instance", instance}, {"ordinal", ordinal}};
    auto got = bus.execute("refs.get", {{"sessionId", session.toStdString()}, {"resourceId", rid}});
    if (!got.value("ok", false)) {
        QMessageBox::warning(parent, QObject::tr("SXPE"),
                             QObject::tr("This resource is not a REFS table, or it failed to parse."));
        return false;
    }
    const auto& d = got["data"];
    if (d.value("partial", false)) {
        QMessageBox::warning(parent, QObject::tr("SXPE"),
                             QObject::tr("This REFS resource only partially parsed; editing is refused."));
        return false;
    }
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Reference table (REFS)"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* info = new QLabel(
        QObject::tr("Version %1 · aux %2%3")
            .arg(d.value("version", 0))
            .arg(d.value("auxIsDword", false) ? QObject::tr("DWORD") : QObject::tr("WORD"))
            .arg(d.value("hasThingy", false)
                     ? QObject::tr(" · thingy %1").arg(d.value("thingy", 0))
                     : QString()));
    info->setWordWrap(true);
    lay->addWidget(info);
    auto* entries = new QPlainTextEdit;
    entries->setPlaceholderText(
        QObject::tr("One TGI per line: type group instance [aux] (hex or decimal)"));
    QStringList entry_lines;
    if (d.contains("entries") && d["entries"].is_array()) {
        for (const auto& row : d["entries"]) {
            entry_lines << QString("0x%1 0x%2 0x%3 %4")
                               .arg(row.value("type", 0u), 8, 16, QLatin1Char('0'))
                               .arg(row.value("group", 0u), 8, 16, QLatin1Char('0'))
                               .arg(row.value("instance", 0ull), 16, 16, QLatin1Char('0'))
                               .arg(row.value("aux", 0u));
        }
    }
    entries->setPlainText(entry_lines.join(QLatin1Char('\n')));
    lay->addWidget(new QLabel(QObject::tr("Entries (TGI + aux)")));
    lay->addWidget(entries, 1);
    auto* indices = new QPlainTextEdit;
    indices->setMaximumHeight(100);
    indices->setPlaceholderText(QObject::tr("WORD indices, one per line or space-separated"));
    QStringList idx_lines;
    if (d.contains("indices") && d["indices"].is_array()) {
        for (const auto& v : d["indices"]) {
            idx_lines << QString::number(v.get<std::uint64_t>());
        }
    }
    indices->setPlainText(idx_lines.join(QLatin1Char('\n')));
    lay->addWidget(new QLabel(QObject::tr("Indices")));
    lay->addWidget(indices);
    auto* note = new QLabel(
        QObject::tr("Preserves version / thingy / aux width. Layout: docs/spec/refs.md"));
    note->setWordWrap(true);
    lay->addWidget(note);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    lay->addWidget(box);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        nlohmann::json entry_arr = nlohmann::json::array();
        const auto lines = entries->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const auto& line : lines) {
            const auto parts = line.simplified().split(QLatin1Char(' '));
            if (parts.size() < 3) {
                QMessageBox::warning(&dlg, QObject::tr("SXPE"),
                                     QObject::tr("Each entry needs type group instance [aux]."));
                return;
            }
            bool ok1 = false, ok2 = false, ok3 = false, ok4 = true;
            const auto ty = parts[0].toULongLong(&ok1, 0);
            const auto gr = parts[1].toULongLong(&ok2, 0);
            const auto in = parts[2].toULongLong(&ok3, 0);
            quint64 aux = 0;
            if (parts.size() >= 4) {
                aux = parts[3].toULongLong(&ok4, 0);
            }
            if (!ok1 || !ok2 || !ok3 || !ok4) {
                QMessageBox::warning(&dlg, QObject::tr("SXPE"), QObject::tr("Invalid entry number."));
                return;
            }
            entry_arr.push_back({{"type", ty}, {"group", gr}, {"instance", in}, {"aux", aux}});
        }
        nlohmann::json idx_arr = nlohmann::json::array();
        const auto idx_text = indices->toPlainText().simplified();
        if (!idx_text.isEmpty()) {
            for (const auto& tok : idx_text.split(QRegularExpression(QStringLiteral("[\\s,]+")),
                                                  Qt::SkipEmptyParts)) {
                bool ok = false;
                const auto v = tok.toULongLong(&ok, 0);
                if (!ok || v > 0xFFFFull) {
                    QMessageBox::warning(&dlg, QObject::tr("SXPE"),
                                         QObject::tr("Invalid index (need 0–65535)."));
                    return;
                }
                idx_arr.push_back(v);
            }
        }
        nlohmann::json args{{"sessionId", session.toStdString()},
                            {"resourceId", rid},
                            {"entries", entry_arr},
                            {"indices", idx_arr}};
        auto env = bus.execute("refs.set", args);
        if (!env.value("ok", false)) {
            QMessageBox::warning(&dlg, QObject::tr("SXPE"),
                                 QString::fromStdString(env["error"].value("message", "")));
            return;
        }
        dlg.accept();
    });
    dlg.resize(640, 520);
    return dlg.exec() == QDialog::Accepted;
}


bool show_rcol_replace_chunk_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                                    std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                                    std::uint32_t ordinal) {
    nlohmann::json rid{{"type", type}, {"group", group}, {"instance", instance}, {"ordinal", ordinal}};
    auto got = bus.execute("rcol.summary", {{"sessionId", session.toStdString()}, {"resourceId", rid}});
    if (!got.value("ok", false)) {
        QMessageBox::warning(parent, QObject::tr("SXPE"),
                             QObject::tr("This resource is not an RCOL mesh/material, or it failed to parse."));
        return false;
    }
    const auto& d = got["data"];
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Replace RCOL chunk"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* info = new QLabel(
        QObject::tr("RCOL v%1 · %2 internal chunk(s). Session undo restores the whole resource; "
                    "optional backup writes the previous chunk bytes.")
            .arg(d.value("version", 0))
            .arg(d.value("internalCount", 0)));
    info->setWordWrap(true);
    lay->addWidget(info);
    auto* chunk_list = new QComboBox;
    if (d.contains("chunks") && d["chunks"].is_array()) {
        for (const auto& ch : d["chunks"]) {
            const auto tag = QString::fromStdString(ch.value("tag", std::string()));
            const auto idx = ch.value("index", 0);
            QString label = QObject::tr("[%1] %2 — %3 bytes")
                                .arg(idx)
                                .arg(tag.isEmpty() ? QString::number(ch.value("type", 0u), 16) : tag)
                                .arg(ch.value("size", 0));
            if (ch.contains("shaderName")) {
                const auto sn = QString::fromStdString(ch.value("shaderName", std::string()));
                if (!sn.isEmpty()) {
                    label += QObject::tr(" · %1").arg(sn);
                }
            }
            chunk_list->addItem(label, idx);
        }
    }
    if (chunk_list->count() == 0) {
        QMessageBox::warning(parent, QObject::tr("SXPE"), QObject::tr("No chunks to replace."));
        return false;
    }
    lay->addWidget(new QLabel(QObject::tr("Chunk")));
    lay->addWidget(chunk_list);
    auto* path_row = new QHBoxLayout;
    auto* path_edit = new QLineEdit;
    path_edit->setPlaceholderText(QObject::tr("New chunk payload file…"));
    auto* browse = new QPushButton(QObject::tr("Browse…"));
    path_row->addWidget(path_edit, 1);
    path_row->addWidget(browse);
    lay->addLayout(path_row);
    QObject::connect(browse, &QPushButton::clicked, &dlg, [&] {
        const auto p = QFileDialog::getOpenFileName(&dlg, QObject::tr("Chunk payload"), {},
                                                    QObject::tr("All files (*)"));
        if (!p.isEmpty()) {
            path_edit->setText(p);
        }
    });
    auto* backup = new QCheckBox(QObject::tr("Write backup of previous chunk bytes"));
    backup->setChecked(true);
    lay->addWidget(backup);
    auto* backup_path = new QLineEdit;
    backup_path->setPlaceholderText(QObject::tr("Backup path (optional; defaults next to payload)"));
    lay->addWidget(backup_path);
    auto* note = new QLabel(
        QObject::tr("No in-app 3D viewport. Layout: docs/spec/preview-wave2.md / rcol tooling."));
    note->setWordWrap(true);
    lay->addWidget(note);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    lay->addWidget(box);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        if (path_edit->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dlg, QObject::tr("SXPE"), QObject::tr("Choose a payload file."));
            return;
        }
        const int idx = chunk_list->currentData().toInt();
        nlohmann::json args{{"sessionId", session.toStdString()},
                            {"resourceId", rid},
                            {"chunkIndex", idx},
                            {"path", path_edit->text().trimmed().toStdString()}};
        if (backup->isChecked()) {
            QString bp = backup_path->text().trimmed();
            if (bp.isEmpty()) {
                bp = path_edit->text().trimmed() + QStringLiteral(".chunk%1.bak").arg(idx);
            }
            args["backupPath"] = bp.toStdString();
        }
        auto env = bus.execute("rcol.replaceChunk", args);
        if (!env.value("ok", false)) {
            QMessageBox::warning(&dlg, QObject::tr("SXPE"),
                                 QString::fromStdString(env["error"].value("message", "")));
            return;
        }
        dlg.accept();
    });
    dlg.resize(560, 320);
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

bool show_clip_editor(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                      std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                      std::uint32_t ordinal) {
    nlohmann::json rid{{"type", type}, {"group", group}, {"instance", instance}, {"ordinal", ordinal}};
    auto got = bus.execute("clip.info", {{"sessionId", session.toStdString()}, {"resourceId", rid}});
    if (!got.value("ok", false)) {
        QMessageBox::warning(parent, QObject::tr("SXPE"),
                             QObject::tr("This resource is not a CLIP, or it failed to parse."));
        return false;
    }
    const auto& d = got["data"];
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("CLIP metadata"));
    auto* form = new QFormLayout(&dlg);
    auto* anim = new QLineEdit(QString::fromStdString(d.value("animName", std::string())));
    auto* src = new QLineEdit(QString::fromStdString(d.value("sourceFile", std::string())));
    auto* actor = new QLineEdit(QString::fromStdString(d.value("actorName", std::string())));
    form->addRow(QObject::tr("Anim name"), anim);
    form->addRow(QObject::tr("Source file"), src);
    form->addRow(QObject::tr("Actor name"), actor);
    auto* tracks = new QPlainTextEdit;
    tracks->setPlaceholderText(QObject::tr("One track hash per line: index hash (hex or decimal)"));
    QStringList track_lines;
    if (d.contains("trackHashes") && d["trackHashes"].is_array()) {
        int i = 0;
        for (const auto& h : d["trackHashes"]) {
            track_lines << QStringLiteral("%1 %2")
                               .arg(i)
                               .arg(h.get<std::uint32_t>(), 8, 16, QLatin1Char('0'));
            ++i;
        }
    }
    tracks->setPlainText(track_lines.join(QLatin1Char('\n')));
    tracks->setMinimumHeight(120);
    form->addRow(QObject::tr("Track hashes"), tracks);
    auto* note = new QLabel(
        QObject::tr("Safe fields only (docs/spec/clip.md). Frame data and playback are not edited. "
                    "Use Resource → Editors → CLIP export as new name… (or clip.exportAsBatch) to "
                    "copy with a new fnv64_clip instance."));
    note->setWordWrap(true);
    form->addRow(note);
    auto* meta = new QLabel(QObject::tr("Duration %1 s · %2 frames · version %3%4")
                                .arg(d.value("durationSeconds", 0.0), 0, 'f', 3)
                                .arg(d.value("frameCount", 0))
                                .arg(d.value("version", 0))
                                .arg(d.value("partial", false) ? QObject::tr(" · partial") : QString()));
    meta->setWordWrap(true);
    form->addRow(meta);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(box);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        nlohmann::json args{{"sessionId", session.toStdString()},
                            {"resourceId", rid},
                            {"animName", anim->text().toStdString()},
                            {"sourceFile", src->text().toStdString()},
                            {"actorName", actor->text().toStdString()}};
        nlohmann::json th = nlohmann::json::array();
        const auto lines = tracks->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const auto& line : lines) {
            const auto parts = line.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() < 2) {
                QMessageBox::warning(&dlg, QObject::tr("SXPE"),
                                     QObject::tr("Track line needs index and hash: %1").arg(line));
                return;
            }
            bool ok_i = false;
            bool ok_h = false;
            const auto index = parts[0].toUInt(&ok_i, 0);
            const auto hash = parts[1].toUInt(&ok_h, 0);
            if (!ok_i || !ok_h) {
                QMessageBox::warning(&dlg, QObject::tr("SXPE"),
                                     QObject::tr("Invalid track index/hash: %1").arg(line));
                return;
            }
            th.push_back({{"index", index}, {"hash", hash}});
        }
        if (!th.empty()) {
            args["trackHashes"] = th;
        }
        auto env = bus.execute("clip.set", args);
        if (!env.value("ok", false)) {
            QMessageBox::warning(&dlg, QObject::tr("SXPE"),
                                 QString::fromStdString(env["error"].value("message", "")));
            return;
        }
        dlg.accept();
    });
    dlg.resize(560, 420);
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
    auto* s3sa = new QLineEdit(s.value("ext/s3sa").toString());
    hex->setPlaceholderText(QObject::tr("e.g. C:\\Tools\\hex.exe {path}"));
    text->setPlaceholderText(QObject::tr("e.g. notepad {path}"));
    s3sa->setPlaceholderText(QObject::tr("e.g. ilspy {path}   or   dnSpy {path}"));
    form->addRow(QObject::tr("Hex editor"), hex);
    form->addRow(QObject::tr("Text editor"), text);
    form->addRow(QObject::tr("S3SA viewer"), s3sa);
    form->addRow(new QLabel(
        QObject::tr("{path} is replaced with the exported file. S3SA View exports a temp DLL "
                    "(never LoadLibrary); the temp is deleted when the viewer exits.")));
    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(box);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        s.setValue("ext/hex", hex->text());
        s.setValue("ext/text", text->text());
        s.setValue("ext/s3sa", s3sa->text());
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
        "copy TGI key; import/export (file, package, DBC); typed editors (STBL, Name map/NMAP, XML/ITUN, Catalog object/OBJD, CAS part/CASP, S3SA export/import/view DLL, "
        "CLIP, DDS, SNAP PNG, VID); open in hex/text editor; delete.</p>"
        "<ul>"
        "<li><b>Add…</b> — Ctrl+I</li>"
        "<li><b>Copy</b> — Ctrl+C</li>"
        "<li><b>Paste</b> — Ctrl+V</li>"
        "<li><b>Duplicate</b> — Ctrl+D</li>"
        "<li><b>Copy resource key</b> — Ctrl+Shift+C</li>"
        "<li><b>Delete</b> — Delete</li>"
        "</ul>"
        "<h3>Neighborhood / world layout lock</h3>"
        "<p><b>.nhd</b>, <b>.world</b>, and <b>.dbc</b> sessions are layout-locked. "
        "A status-bar badge appears. Safe: replace a resource payload in place if it fits "
        "the existing hole. Not supported (actions greyed out / refused): add, delete, "
        "reorder, compact, create NMAP. Error and Validate text name "
        "“neighborhood / world layout lock”.</p>"
        "<h3>Tools</h3>"
        "<p>FNV-1 / CLIP hash, compare packages, find references, scan folder (Downloads hygiene), inspect Sims3Pack, "
        "<b>Merge packages…</b> (Merge assistant: folder → preview → SXMM merge → optional validate), "
        "un-merge package, byte search, validate (conflict hotspots), compact / save.</p>"
        "<ul>"
        "<li><b>Search…</b> — Ctrl+F</li>"
        "</ul>"
        "<h3>Settings</h3>"
        "<p>Preview toggles (DDS / text / hex), DBC import checkpoint, bookmarks, "
        "built-in handlers (first-party only; plugins permanently unsupported), external programs (hex/text/S3SA viewer — not DLL plugins), save settings.</p>"
        "<h3>Help</h3>"
        "<p>Contents (this window), <b>Common tasks</b> (links to workflows.md), "
        "Check for update (GitHub Releases; never auto-downloads), About, Warranty, Licence.</p>"
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



void show_find_refs_dialog(
    QWidget* parent, sxpe::commands::Bus& bus, const QString& session, std::uint32_t type,
    std::uint32_t group, std::uint64_t instance, std::uint32_t ordinal,
    const std::function<void(std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                             std::uint32_t ordinal)>& select_hit) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Find references"));
    auto* lay = new QVBoxLayout(&dlg);

    auto* summary = new QPlainTextEdit;
    summary->setReadOnly(true);
    summary->setMaximumHeight(100);
    lay->addWidget(summary);

    auto* mode_row = new QHBoxLayout;
    auto* mode_inbound = new QRadioButton(QObject::tr("Inbound (who points here)"));
    auto* mode_outbound = new QRadioButton(QObject::tr("Outbound (what this points at)"));
    mode_inbound->setChecked(true);
    mode_row->addWidget(mode_inbound);
    mode_row->addWidget(mode_outbound);
    mode_row->addStretch(1);
    lay->addLayout(mode_row);

    auto* byte_scan = new QCheckBox(QObject::tr("Also byte-scan payloads (slow, capped)"));
    lay->addWidget(byte_scan);

    auto* table = new QTableWidget(0, 6);
    table->setHorizontalHeaderLabels({QObject::tr("Tag"), QObject::tr("Type"),
                                      QObject::tr("Group"), QObject::tr("Instance"),
                                      QObject::tr("Ord"), QObject::tr("Reason")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    lay->addWidget(table, 1);

    auto* hint = new QLabel(
        QObject::tr("Double-click a row (or Jump) to select that resource. Inbound scans REFS and "
                    "OBJK/VPXY TGI lists; optional byte-scan covers other payloads."));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto fill = [&](const nlohmann::json& env) {
        table->setRowCount(0);
        QStringList lines;
        if (!env.value("ok", false)) {
            QString msg = QObject::tr("Command failed.");
            if (env.contains("error") && env["error"].is_object()) {
                msg = QString::fromStdString(env["error"].value("message", msg.toStdString()));
            }
            summary->setPlainText(msg);
            return;
        }
        const auto& data = env["data"];
        if (data.contains("summary") && data["summary"].is_array()) {
            for (const auto& line : data["summary"]) {
                if (line.is_string()) {
                    lines << QString::fromStdString(line.get<std::string>());
                }
            }
        } else {
            for (const auto& line : sxpe::commands::format_find_refs_summary(data)) {
                lines << QString::fromStdString(line);
            }
        }
        summary->setPlainText(lines.join(QLatin1Char('\n')));
        const auto hits = data.contains("hits") ? data.value("hits", nlohmann::json::array())
                                                 : data.value("refs", nlohmann::json::array());
        if (!hits.is_array()) {
            return;
        }
        for (const auto& h : hits) {
            const auto& src = h.contains("source") ? h["source"] : h;
            const int r = table->rowCount();
            table->insertRow(r);
            auto put = [&](int c, const QString& s) {
                auto* item = new QTableWidgetItem(s);
                item->setData(Qt::UserRole, static_cast<qulonglong>(src.value("type", 0u)));
                item->setData(Qt::UserRole + 1, static_cast<qulonglong>(src.value("group", 0u)));
                item->setData(Qt::UserRole + 2,
                              static_cast<qulonglong>(src.value("instance", 0ull)));
                item->setData(Qt::UserRole + 3, static_cast<qulonglong>(src.value("ordinal", 0u)));
                table->setItem(r, c, item);
            };
            put(0, QString::fromStdString(src.value("tag", "")));
            put(1, QString::fromStdString(
                       src.value("typeHex", std::to_string(src.value("type", 0u)))));
            put(2, QString::fromStdString(
                       src.value("groupHex", std::to_string(src.value("group", 0u)))));
            put(3, QString::fromStdString(
                       src.value("instanceHex", std::to_string(src.value("instance", 0ull)))));
            put(4, QString::number(static_cast<qulonglong>(src.value("ordinal", 0u))));
            put(5, QString::fromStdString(h.value("reason", "")));
        }
    };

    auto run_find = [&] {
        nlohmann::json args{{"sessionId", session.toStdString()},
                            {"resourceId",
                             {{"type", type},
                              {"group", group},
                              {"instance", instance},
                              {"ordinal", ordinal}}},
                            {"limit", 200}};
        nlohmann::json env;
        if (mode_outbound->isChecked()) {
            env = bus.execute("resource.listRefs", args);
        } else {
            args["byteScan"] = byte_scan->isChecked();
            env = bus.execute("resource.findRefs", args);
        }
        fill(env);
    };

    auto jump = [&] {
        const auto rows = table->selectionModel()->selectedRows();
        if (rows.isEmpty()) {
            return;
        }
        const int r = rows.front().row();
        auto* item = table->item(r, 0);
        if (!item) {
            return;
        }
        select_hit(static_cast<std::uint32_t>(item->data(Qt::UserRole).toULongLong()),
                   static_cast<std::uint32_t>(item->data(Qt::UserRole + 1).toULongLong()),
                   static_cast<std::uint64_t>(item->data(Qt::UserRole + 2).toULongLong()),
                   static_cast<std::uint32_t>(item->data(Qt::UserRole + 3).toULongLong()));
        dlg.accept();
    };

    auto* box = new QDialogButtonBox;
    auto* find = box->addButton(QObject::tr("Find"), QDialogButtonBox::ActionRole);
    auto* jump_btn = box->addButton(QObject::tr("Jump"), QDialogButtonBox::ActionRole);
    box->addButton(QDialogButtonBox::Close);
    lay->addWidget(box);
    auto sync_mode = [&] {
        byte_scan->setEnabled(mode_inbound->isChecked());
    };
    QObject::connect(mode_inbound, &QRadioButton::toggled, &dlg, [&](bool) { sync_mode(); run_find(); });
    QObject::connect(mode_outbound, &QRadioButton::toggled, &dlg, [&](bool on) {
        if (on) {
            sync_mode();
            run_find();
        }
    });
    sync_mode();
    QObject::connect(find, &QPushButton::clicked, &dlg, run_find);
    QObject::connect(jump_btn, &QPushButton::clicked, &dlg, jump);
    QObject::connect(table, &QTableWidget::cellDoubleClicked, &dlg, [jump](int, int) { jump(); });
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.resize(720, 420);
    run_find();
    dlg.exec();
}

void show_folder_scan_dialog(
    QWidget* parent, sxpe::commands::Bus& bus,
    const std::function<void(const QString& path)>& open_path) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Scan folder"));
    auto* lay = new QVBoxLayout(&dlg);

    auto* form = new QFormLayout;
    auto* path_edit = new QLineEdit;
    auto* browse = new QPushButton(QObject::tr("Browse…"));
    auto* row = new QHBoxLayout;
    row->addWidget(path_edit, 1);
    row->addWidget(browse);
    form->addRow(QObject::tr("Folder"), row);
    lay->addLayout(form);

    auto* summary = new QPlainTextEdit;
    summary->setReadOnly(true);
    summary->setMaximumHeight(140);
    lay->addWidget(summary);

    auto* table = new QTableWidget(0, 4);
    table->setHorizontalHeaderLabels({QObject::tr("Kind"), QObject::tr("Path"),
                                      QObject::tr("Detail"), QObject::tr("TGI / note")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    lay->addWidget(table, 1);

    auto* hint = new QLabel(
        QObject::tr("Read-only scan of *.package (recursive). Never deletes or renames. "
                    "Double-click or Open to load a path in SXPE when it is a valid Sims 3 "
                    "package. Duplicate TGI rows list a sample of conflicting files."));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    nlohmann::json last_env = nlohmann::json::object();

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
        if (data.contains("summary") && data["summary"].is_array()) {
            for (const auto& line : data["summary"]) {
                if (line.is_string()) {
                    lines << QString::fromStdString(line.get<std::string>());
                }
            }
        } else {
            for (const auto& line : sxpe::commands::format_folder_scan_summary(data)) {
                lines << QString::fromStdString(line);
            }
        }
        summary->setPlainText(lines.join(QLatin1Char('\n')));

        auto add_row = [&](const QString& kind, const QString& path, const QString& detail,
                           const QString& note) {
            const int r = table->rowCount();
            table->insertRow(r);
            auto put = [&](int c, const QString& s) {
                auto* item = new QTableWidgetItem(s);
                item->setData(Qt::UserRole, path);
                table->setItem(r, c, item);
            };
            put(0, kind);
            put(1, path);
            put(2, detail);
            put(3, note);
        };

        if (data.contains("issues") && data["issues"].is_array()) {
            for (const auto& it : data["issues"]) {
                add_row(QString::fromStdString(it.value("kind", std::string{"issue"})),
                        QString::fromStdString(it.value("path", std::string{})),
                        QString::fromStdString(it.value("message", std::string{})),
                        QObject::tr("%1 bytes").arg(
                            static_cast<qulonglong>(it.value("bytes", 0ull))));
            }
        }
        if (data.contains("duplicates") && data["duplicates"].is_array()) {
            for (const auto& d : data["duplicates"]) {
                QString tgi = QString::fromStdString(d.value("typeHex", std::string{})) +
                              QLatin1Char(' ') +
                              QString::fromStdString(d.value("groupHex", std::string{})) +
                              QLatin1Char(' ') +
                              QString::fromStdString(d.value("instanceHex", std::string{}));
                QString first_path;
                if (d.contains("paths") && d["paths"].is_array() && !d["paths"].empty() &&
                    d["paths"][0].is_string()) {
                    first_path = QString::fromStdString(d["paths"][0].get<std::string>());
                }
                add_row(QObject::tr("duplicate"), first_path,
                        QObject::tr("in %1 file(s)")
                            .arg(static_cast<qulonglong>(d.value("fileCount", 0u))),
                        tgi);
                if (d.contains("paths") && d["paths"].is_array()) {
                    for (const auto& p : d["paths"]) {
                        if (!p.is_string()) {
                            continue;
                        }
                        const auto ps = QString::fromStdString(p.get<std::string>());
                        if (ps == first_path) {
                            continue;
                        }
                        add_row(QObject::tr("duplicate"), ps, QObject::tr("same TGI"), tgi);
                    }
                }
            }
        }
    };

    auto run_scan = [&] {
        const auto p = path_edit->text().trimmed();
        if (p.isEmpty()) {
            QMessageBox::warning(&dlg, QObject::tr("Scan folder"),
                                 QObject::tr("Choose a folder to scan."));
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        auto env = bus.execute("folder.scan", {{"path", p.toStdString()}});
        QApplication::restoreOverrideCursor();
        fill(env);
    };

    auto open_selected = [&] {
        const auto rows = table->selectionModel() ? table->selectionModel()->selectedRows()
                                                  : QModelIndexList{};
        if (rows.isEmpty() || !open_path) {
            return;
        }
        const auto* item = table->item(rows.first().row(), 0);
        if (!item) {
            return;
        }
        const auto path = item->data(Qt::UserRole).toString();
        if (path.isEmpty()) {
            return;
        }
        open_path(path);
    };

    QObject::connect(browse, &QPushButton::clicked, &dlg, [path_edit, &dlg] {
        const auto p = QFileDialog::getExistingDirectory(&dlg, QObject::tr("Scan folder"),
                                                         path_edit->text());
        if (!p.isEmpty()) {
            path_edit->setText(p);
        }
    });
    QObject::connect(table, &QTableWidget::cellDoubleClicked, &dlg,
                     [&](int, int) { open_selected(); });

    auto* box = new QDialogButtonBox;
    auto* scan = box->addButton(QObject::tr("Scan"), QDialogButtonBox::ActionRole);
    auto* open_btn = box->addButton(QObject::tr("Open in SXPE"), QDialogButtonBox::ActionRole);
    auto* copy = box->addButton(QObject::tr("Copy JSON"), QDialogButtonBox::ActionRole);
    box->addButton(QDialogButtonBox::Close);
    lay->addWidget(box);
    QObject::connect(scan, &QPushButton::clicked, &dlg, run_scan);
    QObject::connect(open_btn, &QPushButton::clicked, &dlg, open_selected);
    QObject::connect(copy, &QPushButton::clicked, &dlg, [&] {
        QGuiApplication::clipboard()->setText(
            QString::fromStdString(last_env.dump(2)));
    });
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.resize(860, 520);
    dlg.exec();
}



void show_sims3pack_dialog(
    QWidget* parent, sxpe::commands::Bus& bus,
    const std::function<void(const QString& path)>& open_package) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Inspect Sims3Pack"));
    dlg.resize(780, 520);
    auto* lay = new QVBoxLayout(&dlg);

    auto* form = new QFormLayout;
    auto* path_edit = new QLineEdit;
    auto* browse = new QPushButton(QObject::tr("Browse…"));
    auto* row = new QHBoxLayout;
    row->addWidget(path_edit, 1);
    row->addWidget(browse);
    form->addRow(QObject::tr("Sims3Pack"), row);
    lay->addLayout(form);

    auto* summary = new QPlainTextEdit;
    summary->setReadOnly(true);
    summary->setMaximumHeight(160);
    lay->addWidget(summary);

    auto* table = new QTableWidget(0, 5);
    table->setHorizontalHeaderLabels({QObject::tr("Index"), QObject::tr("Name"),
                                      QObject::tr("Length"), QObject::tr("Offset"),
                                      QObject::tr("Package?")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    lay->addWidget(table, 1);

    auto* hint = new QLabel(
        QObject::tr("Read-only TS3Pack inspect (SimsWiki). Extract writes packaged payloads to a "
                    "folder. No Store download or DRM bypass. Extracted .package files can be "
                    "opened in SXPE."));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    nlohmann::json last_list = nlohmann::json::object();

    auto fill = [&](const nlohmann::json& env) {
        last_list = env;
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
        if (data.contains("summary") && data["summary"].is_array()) {
            for (const auto& line : data["summary"]) {
                if (line.is_string()) {
                    lines << QString::fromStdString(line.get<std::string>());
                }
            }
        } else {
            for (const auto& line : sxpe::commands::format_sims3pack_summary(data)) {
                lines << QString::fromStdString(line);
            }
        }
        summary->setPlainText(lines.join(QLatin1Char('\n')));
        if (data.contains("entries") && data["entries"].is_array()) {
            for (const auto& e : data["entries"]) {
                const int r = table->rowCount();
                table->insertRow(r);
                auto put = [&](int c, const QString& s, const QVariant& user = {}) {
                    auto* item = new QTableWidgetItem(s);
                    if (user.isValid()) {
                        item->setData(Qt::UserRole, user);
                    }
                    table->setItem(r, c, item);
                };
                const auto idx = static_cast<int>(e.value("index", 0u));
                put(0, QString::number(idx), idx);
                put(1, QString::fromStdString(e.value("name", std::string{})), idx);
                put(2, QString::number(static_cast<qulonglong>(e.value("length", 0ull))), idx);
                put(3, QString::number(static_cast<qulonglong>(e.value("offset", 0ull))), idx);
                put(4, e.value("looksLikePackage", false) ? QObject::tr("yes") : QObject::tr(""),
                    idx);
            }
        }
    };

    auto run_list = [&] {
        const auto p = path_edit->text().trimmed();
        if (p.isEmpty()) {
            QMessageBox::warning(&dlg, QObject::tr("Inspect Sims3Pack"),
                                 QObject::tr("Choose a .sims3pack file."));
            return;
        }
        auto env = bus.execute("sims3pack.list", {{"path", p.toStdString()}});
        fill(env);
    };

    auto selected_index = [&]() -> int {
        const auto rows = table->selectionModel()->selectedRows();
        if (rows.isEmpty()) {
            return -1;
        }
        auto* item = table->item(rows.front().row(), 0);
        if (!item) {
            return -1;
        }
        return item->data(Qt::UserRole).toInt();
    };

    auto extract_selected = [&] {
        const int idx = selected_index();
        if (idx < 0) {
            QMessageBox::warning(&dlg, QObject::tr("Extract"),
                                 QObject::tr("Select an entry to extract."));
            return;
        }
        const auto p = path_edit->text().trimmed();
        const auto dir = QFileDialog::getExistingDirectory(&dlg, QObject::tr("Extract to folder"));
        if (dir.isEmpty()) {
            return;
        }
        auto env = bus.execute("sims3pack.extract",
                               {{"path", p.toStdString()},
                                {"outDir", dir.toStdString()},
                                {"index", idx},
                                {"force", true}});
        if (!env.value("ok", false)) {
            QString msg = QObject::tr("Extract failed.");
            if (env.contains("error") && env["error"].is_object()) {
                msg = QString::fromStdString(env["error"].value("message", msg.toStdString()));
            }
            QMessageBox::warning(&dlg, QObject::tr("Extract"), msg);
            return;
        }
        const auto written = QString::fromStdString(env["data"].value("writtenPath", std::string{}));
        QMessageBox::information(&dlg, QObject::tr("Extract"),
                                 QObject::tr("Wrote:\n%1").arg(written));
        if (open_package && written.endsWith(QStringLiteral(".package"), Qt::CaseInsensitive)) {
            const auto ans = QMessageBox::question(
                &dlg, QObject::tr("Open package"),
                QObject::tr("Open the extracted package in SXPE?"));
            if (ans == QMessageBox::Yes) {
                open_package(written);
            }
        }
    };

    QObject::connect(browse, &QPushButton::clicked, &dlg, [&] {
        const auto p = QFileDialog::getOpenFileName(
            &dlg, QObject::tr("Open Sims3Pack"), {},
            QObject::tr("Sims3Pack (*.sims3pack);;All files (*)"));
        if (!p.isEmpty()) {
            path_edit->setText(p);
            run_list();
        }
    });

    auto* box = new QDialogButtonBox;
    auto* inspect = box->addButton(QObject::tr("Inspect"), QDialogButtonBox::ActionRole);
    auto* extract = box->addButton(QObject::tr("Extract selected…"), QDialogButtonBox::ActionRole);
    auto* close = box->addButton(QDialogButtonBox::Close);
    lay->addWidget(box);
    QObject::connect(inspect, &QPushButton::clicked, &dlg, run_list);
    QObject::connect(extract, &QPushButton::clicked, &dlg, extract_selected);
    QObject::connect(close, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(table, &QTableWidget::doubleClicked, &dlg, [&](const QModelIndex&) {
        extract_selected();
    });

    dlg.exec();
}




void show_create_sims3pack_dialog(QWidget* parent, sxpe::commands::Bus& bus) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Create Sims3Pack"));
    dlg.resize(640, 420);
    auto* lay = new QVBoxLayout(&dlg);

    auto* form = new QFormLayout;
    auto* source_edit = new QLineEdit;
    auto* source_browse = new QPushButton(QObject::tr("Browse…"));
    auto* source_row = new QHBoxLayout;
    source_row->addWidget(source_edit, 1);
    source_row->addWidget(source_browse);
    form->addRow(QObject::tr("Source folder"), source_row);

    auto* out_edit = new QLineEdit;
    auto* out_browse = new QPushButton(QObject::tr("Browse…"));
    auto* out_row = new QHBoxLayout;
    out_row->addWidget(out_edit, 1);
    out_row->addWidget(out_browse);
    form->addRow(QObject::tr("Output .sims3pack"), out_row);

    auto* display = new QLineEdit;
    form->addRow(QObject::tr("Display name"), display);
    auto* description = new QLineEdit;
    form->addRow(QObject::tr("Description"), description);
    auto* package_id = new QLineEdit;
    form->addRow(QObject::tr("Package id"), package_id);
    auto* package_type = new QLineEdit(QStringLiteral("Object"));
    form->addRow(QObject::tr("Type"), package_type);
    auto* package_subtype = new QLineEdit(QStringLiteral("0x00000000"));
    form->addRow(QObject::tr("SubType"), package_subtype);
    lay->addLayout(form);

    auto* hint = new QLabel(
        QObject::tr("Limited TS3Pack authoring: packs non-recursive *.package files from the "
                    "source folder. No Store upload, DRM, or DBPP. CRC values are placeholder "
                    "zeros (algorithm unknown)."));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto* create = box->addButton(QObject::tr("Create"), QDialogButtonBox::AcceptRole);
    lay->addWidget(box);

    QObject::connect(source_browse, &QPushButton::clicked, &dlg, [&] {
        const auto d = QFileDialog::getExistingDirectory(&dlg, QObject::tr("Choose package folder"));
        if (!d.isEmpty()) {
            source_edit->setText(d);
        }
    });
    QObject::connect(out_browse, &QPushButton::clicked, &dlg, [&] {
        const auto p = QFileDialog::getSaveFileName(
            &dlg, QObject::tr("Save Sims3Pack"), {},
            QObject::tr("Sims3Pack (*.sims3pack);;All files (*)"));
        if (!p.isEmpty()) {
            out_edit->setText(p);
        }
    });
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(create, &QPushButton::clicked, &dlg, [&] {
        const auto src = source_edit->text().trimmed();
        const auto out = out_edit->text().trimmed();
        if (src.isEmpty() || out.isEmpty()) {
            QMessageBox::warning(&dlg, QObject::tr("Create Sims3Pack"),
                                 QObject::tr("Choose a source folder and output path."));
            return;
        }
        nlohmann::json args{{"path", out.toStdString()},
                            {"sourceDir", src.toStdString()},
                            {"force", true}};
        if (!display->text().trimmed().isEmpty()) {
            args["displayName"] = display->text().trimmed().toStdString();
        }
        if (!description->text().trimmed().isEmpty()) {
            args["description"] = description->text().trimmed().toStdString();
        }
        if (!package_id->text().trimmed().isEmpty()) {
            args["packageId"] = package_id->text().trimmed().toStdString();
        }
        if (!package_type->text().trimmed().isEmpty()) {
            args["packageType"] = package_type->text().trimmed().toStdString();
        }
        if (!package_subtype->text().trimmed().isEmpty()) {
            args["packageSubType"] = package_subtype->text().trimmed().toStdString();
        }
        auto env = bus.execute("sims3pack.pack", args);
        if (!env.value("ok", false)) {
            QString msg = QObject::tr("Pack failed.");
            if (env.contains("error") && env["error"].is_object()) {
                msg = QString::fromStdString(env["error"].value("message", msg.toStdString()));
            }
            QMessageBox::warning(&dlg, QObject::tr("Create Sims3Pack"), msg);
            return;
        }
        const auto written =
            QString::fromStdString(env["data"].value("writtenPath", out.toStdString()));
        const auto count = env["data"].value("entryCount", 0u);
        QMessageBox::information(
            &dlg, QObject::tr("Create Sims3Pack"),
            QObject::tr("Wrote %1 entry(ies) to:\n%2").arg(count).arg(written));
        dlg.accept();
    });

    dlg.exec();
}

void show_check_for_update_dialog(QWidget* parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Check for update"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* status = new QLabel(QObject::tr("Checking GitHub Releases…"));
    status->setWordWrap(true);
    status->setTextInteractionFlags(Qt::TextBrowserInteraction);
    status->setOpenExternalLinks(true);
    lay->addWidget(status);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* open_btn = box->addButton(QObject::tr("Open releases page"), QDialogButtonBox::ActionRole);
    open_btn->setEnabled(false);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(box);

    const QString current = QCoreApplication::applicationVersion();
    const QUrl api(QStringLiteral("https://api.github.com/repos/tofb15/sxpe/releases/latest"));
    const QUrl releases_page(QStringLiteral("https://github.com/tofb15/sxpe/releases"));

    auto* nam = new QNetworkAccessManager(&dlg);
    QNetworkRequest req(api);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("SXPE/%1 (check-for-update)").arg(current));
    req.setRawHeader("Accept", "application/vnd.github+json");
    // Avoid hanging forever on offline networks.
    req.setTransferTimeout(15000);

    QNetworkReply* reply = nam->get(req);

    auto normalize = [](QString v) {
        v = v.trimmed();
        if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V'))) {
            v = v.mid(1);
        }
        // Drop pre-release / build metadata for a simple compare.
        const int plus = v.indexOf(QLatin1Char('+'));
        if (plus >= 0) {
            v = v.left(plus);
        }
        const int dash = v.indexOf(QLatin1Char('-'));
        if (dash >= 0) {
            v = v.left(dash);
        }
        return v;
    };

    auto parse_parts = [](const QString& v) {
        QList<int> parts;
        for (const QString& p : v.split(QLatin1Char('.'))) {
            bool ok = false;
            const int n = p.toInt(&ok);
            parts.push_back(ok ? n : 0);
        }
        while (parts.size() < 3) {
            parts.push_back(0);
        }
        return parts;
    };

    auto cmp_ver = [&](const QString& a, const QString& b) {
        const auto pa = parse_parts(normalize(a));
        const auto pb = parse_parts(normalize(b));
        const int n = qMax(pa.size(), pb.size());
        for (int i = 0; i < n; ++i) {
            const int x = i < pa.size() ? pa[i] : 0;
            const int y = i < pb.size() ? pb[i] : 0;
            if (x < y) {
                return -1;
            }
            if (x > y) {
                return 1;
            }
        }
        return 0;
    };

    QObject::connect(reply, &QNetworkReply::finished, &dlg, [=, &dlg]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (http == 404) {
                status->setText(QObject::tr(
                    "No GitHub Releases yet for this project.\n"
                    "You are running SXPE %1.\n"
                    "Releases page: <a href=\"%2\">%2</a>")
                                    .arg(current, releases_page.toString()));
                open_btn->setEnabled(true);
                QObject::connect(open_btn, &QPushButton::clicked, &dlg, [releases_page] {
                    QDesktopServices::openUrl(releases_page);
                });
                return;
            }
            status->setText(QObject::tr(
                "Could not check for updates (network or GitHub error).\n"
                "You are running SXPE %1.\n"
                "Error: %2\n"
                "Try again later, or open <a href=\"%3\">%3</a> in a browser.")
                                .arg(current, reply->errorString(), releases_page.toString()));
            open_btn->setEnabled(true);
            QObject::connect(open_btn, &QPushButton::clicked, &dlg, [releases_page] {
                QDesktopServices::openUrl(releases_page);
            });
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            status->setText(QObject::tr(
                "GitHub returned an unexpected response.\n"
                "You are running SXPE %1.\n"
                "Releases: <a href=\"%2\">%2</a>")
                                .arg(current, releases_page.toString()));
            open_btn->setEnabled(true);
            QObject::connect(open_btn, &QPushButton::clicked, &dlg, [releases_page] {
                QDesktopServices::openUrl(releases_page);
            });
            return;
        }

        const QJsonObject obj = doc.object();
        const QString tag = obj.value(QStringLiteral("tag_name")).toString();
        QString html = obj.value(QStringLiteral("html_url")).toString();
        if (html.isEmpty()) {
            html = releases_page.toString();
        }
        if (tag.isEmpty()) {
            status->setText(QObject::tr(
                "No release tag found yet.\n"
                "You are running SXPE %1.\n"
                "Releases: <a href=\"%2\">%2</a>")
                                .arg(current, releases_page.toString()));
            open_btn->setEnabled(true);
            QObject::connect(open_btn, &QPushButton::clicked, &dlg, [releases_page] {
                QDesktopServices::openUrl(releases_page);
            });
            return;
        }

        const int cmp = cmp_ver(current, tag);
        const QUrl release_url(html);
        open_btn->setEnabled(true);
        QObject::connect(open_btn, &QPushButton::clicked, &dlg, [release_url] {
            QDesktopServices::openUrl(release_url);
        });

        if (cmp < 0) {
            status->setText(QObject::tr(
                "A newer release is available.\n"
                "You have SXPE %1; latest is %2.\n"
                "SXPE does not download updates automatically — open the release page "
                "and install when you choose.\n"
                "<a href=\"%3\">%3</a>")
                                .arg(current, tag, html));
        } else if (cmp == 0) {
            status->setText(QObject::tr(
                "You are up to date.\n"
                "Running SXPE %1 (matches latest release %2).\n"
                "Releases: <a href=\"%3\">%3</a>")
                                .arg(current, tag, html));
        } else {
            status->setText(QObject::tr(
                "You appear newer than the latest GitHub Release "
                "(dev or local build).\n"
                "Running SXPE %1; latest published is %2.\n"
                "Releases: <a href=\"%3\">%3</a>")
                                .arg(current, tag, html));
        }
    });

    dlg.resize(480, 220);
    dlg.exec();
}



namespace {

QString find_repo_doc(const QString& relative) {
    const QString dir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        dir + QStringLiteral("/../") + relative,
        dir + QStringLiteral("/../../") + relative,
        dir + QStringLiteral("/../../../") + relative,
        QDir::current().absoluteFilePath(relative),
    };
    for (const auto& p : candidates) {
        if (QFileInfo::exists(p)) {
            return QFileInfo(p).absoluteFilePath();
        }
    }
    return {};
}

QString format_bytes(qint64 bytes) {
    if (bytes < 1024) {
        return QObject::tr("%1 B").arg(bytes);
    }
    const double kib = bytes / 1024.0;
    if (kib < 1024.0) {
        return QObject::tr("%1 KiB").arg(kib, 0, 'f', 1);
    }
    const double mib = kib / 1024.0;
    if (mib < 1024.0) {
        return QObject::tr("%1 MiB").arg(mib, 0, 'f', 1);
    }
    return QObject::tr("%1 GiB").arg(mib / 1024.0, 0, 'f', 2);
}

bool is_mergeable_package(const QFileInfo& fi) {
    if (!fi.isFile()) {
        return false;
    }
    const auto suf = fi.suffix().toLower();
    return suf == QLatin1String("package") || suf == QLatin1String("dbc");
}

QStringList collect_packages_in_folder(const QString& folder, bool recursive) {
    QStringList out;
    if (folder.isEmpty() || !QDir(folder).exists()) {
        return out;
    }
    QDirIterator::IteratorFlags flags = QDirIterator::NoIteratorFlags;
    if (recursive) {
        flags |= QDirIterator::Subdirectories;
    }
    QDirIterator it(folder,
                    QStringList{QStringLiteral("*.package"), QStringLiteral("*.dbc"),
                                QStringLiteral("*.PACKAGE"), QStringLiteral("*.DBC")},
                    QDir::Files | QDir::Readable | QDir::NoSymLinks, flags);
    while (it.hasNext()) {
        it.next();
        const auto fi = it.fileInfo();
        if (is_mergeable_package(fi)) {
            out.push_back(fi.absoluteFilePath());
        }
    }
    out.sort(Qt::CaseInsensitive);
    out.removeDuplicates();
    return out;
}

qint64 total_size_of(const QStringList& paths) {
    qint64 total = 0;
    for (const auto& p : paths) {
        total += QFileInfo(p).size();
    }
    return total;
}

}  // namespace

void show_common_tasks_dialog(QWidget* parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Common tasks"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* view = new QTextBrowser;
    view->setOpenExternalLinks(true);
    const auto workflows = find_repo_doc(QStringLiteral("docs/workflows.md"));
    const QString workflows_link =
        workflows.isEmpty()
            ? QStringLiteral("https://github.com/tofb15/sxpe/blob/dev/docs/workflows.md")
            : QUrl::fromLocalFile(workflows).toString();
    view->setHtml(QObject::tr(
                      "<h2>Common tasks</h2>"
                      "<p>Short recipes for everyday mod work. Full step-by-step: "
                      "<a href=\"%1\">workflows.md</a>.</p>"
                      "<h3>Merge a folder of packages into one file</h3>"
                      "<ol>"
                      "<li>Put the <b>.package</b> files you want to combine in one folder "
                      "(work on <b>copies</b>).</li>"
                      "<li><b>Tools → Merge packages…</b> opens the <b>Merge assistant</b>.</li>"
                      "<li>Choose the folder (or pick files) → check the preview count and size → "
                      "<b>Merge</b>.</li>"
                      "<li>Optional: tick <b>Validate after merge</b>.</li>"
                      "<li><b>File → Save As…</b> to write the new combined package. "
                      "Originals are never changed.</li>"
                      "</ol>"
                      "<p>SXPE writes an <b>SXMM</b> manifest so <b>Tools → Un-merge package…</b> "
                      "can reverse SXPE merges later.</p>"
                      "<h3>Open / edit a package</h3>"
                      "<p><b>File → Open…</b> (or drop one file). Edit resources, then Save. "
                      "Use <b>Tools → Validate</b> before you share.</p>"
                      "<h3>Clean Downloads / Mods folders</h3>"
                      "<p><b>Tools → Scan folder…</b> — read-only hygiene. SXPE never auto-deletes.</p>"
                      "<h3>Inspect a Sims3Pack</h3>"
                      "<p><b>File → Open Sims3Pack…</b> or <b>Tools → Inspect Sims3Pack…</b>, "
                      "then extract embedded packages.</p>"
                      "<h3>Coming from s3pe?</h3>"
                      "<p>See the README / user guide section <b>If you used s3pe before</b> "
                      "for what maps where. The Merge assistant replaces the old "
                      "“drop everything and hope” flow with a preview and safe caps.</p>")
                      .arg(workflows_link));
    lay->addWidget(view, 1);
    auto* row = new QHBoxLayout;
    auto* open_doc = new QPushButton(QObject::tr("Open workflows.md…"));
    open_doc->setEnabled(!workflows.isEmpty());
    QObject::connect(open_doc, &QPushButton::clicked, &dlg, [workflows] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(workflows));
    });
    auto* web = new QPushButton(QObject::tr("Online copy"));
    QObject::connect(web, &QPushButton::clicked, &dlg, [] {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://github.com/tofb15/sxpe/blob/dev/docs/workflows.md")));
    });
    row->addWidget(open_doc);
    row->addWidget(web);
    row->addStretch(1);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    row->addWidget(box);
    lay->addLayout(row);
    dlg.resize(640, 520);
    dlg.exec();
}

void show_merge_assistant_dialog(
    QWidget* parent,
    const std::function<void(const QStringList& paths, bool validate_after)>& on_merge) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Merge assistant"));
    auto* lay = new QVBoxLayout(&dlg);

    auto* intro = new QLabel(QObject::tr(
        "Combine several Sims 3 <b>.package</b> files into one new untitled package.<br/>"
        "Your originals are never changed. Prefer working on copies."));
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    lay->addWidget(intro);

    auto* path_row = new QHBoxLayout;
    auto* path_edit = new QLineEdit;
    path_edit->setPlaceholderText(QObject::tr("Folder or selected files…"));
    path_edit->setReadOnly(true);
    auto* pick_folder = new QPushButton(QObject::tr("Choose folder…"));
    auto* pick_files = new QPushButton(QObject::tr("Choose files…"));
    path_row->addWidget(path_edit, 1);
    path_row->addWidget(pick_folder);
    path_row->addWidget(pick_files);
    lay->addLayout(path_row);

    auto* recursive = new QCheckBox(QObject::tr("Include subfolders (folder mode)"));
    recursive->setChecked(false);
    lay->addWidget(recursive);

    auto* preview = new QLabel(QObject::tr("No packages selected."));
    preview->setWordWrap(true);
    preview->setTextFormat(Qt::RichText);
    lay->addWidget(preview);

    auto* list = new QListWidget;
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setMinimumHeight(140);
    lay->addWidget(list, 1);

    auto* validate_after = new QCheckBox(QObject::tr("Validate after merge (recommended)"));
    validate_after->setChecked(true);
    lay->addWidget(validate_after);

    auto* note = new QLabel(QObject::tr(
        "Merge writes an <b>SXMM</b> manifest, strips known leftover Sims3Pack manifests, "
        "and uses the same bus caps / progress / Cancel as CLI "
        "(<code>resource.importPackage</code>). Save with <b>File → Save As…</b> when done."));
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    lay->addWidget(note);

    QStringList selected;

    auto refresh = [&] {
        list->clear();
        if (selected.isEmpty()) {
            preview->setText(QObject::tr("No packages selected."));
            return;
        }
        const auto bytes = total_size_of(selected);
        preview->setText(QObject::tr("Ready: <b>%1</b> package(s) · <b>%2</b> total")
                             .arg(selected.size())
                             .arg(format_bytes(bytes)));
        const int show = static_cast<int>(std::min<qsizetype>(selected.size(), 200));
        for (int i = 0; i < show; ++i) {
            list->addItem(selected[i]);
        }
        if (selected.size() > show) {
            list->addItem(QObject::tr("… and %1 more").arg(selected.size() - show));
        }
    };

    QObject::connect(pick_folder, &QPushButton::clicked, &dlg, [&] {
        const auto folder = QFileDialog::getExistingDirectory(
            &dlg, QObject::tr("Choose folder of packages to merge"));
        if (folder.isEmpty()) {
            return;
        }
        selected = collect_packages_in_folder(folder, recursive->isChecked());
        path_edit->setText(folder);
        refresh();
        if (selected.size() < 2) {
            QMessageBox::information(
                &dlg, QObject::tr("Merge assistant"),
                QObject::tr("Need at least two .package / .dbc files in that folder%1.")
                    .arg(recursive->isChecked() ? QObject::tr(" (including subfolders)")
                                                : QString()));
        }
    });

    QObject::connect(recursive, &QCheckBox::toggled, &dlg, [&](bool) {
        const auto folder = path_edit->text();
        if (folder.isEmpty() || !QDir(folder).exists()) {
            return;
        }
        if (QFileInfo(folder).isDir()) {
            selected = collect_packages_in_folder(folder, recursive->isChecked());
            refresh();
        }
    });

    QObject::connect(pick_files, &QPushButton::clicked, &dlg, [&] {
        const auto paths = QFileDialog::getOpenFileNames(
            &dlg, QObject::tr("Choose packages to merge"), {},
            QObject::tr("Packages (*.package *.dbc);;All (*.*)"));
        if (paths.isEmpty()) {
            return;
        }
        selected = paths;
        selected.sort(Qt::CaseInsensitive);
        selected.removeDuplicates();
        if (selected.size() == 1) {
            path_edit->setText(selected.front());
        } else {
            path_edit->setText(QObject::tr("%1 files selected").arg(selected.size()));
        }
        refresh();
        if (selected.size() < 2) {
            QMessageBox::information(
                &dlg, QObject::tr("Merge assistant"),
                QObject::tr(
                    "Select at least two packages to merge into a new untitled package.\n"
                    "To import into the open tab, use Resource → Import → "
                    "From package(s) into this package…"));
        }
    });

    auto* buttons = new QDialogButtonBox;
    auto* merge_btn = buttons->addButton(QObject::tr("Merge"), QDialogButtonBox::AcceptRole);
    auto* help_btn =
        buttons->addButton(QObject::tr("Common tasks…"), QDialogButtonBox::HelpRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    lay->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(help_btn, &QPushButton::clicked, &dlg,
                     [&dlg] { show_common_tasks_dialog(&dlg); });
    QObject::connect(merge_btn, &QPushButton::clicked, &dlg, [&] {
        if (selected.size() < 2) {
            QMessageBox::information(
                &dlg, QObject::tr("Merge assistant"),
                QObject::tr("Choose a folder or at least two package files first."));
            return;
        }
        const auto bytes = total_size_of(selected);
        const auto reply = QMessageBox::question(
            &dlg, QObject::tr("Merge assistant"),
            QObject::tr("Merge %1 package(s) (%2) into a new untitled package?\n\n"
                        "Originals stay untouched. You will Save As when finished.")
                .arg(selected.size())
                .arg(format_bytes(bytes)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (reply != QMessageBox::Yes) {
            return;
        }
        const auto paths = selected;
        const bool do_validate = validate_after->isChecked();
        dlg.accept();
        if (on_merge) {
            on_merge(paths, do_validate);
        }
    });

    dlg.resize(720, 520);
    dlg.exec();
}

void show_first_run_tip_if_needed(QWidget* parent, bool smoke_mode,
                                  const std::function<void()>& open_merge_assistant) {
    if (smoke_mode) {
        return;
    }
    QSettings st(QStringLiteral("SXPE"), QStringLiteral("SXPE"));
    if (st.value(QStringLiteral("onboarding/seenFirstRunTip"), false).toBool()) {
        return;
    }
    st.setValue(QStringLiteral("onboarding/seenFirstRunTip"), true);
    QMessageBox box(parent);
    box.setWindowTitle(QObject::tr("Welcome to SXPE"));
    box.setIcon(QMessageBox::Information);
    box.setText(QObject::tr("New here? Start with Help → Common tasks."));
    box.setInformativeText(QObject::tr(
        "To combine a folder of custom-content packages into one file, use "
        "Tools → Merge packages… (Merge assistant). "
        "It previews count and size, merges safely with an SXMM manifest, "
        "and can validate afterwards — no MTS lore required."));
    auto* tasks = box.addButton(QObject::tr("Common tasks…"), QMessageBox::AcceptRole);
    auto* merge = box.addButton(QObject::tr("Merge assistant…"), QMessageBox::ActionRole);
    box.addButton(QObject::tr("Dismiss"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == tasks) {
        show_common_tasks_dialog(parent);
    } else if (box.clickedButton() == merge && open_merge_assistant) {
        open_merge_assistant();
    }
}

}  // namespace sxpe::gui
