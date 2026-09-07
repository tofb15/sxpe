#include "main_window.hpp"

#include "dialogs.hpp"
#include "package_tab.hpp"
#include "palette.hpp"
#include "resource_model.hpp"

#include "sxpe/resources/types.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QCursor>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace sxpe::gui {
namespace {

QString filters() {
    return QObject::tr("Sims 3 packages (*.package *.world *.dbc *.nhd);;All files (*.*)");
}

QString tab_label(const QString& path, bool writable, bool dirty) {
    if (path.isEmpty()) {
        return dirty ? QObject::tr("Untitled *") : QObject::tr("Untitled");
    }
    const QFileInfo fi(path);
    const auto folder = fi.dir().dirName();
    QString name = fi.fileName();
    if (!folder.isEmpty() && folder != QLatin1String(".") && folder != QLatin1String("\\")) {
        name = folder + QStringLiteral(" — ") + name;
    }
    if (!writable) {
        name += QObject::tr(" [read-only]");
    }
    if (dirty) {
        name += QLatin1Char('*');
    }
    return name;
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
    tabs_->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabs_->tabBar(), &QWidget::customContextMenuRequested, this,
            &MainWindow::show_tab_context);

    auto act = [&](QMenu* m, const QString& name, const QKeySequence& ks, auto slot) {
        auto* a = m->addAction(name);
        a->setShortcut(ks);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };

    auto* file = menuBar()->addMenu(tr("&File"));
    act(file, tr("&New"), QKeySequence::New, [this] { new_package(); });
    act(file, tr("&Open…"), QKeySequence::Open, [this] { open_dialog(); });
    act(file, tr("Open &read-only…"), {}, [this] { open_readonly_dialog(); });
    act(file, tr("&Save"), QKeySequence::Save, [this] { save(false, false); });
    act(file, tr("Save &As…"), QKeySequence::SaveAs, [this] { save(false, true); });
    act(file, tr("Save &Copy As…"), {}, [this] { save(true, true); });
    act(file, tr("&Close"), QKeySequence::Close, [this] { close_tab(tabs_->currentIndex()); });
    file->addSeparator();
    mru_menu_ = file->addMenu(tr("&Recent"));
    bookmarks_menu_ = file->addMenu(tr("&Bookmarks"));
    file->addSeparator();
    act(file, tr("&Bookmark current package"), {}, [this] { bookmark_current(); });
    act(file, tr("&Organise bookmarks…"), {}, [this] { organise_bookmarks(); });
    file->addSeparator();
    act(file, tr("E&xit"), QKeySequence::Quit, [this] { close(); });

    auto* edit = menuBar()->addMenu(tr("&Edit"));
    act(edit, tr("&Undo"), QKeySequence::Undo, [this] {
        if (auto* t = current_tab()) {
            run("undo", {{"sessionId", t->session_id().toStdString()}});
            t->reload();
        }
    });
    act(edit, tr("&Redo"), QKeySequence::Redo, [this] {
        if (auto* t = current_tab()) {
            run("redo", {{"sessionId", t->session_id().toStdString()}});
            t->reload();
        }
    });
    edit->addSeparator();
    act(edit, tr("&Copy preview"), {}, [this] { copy_preview(); });
    act(edit, tr("&Save preview…"), {}, [this] { save_preview(); });
    act(edit, tr("&Float preview"), {}, [this] {
        if (auto* t = current_tab()) {
            t->float_preview();
        }
    });
    act(edit, tr("Open in &text editor"), {}, [this] { open_external(false); });
    edit->addSeparator();
    act(edit, tr("Select &All"), QKeySequence::SelectAll, [this] {
        if (auto* t = current_tab()) {
            t->select_all();
        }
    });
    act(edit, tr("Command &palette…"), QKeySequence(Qt::CTRL | Qt::Key_K), [this] { run_palette(); });

    auto* view = menuBar()->addMenu(tr("&View"));
    columns_menu_ = view->addMenu(tr("&Columns"));
    connect(columns_menu_, &QMenu::aboutToShow, this, &MainWindow::rebuild_columns_menu);

    auto* res = menuBar()->addMenu(tr("&Resource"));
    add_resource_act_ =
        act(res, tr("&Add…"), QKeySequence(Qt::CTRL | Qt::Key_I), [this] { add_resource(); });
    act(res, tr("&Copy"), QKeySequence::Copy, [this] { copy_resources(); });
    paste_resource_act_ =
        act(res, tr("&Paste"), QKeySequence::Paste, [this] { paste_resources(); });
    duplicate_resource_act_ = act(res, tr("&Duplicate"), QKeySequence(Qt::CTRL | Qt::Key_D),
                                  [this] { duplicate_resource(); });
    act(res, tr("&Replace…"), {}, [this] { replace_resource(); });
    res->addSeparator();
    compressed_act_ = res->addAction(tr("&Compressed"));
    compressed_act_->setCheckable(true);
    connect(compressed_act_, &QAction::triggered, this, [this](bool on) { set_compressed(on); });
    deleted_act_ = res->addAction(tr("De&leted flag"));
    deleted_act_->setCheckable(true);
    connect(deleted_act_, &QAction::triggered, this, [this](bool on) { set_deleted(on); });
    act(res, tr("D&etails…"), {}, [this] { details_resource(); });
    res->addSeparator();
    act(res, tr("Select A&ll"), {}, [this] {
        if (auto* t = current_tab()) {
            t->select_all();
        }
    });
    act(res, tr("Copy resource &key"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C),
        [this] { copy_resource_key(); });
    import_menu_ = res->addMenu(tr("&Import"));
    auto* imp = import_menu_;
    act(imp, tr("From &file…"), {}, [this] { import_files(); });
    act(imp, tr("From &package(s)…"), {}, [this] {
        if (auto* t = current_tab()) {
            show_import_dialog(this, bus_, t->session_id(), false);
            t->reload();
        }
    });
    act(imp, tr("&Replace selected from package…"), {}, [this] { replace_from_package(); });
    act(imp, tr("As &DBC…"), {}, [this] {
        auto* t = current_tab();
        if (!t) {
            return;
        }
        if (dbc_checkpoint_act_ && dbc_checkpoint_act_->isChecked()) {
            if (QMessageBox::question(this, tr("DBC import"),
                                      tr("Save this package before importing the DBC?")) ==
                QMessageBox::Yes) {
                save(false, false);
            }
        }
        show_import_dialog(this, bus_, t->session_id(), true);
        t->reload();
    });
    auto* exp = res->addMenu(tr("&Export"));
    act(exp, tr("To &file…"), {}, [this] { export_resource(); });
    act(exp, tr("To &package…"), {}, [this] { export_to_package(); });
    res->addSeparator();
    auto* editors = res->addMenu(tr("E&ditors"));
    act(editors, tr("&String table…"), {}, [this] { open_stbl(); });
    nmap_editor_act_ = editors->addAction(tr("&Name map…"), this, [this] { open_nmap(); });
    act(editors, tr("&XML…"), {}, [this] { open_xml(); });
    act(editors, tr("Export S3SA as &DLL…"), {}, [this] { export_s3sa(); });
    act(editors, tr("Import &DLL into S3SA…"), {}, [this] { import_s3sa(); });
    act(editors, tr("&View S3SA…"), {}, [this] { view_s3sa(); });
    act(editors, tr("&CLIP export as new name…"), {}, [this] { clip_export(); });
    act(editors, tr("Replace &DDS…"), {}, [this] { replace_dds(); });
    act(editors, tr("Replace SNAP PNG…"), {}, [this] { replace_snap(); });
    act(editors, tr("Export &VID…"), {}, [this] { export_vid(); });
    act(res, tr("Open in &hex editor"), {}, [this] { open_external(true); });
    act(res, tr("Open in te&xt editor"), {}, [this] { open_external(false); });
    delete_resource_act_ =
        act(res, tr("&Delete"), QKeySequence::Delete, [this] { delete_resource(); });
    connect(res, &QMenu::aboutToShow, this, [this] {
        sync_flag_actions();
        sync_layout_lock_actions();
    });

    auto* tools = menuBar()->addMenu(tr("&Tools"));
    act(tools, tr("&FNV hash…"), {}, [this] { show_fnv_dialog(this, bus_); });
    act(tools, tr("&Compare packages…"), {}, [this] { compare_packages(); });
    act(tools, tr("Find &references…"), {}, [this] { find_refs(); });
    act(tools, tr("Scan &folder…"), {}, [this] { scan_folder(); });
    act(tools, tr("&Un-merge package…"), {}, [this] { unmerge_package(); });
    act(tools, tr("&Search…"), QKeySequence::Find, [this] {
        if (auto* t = current_tab()) {
            show_search_dialog(this, bus_, t->session_id());
        }
    });
    act(tools, tr("&Validate"), {}, [this] {
        if (auto* t = current_tab()) {
            auto env = run("package.validate", {{"sessionId", t->session_id().toStdString()}});
            show_validate_dialog(this, env);
        }
    });
    compact_act_ = act(tools, tr("&Compact / save"), {}, [this] {
        if (auto* t = current_tab()) {
            run("package.compact", {{"sessionId", t->session_id().toStdString()}});
            t->reload();
        }
    });

    auto* settings = menuBar()->addMenu(tr("&Settings"));
    QSettings st("SXPE", "SXPE");
    preview_dds_act_ = settings->addAction(tr("&DDS preview"));
    preview_dds_act_->setCheckable(true);
    preview_dds_act_->setChecked(st.value("preview/dds", true).toBool());
    connect(preview_dds_act_, &QAction::triggered, this, [this](bool on) {
        QSettings("SXPE", "SXPE").setValue("preview/dds", on);
    });
    preview_text_act_ = settings->addAction(tr("Fallback &text preview"));
    preview_text_act_->setCheckable(true);
    preview_text_act_->setChecked(st.value("preview/text", true).toBool());
    connect(preview_text_act_, &QAction::triggered, this, [this](bool on) {
        QSettings("SXPE", "SXPE").setValue("preview/text", on);
    });
    preview_hex_act_ = settings->addAction(tr("Fallback &hex preview"));
    preview_hex_act_->setCheckable(true);
    preview_hex_act_->setChecked(st.value("preview/hex", true).toBool());
    connect(preview_hex_act_, &QAction::triggered, this, [this](bool on) {
        QSettings("SXPE", "SXPE").setValue("preview/hex", on);
    });
    dbc_checkpoint_act_ = settings->addAction(tr("Ask to save before &DBC import"));
    dbc_checkpoint_act_->setCheckable(true);
    dbc_checkpoint_act_->setChecked(st.value("import/dbcCheckpoint", true).toBool());
    connect(dbc_checkpoint_act_, &QAction::triggered, this, [this](bool on) {
        QSettings("SXPE", "SXPE").setValue("import/dbcCheckpoint", on);
    });
    settings->addSeparator();
    act(settings, tr("&Organise bookmarks…"), {}, [this] { organise_bookmarks(); });
    act(settings, tr("&Built-in handlers…"), {},
        [this] { show_handlers_dialog(this, bus_); });
    act(settings, tr("&External programs…"), {},
        [this] { show_external_programs_dialog(this); });
    settings->addSeparator();
    act(settings, tr("&Save settings"), {}, [this] {
        persist_lists();
        QSettings("SXPE", "SXPE").sync();
    });

    auto* help = menuBar()->addMenu(tr("&Help"));
    act(help, tr("&Contents"), {}, [this] { show_contents_dialog(this); });
    help->addSeparator();
    act(help, tr("&About SXPE"), {}, [this] {
        QMessageBox::about(
            this, tr("About SXPE"),
            tr("SXPE %1\n"
               "SXPE is an unofficial Sims 3 package editor.\n"
               "Not affiliated with Electronic Arts. Not s3pe.\n"
               "License: GPL-3.0-or-later.\n"
               "The Sims 3 is a trademark of Electronic Arts.\n"
               "For updates, see https://github.com/tofb15/sxpe")
                .arg(QCoreApplication::applicationVersion()));
    });
    act(help, tr("&Warranty"), {}, [this] { show_warranty(); });
    act(help, tr("&Licence"), {}, [this] { show_licence(); });

    status_path_ = new QLabel(tr("No package"));
    status_layout_ = new QLabel;
    status_layout_->setVisible(false);
    status_layout_->setStyleSheet(
        QStringLiteral("QLabel { padding: 1px 6px; border-radius: 3px; "
                       "background: #5c4a1f; color: #ffe9a8; font-weight: 600; }"));
    status_counts_ = new QLabel;
    statusBar()->addWidget(status_path_, 1);
    statusBar()->addPermanentWidget(status_layout_);
    statusBar()->addPermanentWidget(status_counts_);

    plugins_.scan();
    mru_ = st.value("mru").toStringList();
    bookmarks_ = st.value("bookmarks").toStringList();
    rebuild_mru();
    rebuild_bookmarks();

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
    connect(tab, &PackageTab::resource_context_menu, this, &MainWindow::show_resource_context);
    connect(tab, &PackageTab::columns_changed, this, &MainWindow::apply_columns_all);
    const int i = tabs_->addTab(tab, title);
    tabs_->setCurrentIndex(i);
    refresh_tab_chrome(tab);
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

void MainWindow::unmerge_package() {
    const auto path = QFileDialog::getOpenFileName(this, tr("Un-merge package"), {},
                                                   tr("Packages (*.package);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    const auto dir = QFileDialog::getExistingDirectory(this, tr("Output folder"));
    if (dir.isEmpty()) {
        return;
    }
    auto env = run("package.unmerge", {{"path", path.toStdString()},
                                       {"outDir", dir.toStdString()},
                                       {"force", true}});
    if (env.value("ok", false)) {
        QMessageBox::information(
            this, tr("Un-merge"),
            tr("Wrote %1 package(s). Only SXPE-manifest merges can be un-merged.")
                .arg(env["data"].value("packagesWritten", 0)));
    }
}


void MainWindow::find_refs() {
    auto* t = current_tab();
    const auto* r = t ? t->current() : nullptr;
    if (!t || !r) {
        QMessageBox::information(this, tr("Find references"),
                                 tr("Select a resource to find references to."));
        return;
    }
    show_find_refs_dialog(this, bus_, t->session_id(), r->type, r->group, r->instance, r->ordinal,
                          [this](std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                                 std::uint32_t ordinal) {
                              if (auto* tab = current_tab()) {
                                  if (!tab->select_resource(type, group, instance, ordinal)) {
                                      QMessageBox::information(
                                          this, tr("Find references"),
                                          tr("Hit listed, but the resource was not found in the "
                                             "index."));
                                  }
                              }
                          });
}

void MainWindow::scan_folder() {
    show_folder_scan_dialog(this, bus_, [this](const QString& path) {
        if (!open_path(path, true)) {
            QMessageBox::warning(this, tr("Scan folder"),
                                 tr("Could not open \"%1\" in SXPE.").arg(path));
        }
    });
}

void MainWindow::compare_packages() {
    show_package_diff_dialog(this, bus_, [this](const QString& path, std::uint32_t type,
                                                std::uint32_t group, std::uint64_t instance,
                                                std::uint32_t ordinal) {
        if (!open_path(path, true)) {
            return;
        }
        if (auto* t = current_tab()) {
            if (!t->select_resource(type, group, instance, ordinal)) {
                QMessageBox::information(this, tr("Compare packages"),
                                         tr("Opened the package, but the resource was not found "
                                            "in the index."));
            }
        }
    });
}

void MainWindow::merge_dropped_packages(const QStringList& paths) {
    auto created = bus_.execute("package.new", nlohmann::json::object());
    if (!created.value("ok", false)) {
        warn_if_err(created);
        return;
    }
    const auto sid = QString::fromStdString(created["data"]["sessionId"].get<std::string>());
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : paths) {
        arr.push_back(p.toStdString());
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto env = bus_.execute("resource.importPackage",
                            {{"sessionId", sid.toStdString()},
                             {"paths", arr},
                             {"force", true},
                             {"writeMergeManifest", true}});
    QApplication::restoreOverrideCursor();
    if (!env.value("ok", false) || env["data"].value("imported", 0) == 0) {
        bus_.execute("package.close", {{"sessionId", sid.toStdString()}});
        warn_if_err(env.value("ok", false)
                        ? nlohmann::json{{"ok", false},
                                         {"error", {{"message", tr("Nothing was imported.").toStdString()}}}}
                        : env);
        return;
    }
    add_tab(sid, tr("Untitled merge"));
    const auto imported = env["data"].value("imported", 0);
    const auto pkgs = env["data"].value("packages", 0);
    const auto failed = env["data"].value("failed", 0);
    QString msg = tr("Merged %1 resource(s) from %2 file(s) into a new untitled package. "
                     "Use File → Save As to write it. The original files were not changed.")
                      .arg(imported)
                      .arg(pkgs);
    if (failed > 0) {
        msg += QLatin1Char('\n') + tr("%1 file(s) could not be imported.").arg(failed);
        QMessageBox::warning(this, tr("SXPE"), msg);
    } else {
        QMessageBox::information(this, tr("SXPE"), msg);
    }
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
    if (env["data"].value("alreadyOpen", false)) {
        QApplication::restoreOverrideCursor();
        const int i = tab_index_for_session(sid);
        if (i >= 0) {
            tabs_->setCurrentIndex(i);
        }
        return true;
    }
    add_tab(sid, QFileInfo(path).fileName() + (writable ? QString() : tr(" [read-only]")));
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

void MainWindow::open_readonly_dialog() {
    const auto path = QFileDialog::getOpenFileName(this, tr("Open package read-only"), {}, filters());
    if (!path.isEmpty()) {
        open_path(path, false);
    }
}

bool MainWindow::save(bool as_copy, bool save_as) { return save_tab(current_tab(), as_copy, save_as); }

bool MainWindow::save_tab(PackageTab* t, bool as_copy, bool save_as) {
    if (!t) {
        return false;
    }
    QString dest;
    if (save_as || as_copy) {
        dest = QFileDialog::getSaveFileName(this, tr("Save package"), package_path(t), filters());
        if (dest.isEmpty()) {
            return false;
        }
        if (path_is_open(dest, t)) {
            QMessageBox::warning(this, tr("SXPE"),
                                 tr("That file is already open in another tab. Close it first, or "
                                    "pick a different name."));
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
        remember_mru(dest);
    }
    t->reload();
    refresh_tab_chrome(t);
    return true;
}

bool MainWindow::close_tab(int index) {
    if (index < 0 || index >= tabs_->count()) {
        return true;
    }
    auto* t = qobject_cast<PackageTab*>(tabs_->widget(index));
    if (t) {
        auto info = bus_.execute("package.info", {{"sessionId", t->session_id().toStdString()}});
        const bool dirty = info.value("ok", false) && info["data"].value("dirty", false);
        const bool writable = info.value("ok", false) && info["data"].value("readWrite", false);
        if (dirty && writable) {
            const auto path = QString::fromStdString(info["data"].value("path", std::string()));
            const auto name = path.isEmpty() ? tr("Untitled") : QFileInfo(path).fileName();
            const auto btn = QMessageBox::question(
                this, tr("Unsaved changes"), tr("Save changes to %1?").arg(name),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
            if (btn == QMessageBox::Cancel) {
                return false;
            }
            if (btn == QMessageBox::Save) {
                tabs_->setCurrentIndex(index);
                if (!save(false, path.isEmpty())) {
                    return false;
                }
            }
        }
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
    return true;
}

void MainWindow::close_other_tabs(int keep) {
    for (int i = tabs_->count() - 1; i >= 0; --i) {
        if (i == keep) {
            continue;
        }
        if (!close_tab(i)) {
            return;
        }
    }
}

void MainWindow::close_tabs_right(int index) {
    for (int i = tabs_->count() - 1; i > index; --i) {
        if (!close_tab(i)) {
            return;
        }
    }
}

void MainWindow::close_tabs_left(int index) {
    for (int i = index - 1; i >= 0; --i) {
        if (!close_tab(i)) {
            return;
        }
    }
}

void MainWindow::show_tab_context(const QPoint& local) {
    auto* bar = tabs_->tabBar();
    if (!bar) {
        return;
    }
    const int index = bar->tabAt(local);
    auto* t = (index >= 0) ? qobject_cast<PackageTab*>(tabs_->widget(index)) : nullptr;
    if (!t) {
        return;
    }
    auto info = bus_.execute("package.info", {{"sessionId", t->session_id().toStdString()}});
    const bool dirty = info.value("ok", false) && info["data"].value("dirty", false);
    const bool writable = info.value("ok", false) && info["data"].value("readWrite", true);
    const auto path = QString::fromStdString(info.value("ok", false)
                                                 ? info["data"].value("path", std::string())
                                                 : std::string());
    QMenu m(this);
    if (dirty && writable) {
        m.addAction(tr("&Save"), this, [this, t, path] {
            const int i = tabs_->indexOf(t);
            if (i >= 0) {
                tabs_->setCurrentIndex(i);
            }
            save_tab(t, false, path.isEmpty());
        });
    }
    m.addAction(tr("&Close"), this, [this, t] {
        const int i = tabs_->indexOf(t);
        if (i >= 0) {
            close_tab(i);
        }
    });
    auto* others = m.addAction(tr("Close &Others"), this, [this, t] {
        const int i = tabs_->indexOf(t);
        if (i >= 0) {
            close_other_tabs(i);
        }
    });
    others->setEnabled(tabs_->count() > 1);
    auto* right = m.addAction(tr("Close tabs to the &right"), this, [this, t] {
        const int i = tabs_->indexOf(t);
        if (i >= 0) {
            close_tabs_right(i);
        }
    });
    right->setEnabled(index < tabs_->count() - 1);
    auto* left = m.addAction(tr("Close tabs to the &left"), this, [this, t] {
        const int i = tabs_->indexOf(t);
        if (i >= 0) {
            close_tabs_left(i);
        }
    });
    left->setEnabled(index > 0);
    if (!path.isEmpty() && !is_bookmarked(path)) {
        m.addSeparator();
        m.addAction(tr("&Bookmark"), this, [this, path] { bookmark_path(path); });
    }
    m.exec(bar->mapToGlobal(local));
}

PackageTab* MainWindow::current_tab() const {
    return qobject_cast<PackageTab*>(tabs_->currentWidget());
}

int MainWindow::tab_index_for_session(const QString& session_id) const {
    for (int i = 0; i < tabs_->count(); ++i) {
        auto* t = qobject_cast<PackageTab*>(tabs_->widget(i));
        if (t && t->session_id() == session_id) {
            return i;
        }
    }
    return -1;
}

bool MainWindow::path_is_open(const QString& path, const PackageTab* except) {
    const QFileInfo want(path);
    const auto key = want.canonicalFilePath().isEmpty() ? want.absoluteFilePath()
                                                        : want.canonicalFilePath();
    for (int i = 0; i < tabs_->count(); ++i) {
        auto* t = qobject_cast<PackageTab*>(tabs_->widget(i));
        if (!t || t == except) {
            continue;
        }
        auto info = bus_.execute("package.info", {{"sessionId", t->session_id().toStdString()}});
        if (!info.value("ok", false)) {
            continue;
        }
        const auto p = QString::fromStdString(info["data"].value("path", std::string()));
        if (p.isEmpty()) {
            continue;
        }
        const QFileInfo have(p);
        const auto hk = have.canonicalFilePath().isEmpty() ? have.absoluteFilePath()
                                                           : have.canonicalFilePath();
        if (QString::compare(key, hk, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

void MainWindow::refresh_tab_chrome(PackageTab* tab) {
    if (!tab) {
        return;
    }
    auto info = bus_.execute("package.info", {{"sessionId", tab->session_id().toStdString()}});
    QString path;
    bool dirty = false;
    bool writable = true;
    if (info.value("ok", false)) {
        path = QString::fromStdString(info["data"].value("path", std::string()));
        dirty = info["data"].value("dirty", false);
        writable = info["data"].value("readWrite", true);
    }
    const int i = tabs_->indexOf(tab);
    if (i >= 0) {
        tabs_->setTabText(i, tab_label(path, writable, dirty));
        tabs_->setTabToolTip(i, path.isEmpty() ? tr("Untitled") : path);
    }
    if (tab == current_tab()) {
        setWindowTitle(tab_label(path, writable, false) + QStringLiteral("[*]") +
                       QStringLiteral(" — SXPE"));
        setWindowModified(dirty);
    }
}

void MainWindow::update_status() {
    auto* t = current_tab();
    if (!t) {
        status_path_->setText(tr("No package"));
        status_layout_->clear();
        status_layout_->setVisible(false);
        status_layout_->setToolTip({});
        status_counts_->clear();
        setWindowTitle(tr("SXPE"));
        setWindowModified(false);
        sync_layout_lock_actions();
        return;
    }
    refresh_tab_chrome(t);
    auto info = bus_.execute("package.info", {{"sessionId", t->session_id().toStdString()}});
    if (!info.value("ok", false)) {
        sync_layout_lock_actions();
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
    const bool locked = d.value("layoutLocked", false);
    if (locked) {
        const auto kind = QString::fromStdString(d.value("pathKind", std::string("neighborhood")));
        status_layout_->setText(tr("Layout lock (%1)").arg(kind));
        status_layout_->setToolTip(
            tr("Neighborhood / world layout lock: only in-place payload replace within "
               "existing hole capacity. Add, delete, reorder, and compact are refused."));
        status_layout_->setVisible(true);
    } else {
        status_layout_->clear();
        status_layout_->setVisible(false);
        status_layout_->setToolTip({});
    }
    status_counts_->setText(tr("%1 shown / %2 in package")
                                .arg(t->visible_count())
                                .arg(d.value("indexCount", 0)));
    sync_layout_lock_actions();
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
    persist_lists();
    rebuild_mru();
}

void MainWindow::persist_lists() {
    QSettings st("SXPE", "SXPE");
    st.setValue("mru", mru_);
    st.setValue("bookmarks", bookmarks_);
}

void MainWindow::apply_columns_all() {
    const auto m = load_column_mask();
    for (int i = 0; i < tabs_->count(); ++i) {
        if (auto* t = qobject_cast<PackageTab*>(tabs_->widget(i))) {
            t->apply_column_mask(m);
        }
    }
}

void MainWindow::rebuild_columns_menu() {
    if (!columns_menu_) {
        return;
    }
    columns_menu_->clear();
    const auto mask = load_column_mask();
    const int vis = visible_column_count(mask);
    for (const auto& c : kColumnInfo) {
        auto* a = columns_menu_->addAction(tr(c.title));
        a->setCheckable(true);
        const bool on = (mask & (1u << static_cast<unsigned>(c.id))) != 0;
        a->setChecked(on);
        a->setEnabled(!(on && vis <= 1));
        connect(a, &QAction::triggered, this, [this, id = static_cast<int>(c.id)](bool checked) {
            auto m = load_column_mask();
            if (!try_set_column_visible(m, id, checked)) {
                rebuild_columns_menu();
                return;
            }
            save_column_mask(m);
            apply_columns_all();
        });
    }
    columns_menu_->addSeparator();
    columns_menu_->addAction(tr("Show all"), this, [this] {
        save_column_mask((1u << static_cast<unsigned>(ResourceModel::Count_)) - 1u);
        apply_columns_all();
    });
    columns_menu_->addAction(tr("Reset to defaults"), this, [this] {
        save_column_mask(default_column_mask());
        apply_columns_all();
    });
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

void MainWindow::rebuild_bookmarks() {
    if (!bookmarks_menu_) {
        return;
    }
    bookmarks_menu_->clear();
    for (const auto& p : bookmarks_) {
        bookmarks_menu_->addAction(p, this, [this, p] { open_path(p, true); });
    }
    if (bookmarks_.isEmpty()) {
        auto* empty = bookmarks_menu_->addAction(tr("(none)"));
        empty->setEnabled(false);
    }
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

namespace {

nlohmann::json rid_json(const DisplayRow& r) {
    return {{"type", r.type}, {"group", r.group}, {"instance", r.instance}, {"ordinal", r.ordinal}};
}

}  // namespace

void MainWindow::copy_resources() {
    auto* t = current_tab();
    if (!t) {
        return;
    }
    const auto sel = t->selected();
    bool first = true;
    for (const auto* r : sel) {
        nlohmann::json args{{"sessionId", t->session_id().toStdString()},
                            {"resourceId", rid_json(*r)}};
        if (!first) {
            args["append"] = true;
        }
        first = false;
        run("resource.copy", args);
    }
}

void MainWindow::paste_resources() {
    auto* t = current_tab();
    if (!t) {
        return;
    }
    run("resource.paste", {{"sessionId", t->session_id().toStdString()}});
    t->reload();
}

void MainWindow::add_resource() {
    auto* t = current_tab();
    if (!t) {
        return;
    }
    std::uint32_t type = 0, group = 0, ordinal = 0;
    std::uint64_t instance = 0;
    if (const auto* r = t->current()) {
        type = r->type;
        group = r->group;
        instance = r->instance;
        ordinal = r->ordinal;
    }
    if (show_add_resource_dialog(this, bus_, t->session_id(), false, type, group, instance, ordinal)) {
        t->reload();
    }
}

void MainWindow::replace_resource() {
    auto* t = current_tab();
    const auto* r = t ? t->current() : nullptr;
    if (!t || !r) {
        return;
    }
    if (sxpe::resources::is_png_image(r->type)) {
        if (show_replace_snap_dialog(this, bus_, t->session_id(), r->type, r->group, r->instance,
                                     r->ordinal, r->file_size ? r->file_size : r->mem_size)) {
            t->reload();
        }
        return;
    }
    if (show_add_resource_dialog(this, bus_, t->session_id(), true, r->type, r->group, r->instance,
                                 r->ordinal)) {
        t->reload();
    }
}

void MainWindow::export_resource() {
    auto* t = current_tab();
    if (!t) {
        return;
    }
    const auto sel = t->selected();
    if (sel.size() > 1) {
        const auto dir = QFileDialog::getExistingDirectory(this, tr("Export folder"));
        if (dir.isEmpty()) {
            return;
        }
        const int n = t->export_selected_to_dir(dir);
        QMessageBox::information(this, tr("Export"), tr("Exported %1 resource(s).").arg(n));
        return;
    }
    if (!t->current()) {
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

void MainWindow::export_to_package() {
    auto* t = current_tab();
    if (!t) {
        return;
    }
    const auto sel = t->selected();
    if (sel.isEmpty()) {
        return;
    }
    const auto path = QFileDialog::getSaveFileName(this, tr("Export to package"), {}, filters());
    if (path.isEmpty()) {
        return;
    }
    int n = 0;
    for (const auto* r : sel) {
        auto env = run("resource.exportToPackage",
                       {{"sessionId", t->session_id().toStdString()},
                        {"resourceId", rid_json(*r)},
                        {"path", path.toStdString()},
                        {"force", true}});
        if (env.value("ok", false)) {
            ++n;
        }
    }
    QMessageBox::information(this, tr("Export"), tr("Copied %1 resource(s) into the package.").arg(n));
}

void MainWindow::import_files() {
    auto* t = current_tab();
    if (!t) {
        return;
    }
    const auto paths = QFileDialog::getOpenFileNames(this, tr("Import files"));
    if (paths.isEmpty()) {
        return;
    }
    int n = 0;
    for (const auto& path : paths) {
        auto env = bus_.execute("resource.importFiles",
                                {{"sessionId", t->session_id().toStdString()},
                                 {"path", path.toStdString()},
                                 {"force", true}});
        if (env.value("ok", false)) {
            ++n;
            continue;
        }
        std::uint32_t type = 0, group = 0, ordinal = 0;
        std::uint64_t instance = 0;
        if (const auto* r = t->current()) {
            type = r->type;
            group = r->group;
            instance = r->instance;
            ordinal = r->ordinal;
        }
        if (show_add_resource_dialog(this, bus_, t->session_id(), false, type, group, instance,
                                     ordinal)) {
            ++n;
        }
    }
    t->reload();
    if (n == 0) {
        QMessageBox::warning(this, tr("SXPE"),
                             tr("Nothing imported. Use community S3_ filenames or Add to set a TGI."));
    }
}

void MainWindow::replace_from_package() {
    auto* t = current_tab();
    if (!t) {
        return;
    }
    const auto sel = t->selected();
    if (sel.isEmpty()) {
        return;
    }
    const auto src_path = QFileDialog::getOpenFileName(this, tr("Replace from package"), {}, filters());
    if (src_path.isEmpty()) {
        return;
    }
    auto open = bus_.execute("package.open",
                             {{"path", src_path.toStdString()}, {"writable", false}});
    if (!open.value("ok", false)) {
        warn_if_err(open);
        return;
    }
    const auto src = QString::fromStdString(open["data"]["sessionId"].get<std::string>());
    int n = 0;
    const auto tmpdir = QDir::temp();
    for (const auto* r : sel) {
        const auto tmp = tmpdir.filePath(QString("sxpe-repl-%1.bin").arg(r->index));
        auto exp = bus_.execute("resource.export", {{"sessionId", src.toStdString()},
                                                    {"resourceId", rid_json(*r)},
                                                    {"path", tmp.toStdString()},
                                                    {"force", true}});
        if (!exp.value("ok", false)) {
            continue;
        }
        auto imp = bus_.execute("resource.importFiles",
                                {{"sessionId", t->session_id().toStdString()},
                                 {"path", tmp.toStdString()},
                                 {"resourceId", rid_json(*r)},
                                 {"force", true}});
        if (imp.value("ok", false)) {
            ++n;
        }
        QFile::remove(tmp);
    }
    bus_.execute("package.close", {{"sessionId", src.toStdString()}});
    t->reload();
    QMessageBox::information(this, tr("Replace"),
                             tr("Replaced %1 of %2 selected resource(s).").arg(n).arg(sel.size()));
}

void MainWindow::copy_resource_key() {
    auto* t = current_tab();
    if (!t) {
        return;
    }
    QStringList lines;
    for (const auto* r : t->selected()) {
        lines << QString("%1-%2-%3").arg(r->type_h, r->group_h, r->inst_h);
    }
    if (lines.isEmpty()) {
        return;
    }
    if (auto* cb = QGuiApplication::clipboard()) {
        cb->setText(lines.join('\n'));
    }
}

void MainWindow::set_compressed(bool on) {
    auto* t = current_tab();
    if (!t) {
        return;
    }
    for (const auto* r : t->selected()) {
        run("resource.setFlags", {{"sessionId", t->session_id().toStdString()},
                                  {"resourceId", rid_json(*r)},
                                  {"compressed", on}});
    }
    t->reload();
}

void MainWindow::set_deleted(bool on) {
    auto* t = current_tab();
    if (!t) {
        return;
    }
    for (const auto* r : t->selected()) {
        run("resource.setFlags", {{"sessionId", t->session_id().toStdString()},
                                  {"resourceId", rid_json(*r)},
                                  {"deleted", on}});
    }
    t->reload();
}

void MainWindow::delete_resource() {
    auto* t = current_tab();
    if (!t) {
        return;
    }
    const auto sel = t->selected();
    if (sel.isEmpty()) {
        return;
    }
    if (QMessageBox::question(this, tr("Delete"),
                              tr("Remove %1 selected resource(s) from the package?").arg(sel.size())) !=
        QMessageBox::Yes) {
        return;
    }
    for (const auto* r : sel) {
        run("resource.delete", {{"sessionId", t->session_id().toStdString()},
                                {"resourceId", rid_json(*r)}});
    }
    t->reload();
}

void MainWindow::duplicate_resource() {
    auto* t = current_tab();
    if (!t) {
        return;
    }
    for (const auto* r : t->selected()) {
        run("resource.duplicate", {{"sessionId", t->session_id().toStdString()},
                                   {"resourceId", rid_json(*r)}});
    }
    t->reload();
}

void MainWindow::details_resource() {
    auto* t = current_tab();
    const auto* r = t ? t->current() : nullptr;
    if (!t || !r) {
        return;
    }
    show_details_dialog(this, bus_, t->session_id(), r->type, r->group, r->instance, r->ordinal,
                        r->name, r->compressed, r->deleted);
    t->reload();
}

void MainWindow::open_stbl() {
    auto* t = current_tab();
    const auto* r = t ? t->current() : nullptr;
    if (!t || !r) {
        return;
    }
    if (show_stbl_editor(this, bus_, t->session_id(), r->type, r->group, r->instance, r->ordinal)) {
        t->reload();
    }
}

void MainWindow::open_nmap() {
    auto* t = current_tab();
    if (!t) {
        return;
    }
    const auto* r = t->current();
    nlohmann::json rid;
    const nlohmann::json* rid_ptr = nullptr;
    if (r && r->type == sxpe::resources::kNmap) {
        rid = rid_json(*r);
        rid_ptr = &rid;
    } else {
        // Prefer package NMAP when another row is selected (or none).
        auto listed = bus_.execute(
            "resource.list",
            {{"sessionId", t->session_id().toStdString()},
             {"filter", {{"tag", "NMAP"}}},
             {"limit", 1}});
        if (!listed.value("ok", false) || !listed["data"].contains("items") ||
            listed["data"]["items"].empty()) {
            QMessageBox::information(this, tr("SXPE"),
                                     tr("This package has no name map (NMAP)."));
            return;
        }
        const auto& it = listed["data"]["items"][0];
        rid = {{"type", it.value("type", 0u)},
               {"group", it.value("group", 0u)},
               {"instance", it.value("instance", 0ull)},
               {"ordinal", it.value("ordinal", 0u)}};
        rid_ptr = &rid;
    }
    if (show_nmap_editor(this, bus_, t->session_id(), rid_ptr)) {
        t->reload();
    }
}

void MainWindow::open_xml() {
    auto* t = current_tab();
    const auto* r = t ? t->current() : nullptr;
    if (!t || !r) {
        return;
    }
    if (show_xml_editor(this, bus_, t->session_id(), r->type, r->group, r->instance, r->ordinal)) {
        t->reload();
    }
}

void MainWindow::import_s3sa() {
    auto* t = current_tab();
    if (!t) {
        return;
    }
    const auto path = QFileDialog::getOpenFileName(this, tr("Import DLL into S3SA"), {},
                                                   tr("DLL (*.dll);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    nlohmann::json args{{"sessionId", t->session_id().toStdString()},
                        {"path", path.toStdString()},
                        {"force", true}};
    const auto* r = t->current();
    if (r && r->type == sxpe::resources::kS3sa) {
        args["resourceId"] = rid_json(*r);
    }
    run("s3sa.importDll", std::move(args));
    t->reload();
}

void MainWindow::export_s3sa() {
    auto* t = current_tab();
    const auto* r = t ? t->current() : nullptr;
    if (!t || !r) {
        return;
    }
    auto info = run("s3sa.info",
                    {{"sessionId", t->session_id().toStdString()}, {"resourceId", rid_json(*r)}});
    QString hint = QStringLiteral("assembly.dll");
    if (info.value("ok", false)) {
        const auto h = QString::fromStdString(info["data"].value("moduleHint", std::string()));
        if (!h.isEmpty()) {
            hint = h;
            if (!hint.contains('.')) {
                hint += QStringLiteral(".dll");
            }
        }
    }
    const auto path = QFileDialog::getSaveFileName(this, tr("Export DLL"), hint,
                                                   tr("DLL (*.dll);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    run("s3sa.exportDll", {{"sessionId", t->session_id().toStdString()},
                           {"resourceId", rid_json(*r)},
                           {"path", path.toStdString()},
                           {"force", true}});
}


void MainWindow::view_s3sa() {
    auto* t = current_tab();
    const auto* r = t ? t->current() : nullptr;
    if (!t || !r || r->type != sxpe::resources::kS3sa) {
        return;
    }
    QSettings s("SXPE", "SXPE");
    const auto cmd = s.value("ext/s3sa").toString();
    if (cmd.trimmed().isEmpty()) {
        QMessageBox::information(
            this, tr("SXPE"),
            tr("Set an S3SA viewer under Settings → External programs (ILSpy, dnSpy, …). "
               "Use {path}. SXPE never LoadLibrarys the assembly."));
        return;
    }
    auto env = run("s3sa.view", {{"sessionId", t->session_id().toStdString()},
                                 {"resourceId", rid_json(*r)},
                                 {"force", true}});
    if (!env.value("ok", false)) {
        return;
    }
    const auto path = QString::fromStdString(env["data"].value("path", std::string()));
    if (path.isEmpty()) {
        QMessageBox::warning(this, tr("SXPE"), tr("s3sa.view returned no path."));
        return;
    }
    QString err;
    if (!plugins_.run_user_command_cleanup(cmd, path, &err)) {
        QMessageBox::warning(this, tr("SXPE"), err);
        QFile::remove(path);
    }
}

void MainWindow::clip_export() {
    auto* t = current_tab();
    const auto* r = t ? t->current() : nullptr;
    if (!t || !r) {
        return;
    }
    if (show_clip_export_dialog(this, bus_, t->session_id(), r->type, r->group, r->instance,
                                r->ordinal)) {
        t->reload();
    }
}

void MainWindow::replace_dds() {
    auto* t = current_tab();
    const auto* r = t ? t->current() : nullptr;
    if (!t || !r) {
        return;
    }
    if (show_add_resource_dialog(this, bus_, t->session_id(), true, r->type, r->group, r->instance,
                                 r->ordinal, tr("DDS (*.dds);;All files (*.*)"))) {
        t->reload();
    }
}

void MainWindow::replace_snap() {
    auto* t = current_tab();
    const auto* r = t ? t->current() : nullptr;
    if (!t || !r) {
        return;
    }
    if (show_replace_snap_dialog(this, bus_, t->session_id(), r->type, r->group, r->instance,
                                 r->ordinal, r->file_size ? r->file_size : r->mem_size)) {
        t->reload();
    }
}

void MainWindow::export_vid() {
    auto* t = current_tab();
    const auto* r = t ? t->current() : nullptr;
    if (!t || !r) {
        return;
    }
    const auto path = QFileDialog::getSaveFileName(this, tr("Export VID"), {},
                                                   tr("Video (*.vp6 *.vid);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    run("vid.export", {{"sessionId", t->session_id().toStdString()},
                       {"resourceId", rid_json(*r)},
                       {"path", path.toStdString()},
                       {"force", true}});
}

void MainWindow::copy_preview() {
    if (auto* t = current_tab()) {
        t->copy_preview();
    }
}

void MainWindow::save_preview() {
    auto* t = current_tab();
    if (!t) {
        return;
    }
    const auto path = QFileDialog::getSaveFileName(
        this, tr("Save preview"), {}, tr("PNG (*.png);;Text (*.txt);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    if (!t->save_preview(path)) {
        QMessageBox::warning(this, tr("SXPE"), tr("Could not save preview."));
    }
}

void MainWindow::bookmark_current() {
    const auto path = current_package_path();
    if (path.isEmpty()) {
        QMessageBox::information(this, tr("Bookmarks"),
                                 tr("Save the package first, then bookmark it."));
        return;
    }
    bookmark_path(path);
}

void MainWindow::bookmark_path(const QString& path) {
    if (path.isEmpty() || is_bookmarked(path)) {
        return;
    }
    bookmarks_.removeAll(path);
    bookmarks_.prepend(path);
    while (bookmarks_.size() > 16) {
        bookmarks_.removeLast();
    }
    persist_lists();
    rebuild_bookmarks();
}

bool MainWindow::is_bookmarked(const QString& path) const {
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo want(path);
    const auto key = want.canonicalFilePath().isEmpty() ? want.absoluteFilePath()
                                                        : want.canonicalFilePath();
    for (const auto& p : bookmarks_) {
        const QFileInfo have(p);
        const auto hk = have.canonicalFilePath().isEmpty() ? have.absoluteFilePath()
                                                           : have.canonicalFilePath();
        if (QString::compare(key, hk, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

void MainWindow::organise_bookmarks() {
    show_bookmarks_dialog(this, &bookmarks_);
    persist_lists();
    rebuild_bookmarks();
}

QString MainWindow::package_path(PackageTab* t) {
    if (!t) {
        return {};
    }
    auto info = bus_.execute("package.info", {{"sessionId", t->session_id().toStdString()}});
    if (!info.value("ok", false)) {
        return {};
    }
    return QString::fromStdString(info["data"].value("path", std::string()));
}

QString MainWindow::current_package_path() { return package_path(current_tab()); }

void MainWindow::sync_flag_actions() {
    auto* t = current_tab();
    const auto* r = t ? t->current() : nullptr;
    if (compressed_act_) {
        compressed_act_->setEnabled(r != nullptr);
        compressed_act_->setChecked(r && r->compressed);
    }
    if (deleted_act_) {
        deleted_act_->setEnabled(r != nullptr && !current_layout_locked());
        deleted_act_->setChecked(r && r->deleted);
    }
    if (nmap_editor_act_) {
        bool enable = false;
        if (t) {
            if (r && r->type == sxpe::resources::kNmap) {
                enable = true;
            } else {
                auto listed = bus_.execute(
                    "resource.list",
                    {{"sessionId", t->session_id().toStdString()},
                     {"filter", {{"tag", "NMAP"}}},
                     {"limit", 1}});
                enable = listed.value("ok", false) && listed["data"].contains("items") &&
                         !listed["data"]["items"].empty();
            }
        }
        nmap_editor_act_->setEnabled(enable);
    }
}

bool MainWindow::current_layout_locked() {
    auto* t = current_tab();
    if (!t) {
        return false;
    }
    auto info = bus_.execute("package.info", {{"sessionId", t->session_id().toStdString()}});
    return info.value("ok", false) && info["data"].value("layoutLocked", false);
}

void MainWindow::sync_layout_lock_actions() {
    const bool locked = current_layout_locked();
    const bool allow_mutate_layout = !locked;
    if (add_resource_act_) {
        add_resource_act_->setEnabled(allow_mutate_layout);
        add_resource_act_->setToolTip(
            locked ? tr("Neighborhood / world layout lock: adding resources is not supported")
                   : QString());
    }
    if (paste_resource_act_) {
        paste_resource_act_->setEnabled(allow_mutate_layout);
    }
    if (duplicate_resource_act_) {
        duplicate_resource_act_->setEnabled(allow_mutate_layout);
    }
    if (delete_resource_act_) {
        delete_resource_act_->setEnabled(allow_mutate_layout);
        delete_resource_act_->setToolTip(
            locked ? tr("Neighborhood / world layout lock: deleting resources is not supported")
                   : QString());
    }
    if (deleted_act_) {
        const auto* r = current_tab() ? current_tab()->current() : nullptr;
        deleted_act_->setEnabled(r != nullptr && allow_mutate_layout);
    }
    if (import_menu_) {
        import_menu_->setEnabled(allow_mutate_layout);
    }
    if (compact_act_) {
        compact_act_->setEnabled(allow_mutate_layout);
        compact_act_->setToolTip(
            locked ? tr("Neighborhood / world layout lock: compact is not supported") : QString());
    }
}

void MainWindow::show_resource_context(const QPoint& global) {
    auto* t = current_tab();
    const auto* r = t ? t->current() : nullptr;
    const bool locked = current_layout_locked();
    QMenu m(this);
    m.addAction(tr("&Add…"), this, [this] { add_resource(); })->setEnabled(!locked);
    m.addAction(tr("&Copy"), this, [this] { copy_resources(); });
    m.addAction(tr("&Paste"), this, [this] { paste_resources(); })->setEnabled(!locked);
    m.addAction(tr("&Duplicate"), this, [this] { duplicate_resource(); })->setEnabled(!locked);
    m.addAction(tr("&Replace…"), this, [this] { replace_resource(); });
    m.addSeparator();
    auto* cmp = m.addAction(tr("&Compressed"));
    cmp->setCheckable(true);
    cmp->setEnabled(r != nullptr);
    cmp->setChecked(r && r->compressed);
    connect(cmp, &QAction::triggered, this, [this](bool on) { set_compressed(on); });
    auto* del = m.addAction(tr("De&leted flag"));
    del->setCheckable(true);
    del->setEnabled(r != nullptr && !locked);
    del->setChecked(r && r->deleted);
    connect(del, &QAction::triggered, this, [this](bool on) { set_deleted(on); });
    m.addAction(tr("D&etails…"), this, [this] { details_resource(); });
    m.addSeparator();
    m.addAction(tr("Select &All"), this, [this] {
        if (auto* tab = current_tab()) {
            tab->select_all();
        }
    });
    m.addAction(tr("Copy resource &key"), this, [this] { copy_resource_key(); });
    auto* imp = m.addMenu(tr("&Import"));
    imp->setEnabled(!locked);
    imp->addAction(tr("From &file…"), this, [this] { import_files(); });
    imp->addAction(tr("From &package(s)…"), this, [this] {
        if (auto* tab = current_tab()) {
            show_import_dialog(this, bus_, tab->session_id(), false);
            tab->reload();
        }
    });
    imp->addAction(tr("&Replace selected from package…"), this, [this] { replace_from_package(); });
    imp->addAction(tr("As &DBC…"), this, [this] {
        if (auto* tab = current_tab()) {
            show_import_dialog(this, bus_, tab->session_id(), true);
            tab->reload();
        }
    });
    auto* exp = m.addMenu(tr("&Export"));
    exp->addAction(tr("To &file…"), this, [this] { export_resource(); });
    exp->addAction(tr("To &package…"), this, [this] { export_to_package(); });
    m.addSeparator();
    auto* editors = m.addMenu(tr("E&ditors"));
    auto* stbl = editors->addAction(tr("&String table…"), this, [this] { open_stbl(); });
    auto* nmap = editors->addAction(tr("&Name map…"), this, [this] { open_nmap(); });
    auto* xml = editors->addAction(tr("&XML…"), this, [this] { open_xml(); });
    auto* s3sa = editors->addAction(tr("Export S3SA as &DLL…"), this, [this] { export_s3sa(); });
    auto* s3sa_in = editors->addAction(tr("Import &DLL into S3SA…"), this, [this] { import_s3sa(); });
    auto* s3sa_view = editors->addAction(tr("&View S3SA…"), this, [this] { view_s3sa(); });
    auto* clip = editors->addAction(tr("&CLIP export as new name…"), this, [this] { clip_export(); });
    auto* dds = editors->addAction(tr("Replace &DDS…"), this, [this] { replace_dds(); });
    auto* snap = editors->addAction(tr("Replace SNAP PNG…"), this, [this] { replace_snap(); });
    editors->addAction(tr("Export &VID…"), this, [this] { export_vid(); });
    bool has_nmap = false;
    if (t) {
        auto listed = bus_.execute(
            "resource.list",
            {{"sessionId", t->session_id().toStdString()},
             {"filter", {{"tag", "NMAP"}}},
             {"limit", 1}});
        has_nmap = listed.value("ok", false) && listed["data"].contains("items") &&
                   !listed["data"]["items"].empty();
    }
    nmap->setEnabled((r && r->type == sxpe::resources::kNmap) || has_nmap);
    if (r) {
        stbl->setEnabled(r->type == sxpe::resources::kStbl);
        xml->setEnabled(r->type == sxpe::resources::kXml || r->type == sxpe::resources::kItun);
        s3sa->setEnabled(r->type == sxpe::resources::kS3sa);
        s3sa_in->setEnabled(true);
        s3sa_view->setEnabled(r->type == sxpe::resources::kS3sa);
        clip->setEnabled(r->type == sxpe::resources::kClip);
        dds->setEnabled(r->type == sxpe::resources::kImg || r->type == sxpe::resources::kImgAlt);
        snap->setEnabled(sxpe::resources::is_png_image(r->type));
    } else {
        stbl->setEnabled(false);
        xml->setEnabled(false);
        s3sa->setEnabled(false);
        s3sa_view->setEnabled(false);
        clip->setEnabled(false);
        dds->setEnabled(false);
        snap->setEnabled(false);
    }
    m.addAction(tr("Find &references…"), this, [this] { find_refs(); })->setEnabled(r != nullptr);
    m.addAction(tr("Open in &hex editor"), this, [this] { open_external(true); });
    m.addAction(tr("Open in te&xt editor"), this, [this] { open_external(false); });
    m.addAction(tr("&Delete"), this, [this] { delete_resource(); })->setEnabled(!locked);
    m.exec(global);
}

void MainWindow::show_warranty() {
    QMessageBox::information(
        this, tr("Warranty"),
        tr("There is no warranty for this program, to the extent permitted by applicable law. "
           "Except when otherwise stated in writing the copyright holders and/or other parties "
           "provide the program “as is” without warranty of any kind, either expressed or implied, "
           "including, but not limited to, the implied warranties of merchantability and fitness "
           "for a particular purpose. See GNU GPL version 3 for the full text."));
}

void MainWindow::show_licence() {
    QString text;
    const QString dir = QCoreApplication::applicationDirPath();
    for (const auto& p : {dir + "/LICENSE", dir + "/../LICENSE", dir + "/../../LICENSE"}) {
        QFile f(p);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            text = QString::fromUtf8(f.readAll());
            break;
        }
    }
    if (text.isEmpty()) {
        text = tr("SXPE is licensed under GPL-3.0-or-later.\n"
                  "The full licence is the LICENSE file in the SXPE source tree.");
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Licence"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* view = new QPlainTextEdit;
    view->setReadOnly(true);
    view->setPlainText(text);
    lay->addWidget(view);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(box);
    dlg.resize(640, 480);
    dlg.exec();
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
    for (int i = tabs_->count() - 1; i >= 0; --i) {
        if (!close_tab(i)) {
            e->ignore();
            return;
        }
    }
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* e) {
    QStringList files;
    for (const auto& u : e->mimeData()->urls()) {
        const auto p = u.toLocalFile();
        if (!p.isEmpty() && QFileInfo::exists(p)) {
            files.push_back(p);
        }
    }
    if (files.isEmpty()) {
        return;
    }
    if (files.size() == 1) {
        open_path(files.front(), true);
        return;
    }
    QMessageBox box(this);
    box.setWindowTitle(tr("Drop %1 files").arg(files.size()));
    box.setText(tr("Open each file in its own tab, or merge every resource into a new untitled package?"));
    box.setInformativeText(
        tr("A merge never writes the dropped files. If two packages share a resource key, "
           "the later file wins. Save the result with File → Save As."));
    auto* as_tabs = box.addButton(tr("Open as tabs"), QMessageBox::AcceptRole);
    auto* as_merge = box.addButton(tr("Merge into new package"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(qobject_cast<QPushButton*>(as_tabs));
    box.exec();
    if (box.clickedButton() == as_tabs) {
        for (const auto& p : files) {
            open_path(p, true);
        }
    } else if (box.clickedButton() == as_merge) {
        merge_dropped_packages(files);
    }
}

}  // namespace sxpe::gui
