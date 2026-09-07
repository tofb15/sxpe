#pragma once

#include <QString>
#include <QStringList>
#include <vector>

namespace sxpe::gui {

struct PluginInfo {
    QString id;
    QString path;
    QString kind;  // "dll" or "exe"
    QString label;
};

/// GUI-only. CLI/MCP must not load these DLLs.
class PluginHost {
public:
    void scan();
    [[nodiscard]] const std::vector<PluginInfo>& plugins() const { return plugins_; }
    /// Substitute `{path}` in a user command and start it. `path` is a filesystem file.
    bool run_user_command(const QString& command, const QString& file_path, QString* error) const;
    /// Like run_user_command, but delete `file_path` when the process exits (best-effort).
    bool run_user_command_cleanup(const QString& command, const QString& file_path,
                                  QString* error) const;

private:
    std::vector<PluginInfo> plugins_;
};

QStringList plugin_search_dirs();

}  // namespace sxpe::gui
