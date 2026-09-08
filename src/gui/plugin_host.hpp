#pragma once

#include <QString>

namespace sxpe::gui {

/// GUI-only helpers for Settings → External programs (hex / text / S3SA viewer).
/// Third-party GUI plugins / DLL Handlers are permanently unsupported (#60): no
/// plugin directory scan, no LoadLibrary of random DLLs, no plugin SDK.
class PluginHost {
public:
    /// Substitute `{path}` in a user command and start it. `path` is a filesystem file.
    bool run_user_command(const QString& command, const QString& file_path, QString* error) const;
    /// Like run_user_command, but delete `file_path` when the process exits (best-effort).
    bool run_user_command_cleanup(const QString& command, const QString& file_path,
                                  QString* error) const;
};

}  // namespace sxpe::gui
