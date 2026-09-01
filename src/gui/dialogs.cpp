#include "dialogs.hpp"

#include "sxpe/resources/types.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

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
                         const sxpe::commands::UiRow& row) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Resource details"));
    auto* form = new QFormLayout(&dlg);
    auto* type = new QLineEdit(QString("%1").arg(row.type, 8, 16, QLatin1Char('0')).toUpper());
    auto* group = new QLineEdit(QString("%1").arg(row.group, 8, 16, QLatin1Char('0')).toUpper());
    auto* inst =
        new QLineEdit(QString("%1").arg(row.instance, 16, 16, QLatin1Char('0')).toUpper());
    auto* name = new QLineEdit(QString::fromStdString(row.name));
    auto* cmp = new QCheckBox(QObject::tr("Compressed"));
    auto* del = new QCheckBox(QObject::tr("Deleted"));
    cmp->setChecked(row.compressed);
    del->setChecked(row.deleted);
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
        nlohmann::json rid{{"type", row.type},
                           {"group", row.group},
                           {"instance", row.instance},
                           {"ordinal", row.ordinal}};
        bool ok = true;
        const auto nt = type->text().toUInt(nullptr, 16);
        const auto ng = group->text().toUInt(nullptr, 16);
        const auto ni = inst->text().toULongLong(nullptr, 16);
        if (nt != row.type || ng != row.group || ni != row.instance) {
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
