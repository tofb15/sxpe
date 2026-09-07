#include "plugin_host.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QStandardPaths>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace sxpe::gui {
namespace {

bool under_root(const QString& file, const QString& root) {
    const auto n = QDir::cleanPath(file);
    const auto r = QDir::cleanPath(root);
    return n.startsWith(r, Qt::CaseInsensitive);
}

}  // namespace

QStringList plugin_search_dirs() {
    QStringList dirs;
    const auto exe = QCoreApplication::applicationDirPath() + "/plugins";
    dirs << exe;
    const auto data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!data.isEmpty()) {
        dirs << data + "/plugins";
    }
    return dirs;
}

void PluginHost::scan() {
    plugins_.clear();
    const auto roots = plugin_search_dirs();
    for (const auto& root : roots) {
        QDir dir(root);
        if (!dir.exists()) {
            continue;
        }
        const auto dlls = dir.entryInfoList({"*.dll", "*.exe"}, QDir::Files);
        for (const auto& fi : dlls) {
            if (!under_root(fi.absoluteFilePath(), root)) {
                continue;
            }
            PluginInfo p;
            p.path = fi.absoluteFilePath();
            p.id = fi.completeBaseName();
            p.label = fi.completeBaseName();
            p.kind = fi.suffix().toLower() == "dll" ? "dll" : "exe";
#ifdef _WIN32
            if (p.kind == "dll") {
                const auto w = fi.absoluteFilePath().toStdWString();
                HMODULE h = LoadLibraryW(w.c_str());
                if (h) {
                    using NameFn = const char* (*)();
                    if (auto fn = reinterpret_cast<NameFn>(GetProcAddress(h, "sxpe_plugin_name"))) {
                        if (const char* n = fn()) {
                            p.label = QString::fromUtf8(n);
                        }
                    }
                    FreeLibrary(h);
                }
            }
#endif
            plugins_.push_back(std::move(p));
        }
    }
}

bool PluginHost::run_user_command(const QString& command, const QString& file_path,
                                  QString* error) const {
    if (file_path.contains("..")) {
        if (error) {
            *error = "path contains ..";
        }
        return false;
    }
    QString cmd = command;
    cmd.replace("{path}", QDir::toNativeSeparators(file_path));
    if (cmd.trimmed().isEmpty()) {
        if (error) {
            *error = "empty command";
        }
        return false;
    }
    if (!QProcess::startDetached(cmd)) {
        if (error) {
            *error = "failed to start";
        }
        return false;
    }
    return true;
}


bool PluginHost::run_user_command_cleanup(const QString& command, const QString& file_path,
                                          QString* error) const {
    if (file_path.contains("..")) {
        if (error) {
            *error = "path contains ..";
        }
        return false;
    }
    QString cmd = command;
    cmd.replace("{path}", QDir::toNativeSeparators(file_path));
    if (cmd.trimmed().isEmpty()) {
        if (error) {
            *error = "empty command";
        }
        return false;
    }
    auto* proc = new QProcess;
    const QString path_copy = file_path;
    QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), proc,
                     [proc, path_copy](int, QProcess::ExitStatus) {
                         QFile::remove(path_copy);
                         proc->deleteLater();
                     });
    QObject::connect(proc, &QProcess::errorOccurred, proc,
                     [proc, path_copy](QProcess::ProcessError) {
                         if (proc->state() == QProcess::NotRunning) {
                             QFile::remove(path_copy);
                             proc->deleteLater();
                         }
                     });
#ifdef Q_OS_WIN
    proc->setProgram(QStringLiteral("cmd.exe"));
    proc->setArguments({QStringLiteral("/c"), cmd});
#else
    proc->setProgram(QStringLiteral("/bin/sh"));
    proc->setArguments({QStringLiteral("-c"), cmd});
#endif
    proc->start();
    if (!proc->waitForStarted(8000)) {
        if (error) {
            *error = proc->errorString().isEmpty() ? QStringLiteral("failed to start")
                                                   : proc->errorString();
        }
        QFile::remove(path_copy);
        proc->deleteLater();
        return false;
    }
    return true;
}

}  // namespace sxpe::gui
