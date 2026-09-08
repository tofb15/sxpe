#include "plugin_host.hpp"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QProcess>

namespace sxpe::gui {

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
