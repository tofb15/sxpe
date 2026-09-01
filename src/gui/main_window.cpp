#include "main_window.hpp"

#include "dialogs.hpp"
#include "package_tab.hpp"
#include "palette.hpp"

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QStatusBar>
#include <QStyleHints>
#include <QTabWidget>

namespace sxpe::gui {
namespace {

QString filters() {
    return QObject::tr("Sims 3 packages (*.package *.world *.dbc *.nhd);;All files (*.*)");
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("SXPE"));
    setAcceptDrops(true);
    resize(1100, 720);

    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(true);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    setCentralWidget(tabs_);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::close_tab);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int) { update_status(); });

    auto act = [&](QMenu* m, const QString& name, const QKeySequence& ks, auto slot) {
        auto* a = m->addAction(name);
        a->setShortcut(ks);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };

    auto* file = menuBar()->addMenu(tr("&File"));
    act(file, tr("&New"), QKeySequence::New, [this] { new_package(); });
    act(file, tr("&Open…"), QKeySequence::Open, [this] { open_dialog(); });
    act(file, tr("&Save"), QKeySequence::Save, [this] { save(false, false); });
    act(file, tr("Save &As…"), QKeySequence::SaveAs, [this] { save(false, true); });
    act(file, tr("Save &Copy As…"), {}, [this] { save(true, true); });
    act(file, tr("&Close"), QKeySequence::Close, [this] { close_tab(tabs_->currentIndex()); });
    file->addSeparator();
    mru_menu_ = file->addMenu(tr("&Recent"));
    file->addSeparator();
    act(file, tr("E&xit"), QKeySequence::Quit, [this] { close(); });

    auto* edit = menuBar()->addMenu(tr("&Edit"));
    act(edit, tr("&Undo"), QKeySequence::Undo, [this] {
        if (auto* t = current_tab()) {
            run("undo", {{"sessionId", t->session_id().toStdString()}});
            t->reload();
        }
    });
    act(edit, tr("&Copy"), QKeySequence::Copy, [this] {
        if (auto* t = current_tab()) {
            if (const auto* r = t->current()) {
                run("resource.copy", {{"sessionId", t->session_id().toStdString()},
                                      {"resourceId",
                                       {{"type", r->type},
                                        {"group", r->group},
                                        {"instance", r->instance},
                                        {"ordinal", r->ordinal}}}});
            }
        }
    });
    act(edit, tr("&Paste"), QKeySequence::Paste, [this] {
        if (auto* t = current_tab()) {
            run("resource.paste", {{"sessionId", t->session_id().toStdString()}});
            t->reload();
        }
    });
    act(edit, tr("Select &All"), QKeySequence::SelectAll, [this] {
        if (auto* t = current_tab()) {
            t->select_all();
        }
    });
    act(edit, tr("Command &palette…"), QKeySequence(Qt::CTRL | Qt::Key_K), [this] { run_palette(); });

    auto* res = menuBar()->addMenu(tr("&Resource"));
    act(res, tr("&Export…"), {}, [this] { export_resource(); });
    act(res, tr("&Import package…"), {}, [this] {
        if (auto* t = current_tab()) {
            show_import_dialog(this, bus_, t->session_id(), false);
            t->reload();
        }
    });
    act(res, tr("Import &DBC…"), {}, [this] {
        if (auto* t = current_tab()) {
            show_import_dialog(this, bus_, t->session_id(), true);
            t->reload();
        }
    });
    act(res, tr("Import &files…"), {}, [this] {
        auto* t = current_tab();
        if (!t) {
            return;
        }
        const auto path = QFileDialog::getOpenFileName(this, tr("Import file"));
        if (path.isEmpty()) {
            return;
        }
        run("resource.importFiles", {{"sessionId", t->session_id().toStdString()},
                                     {"path", path.toStdString()},
                                     {"force", true}});
        t->reload();
    });
    act(res, tr("&Duplicate"), {}, [this] { duplicate_resource(); });
    act(res, tr("&Delete"), QKeySequence::Delete, [this] { delete_resource(); });
    act(res, tr("D&etails…"), {}, [this] {
        auto* t = current_tab();
        const auto* r = t ? t->current() : nullptr;
        if (!t || !r) {
            return;
        }
        show_details_dialog(this, bus_, t->session_id(), r->type, r->group, r->instance, r->ordinal,
                            r->name, r->compressed, r->deleted);
        t->reload();
    });
    act(res, tr("&Float preview"), {}, [this] {
        if (auto* t = current_tab()) {
            t->float_preview();
        }
    });
    act(res, tr("Open in &text editor"), {}, [this] { open_external(false); });
    act(res, tr("Open in &hex editor"), {}, [this] { open_external(true); });

    auto* tools = menuBar()->addMenu(tr("&Tools"));
    act(tools, tr("&FNV hash…"), {}, [this] { show_fnv_dialog(this, bus_); });
    act(tools, tr("&Search…"), QKeySequence::Find, [this] {
        if (auto* t = current_tab()) {
            show_search_dialog(this, bus_, t->session_id());
        }
    });
    act(tools, tr("&Validate"), {}, [this] {
        if (auto* t = current_tab()) {
            auto env = run("package.validate", {{"sessionId", t->session_id().toStdString()}});
            QMessageBox::information(this, tr("Validate"), QString::fromStdString(env.dump(2)));
        }
    });
    act(tools, tr("&Compact / save"), {}, [this] {
        if (auto* t = current_tab()) {
            run("package.compact", {{"sessionId", t->session_id().toStdString()}});
            t->reload();
        }
    });

    auto* settings = menuBar()->addMenu(tr("&Settings"));
    act(settings, tr("&Handlers / plugins…"), {},
        [this] { show_handlers_dialog(this, bus_, plugins_); });
    act(settings, tr("&External programs…"), {},
        [this] { show_external_programs_dialog(this); });

    auto* help = menuBar()->addMenu(tr("&Help"));
    act(help, tr("&About SXPE"), {}, [this] {
        QMessageBox::about(
            this, tr("About SXPE"),
            tr("SXPE is an unofficial Sims 3 package editor.\n"
               "Not affiliated with Electronic Arts. Not s3pe.\n"
               "License: GPL-3.0-or-later.\n"
               "The Sims 3 is a trademark of Electronic Arts."));
    });

    status_path_ = new QLabel(tr("No package"));
    status_counts_ = new QLabel;
    statusBar()->addWidget(status_path_, 1);
    statusBar()->addPermanentWidget(status_counts_);

    plugins_.scan();
    QSettings st("SXPE", "SXPE");
    mru_ = st.value("mru").toStringList();
    rebuild_mru();

    auto* empty = new QLabel(tr("Open a package (Ctrl+O) or drop a .package file here."));
    empty->setAlignment(Qt::AlignCenter);
    empty->setObjectName("empty");
    tabs_->addTab(empty, tr("Start"));
}

void MainWindow::add_tab(const QString& session_id, const QString& title) {
    if (tabs_->count() == 1 && tabs_->widget(0)->objectName() == QLatin1String("empty")) {
        tabs_->removeTab(0);
    }
    auto* tab = new PackageTab(bus_, session_id, tabs_);
    connect(tab, &PackageTab::status_changed, this, &MainWindow::update_status);
    const int i = tabs_->addTab(tab, title);
    tabs_->setCurrentIndex(i);
    update_status();
}

void MainWindow::new_package() {
    auto env = bus_.execute("package.new", nlohmann::json::object());
    if (!env.value("ok", false)) {
        warn_if_err(env);
        return;
    }
    add_tab(QString::fromStdString(env["data"]["sessionId"].get<std::string>()), tr("Untitled"));
}

bool MainWindow::open_path(const QString& path, bool writable) {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto env = bus_.execute("package.open", {{"path", path.toStdString()}, {"writable", writable}});
    if (!env.value("ok", false)) {
        QApplication::restoreOverrideCursor();
        warn_if_err(env);
        return false;
    }
    const auto sid = QString::fromStdString(env["data"]["sessionId"].get<std::string>());
    add_tab(sid, QFileInfo(path).fileName());
    remember_mru(path);
    QApplication::restoreOverrideCursor();
    return true;
}

void MainWindow::open_dialog() {
    const auto path = QFileDialog::getOpenFileName(this, tr("Open package"), {}, filters());
    if (!path.isEmpty()) {
        open_path(path, true);
    }
}

bool MainWindow::save(bool as_copy, bool save_as) {
    auto* t = current_tab();
    if (!t) {
        return false;
    }
    QString dest;
    if (save_as || as_copy) {
        dest = QFileDialog::getSaveFileName(this, tr("Save package"), {}, filters());
        if (dest.isEmpty()) {
            return false;
        }
    }
    nlohmann::json args{{"sessionId", t->session_id().toStdString()}, {"force", true}};
    const char* cmd = "package.save";
    if (as_copy) {
        cmd = "package.saveCopyAs";
        args["path"] = dest.toStdString();
    } else if (save_as) {
        cmd = "package.saveAs";
        args["path"] = dest.toStdString();
    }
    auto env = run(cmd, args);
    if (!env.value("ok", false)) {
        return false;
    }
    if (!dest.isEmpty() && !as_copy) {
        tabs_->setTabText(tabs_->currentIndex(), QFileInfo(dest).fileName());
        remember_mru(dest);
    }
    t->reload();
    return true;
}

void MainWindow::close_tab(int index) {
    if (index < 0 || index >= tabs_->count()) {
        return;
    }
    auto* t = qobject_cast<PackageTab*>(tabs_->widget(index));
    if (t) {
        bus_.execute("package.close", {{"sessionId", t->session_id().toStdString()}});
    }
    tabs_->removeTab(index);
    if (tabs_->count() == 0) {
        auto* empty = new QLabel(tr("Open a package (Ctrl+O) or drop a .package file here."));
        empty->setAlignment(Qt::AlignCenter);
        empty->setObjectName("empty");
        tabs_->addTab(empty, tr("Start"));
    }
    update_status();
}

PackageTab* MainWindow::current_tab() const {
    return qobject_cast<PackageTab*>(tabs_->currentWidget());
}

void MainWindow::update_status() {
    auto* t = current_tab();
    if (!t) {
        status_path_->setText(tr("No package"));
        status_counts_->clear();
        return;
    }
    auto info = bus_.execute("package.info", {{"sessionId", t->session_id().toStdString()}});
    if (!info.value("ok", false)) {
        return;
    }
    const auto& d = info["data"];
    QString path = QString::fromStdString(d.value("path", std::string()));
    if (path.isEmpty()) {
        path = tr("Untitled");
    }
    if (d.value("dirty", false)) {
        path += " *";
    }
    status_path_->setText(path);
    status_counts_->setText(tr("%1 shown / %2 in package")
                                .arg(t->visible_count())
                                .arg(d.value("indexCount", 0)));
}

void MainWindow::run_palette() {
    CommandPalette pal(bus_, this);
    if (pal.exec() != QDialog::Accepted) {
        return;
    }
    const auto id = pal.selected_id();
    if (id == "package.new") {
        new_package();
    } else if (id == "package.open") {
        open_dialog();
    } else if (id == "package.save") {
        save(false, false);
    } else if (id == "ui.palette") {
        return;
    } else if (id.contains("fnv")) {
        show_fnv_dialog(this, bus_);
    } else if (auto* t = current_tab()) {
        auto env = bus_.execute(id.toStdString(), {{"sessionId", t->session_id().toStdString()}});
        if (!env.value("ok", false)) {
            warn_if_err(env);
        }
        t->reload();
    }
}

void MainWindow::remember_mru(const QString& path) {
    mru_.removeAll(path);
    mru_.prepend(path);
    while (mru_.size() > 12) {
        mru_.removeLast();
    }
    QSettings("SXPE", "SXPE").setValue("mru", mru_);
    rebuild_mru();
}

void MainWindow::rebuild_mru() {
    if (!mru_menu_) {
        return;
    }
    mru_menu_->clear();
    for (const auto& p : mru_) {
        mru_menu_->addAction(p, this, [this, p] { open_path(p, true); });
    }
    mru_menu_->setEnabled(!mru_.isEmpty());
}

nlohmann::json MainWindow::run(const char* id, nlohmann::json args) {
    auto env = bus_.execute(id, args);
    warn_if_err(env);
    return env;
}

void MainWindow::warn_if_err(const nlohmann::json& env) {
    if (env.value("ok", false)) {
        return;
    }
    QString msg = tr("Command failed");
    if (env.contains("error") && env["error"].contains("message")) {
        msg = QString::fromStdString(env["error"]["message"].get<std::string>());
    }
    QMessageBox::warning(this, tr("SXPE"), msg);
}

void MainWindow::export_resource() {
    auto* t = current_tab();
    if (!t || !t->current()) {
        return;
    }
    const auto path = QFileDialog::getSaveFileName(this, tr("Export resource"));
    if (path.isEmpty()) {
        return;
    }
    if (!t->export_selected(path, false)) {
        QMessageBox::warning(this, tr("SXPE"), tr("Export failed."));
    }
}

void MainWindow::delete_resource() {
    auto* t = current_tab();
    const auto* r = t ? t->current() : nullptr;
    if (!t || !r) {
        return;
    }
    if (QMessageBox::question(this, tr("Delete"), tr("Remove the selected resource from the package?")) !=
        QMessageBox::Yes) {
        return;
    }
    run("resource.delete", {{"sessionId", t->session_id().toStdString()},
                            {"resourceId",
                             {{"type", r->type},
                              {"group", r->group},
                              {"instance", r->instance},
                              {"ordinal", r->ordinal}}}});
    t->reload();
}

void MainWindow::duplicate_resource() {
    auto* t = current_tab();
    const auto* r = t ? t->current() : nullptr;
    if (!t || !r) {
        return;
    }
    run("resource.duplicate", {{"sessionId", t->session_id().toStdString()},
                               {"resourceId",
                                {{"type", r->type},
                                 {"group", r->group},
                                 {"instance", r->instance},
                                 {"ordinal", r->ordinal}}}});
    t->reload();
}

void MainWindow::open_external(bool hex) {
    auto* t = current_tab();
    if (!t || !t->current()) {
        return;
    }
    QSettings s("SXPE", "SXPE");
    const auto cmd = s.value(hex ? "ext/hex" : "ext/text").toString();
    if (cmd.isEmpty()) {
        QMessageBox::information(this, tr("SXPE"),
                                 tr("Set a program under Settings → External programs. Use {path}."));
        return;
    }
    const auto tmp = QDir::temp().filePath("sxpe-ext.bin");
    if (!t->export_selected(tmp, false)) {
        return;
    }
    QString err;
    if (!plugins_.run_user_command(cmd, tmp, &err)) {
        QMessageBox::warning(this, tr("SXPE"), err);
    }
}

bool MainWindow::smoke_filter(const QString& text) {
    auto* t = current_tab();
    if (!t) {
        return false;
    }
    t->apply_filter();
    (void)text;
    return t->visible_count() >= 0;
}

void MainWindow::closeEvent(QCloseEvent* e) {
    while (auto* t = current_tab()) {
        close_tab(tabs_->indexOf(t));
    }
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* e) {
    for (const auto& u : e->mimeData()->urls()) {
        const auto p = u.toLocalFile();
        if (!p.isEmpty()) {
            open_path(p, true);
        }
    }
}

}  // namespace sxpe::gui
