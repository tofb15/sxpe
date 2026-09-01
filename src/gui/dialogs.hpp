#pragma once

#include "sxpe/commands/bus.hpp"
#include "plugin_host.hpp"

#include <QDialog>
#include <QString>

class QWidget;

namespace sxpe::gui {

void show_fnv_dialog(QWidget* parent, sxpe::commands::Bus& bus);
void show_details_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                         std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                         std::uint32_t ordinal, const QString& name, bool compressed,
                         bool deleted);
void show_search_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session);
void show_import_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                        bool dbc);
void show_handlers_dialog(QWidget* parent, sxpe::commands::Bus& bus, PluginHost& host);
void show_external_programs_dialog(QWidget* parent);
bool show_add_resource_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                              bool replace, std::uint32_t type, std::uint32_t group,
                              std::uint64_t instance, std::uint32_t ordinal,
                              const QString& file_filter = {});
bool show_stbl_editor(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                      std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                      std::uint32_t ordinal);
bool show_clip_export_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                             std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                             std::uint32_t ordinal);
bool show_replace_snap_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                              std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                              std::uint32_t ordinal);
void show_bookmarks_dialog(QWidget* parent, QStringList* bookmarks);

}  // namespace sxpe::gui
