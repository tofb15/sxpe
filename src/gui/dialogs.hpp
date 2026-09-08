#pragma once

#include "sxpe/commands/bus.hpp"
#include <QDialog>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>

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
void show_handlers_dialog(QWidget* parent, sxpe::commands::Bus& bus);
void show_external_programs_dialog(QWidget* parent);
bool show_add_resource_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                              bool replace, std::uint32_t type, std::uint32_t group,
                              std::uint64_t instance, std::uint32_t ordinal,
                              const QString& file_filter = {});
bool show_stbl_editor(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                      std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                      std::uint32_t ordinal);
/// resourceId may be nullopt to edit the package's (first) NMAP via nmap.get / nmap.replace.
bool show_nmap_editor(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                      const nlohmann::json* resource_id);
bool show_xml_editor(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                     std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                     std::uint32_t ordinal);
bool show_clip_export_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                             std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                             std::uint32_t ordinal);
bool show_replace_snap_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                              std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                              std::uint32_t ordinal, std::uint32_t max_bytes);
void show_bookmarks_dialog(QWidget* parent, QStringList* bookmarks);
void show_contents_dialog(QWidget* parent);
/// Help → Common tasks: plain-language recipes + link to docs/workflows.md.
void show_common_tasks_dialog(QWidget* parent);
/// Tools → Merge packages…: folder/files → preview count/size → merge (bus) → optional validate.
/// Calls on_merge(paths, validate_after) when the user confirms; does not run the bus itself.
void show_merge_assistant_dialog(
    QWidget* parent,
    const std::function<void(const QStringList& paths, bool validate_after)>& on_merge);
/// One-shot first-run tip (skip when smoke_mode). Sets onboarding/seenFirstRunTip.
void show_first_run_tip_if_needed(QWidget* parent, bool smoke_mode,
                                  const std::function<void()>& open_merge_assistant = {});
/// Query GitHub Releases API; never downloads. Graceful offline / no-release.
void show_check_for_update_dialog(QWidget* parent);
void show_validate_dialog(QWidget* parent, const nlohmann::json& envelope);
/// Compare two packages via package.diff. open_hit opens a path and selects a resource.
void show_package_diff_dialog(
    QWidget* parent, sxpe::commands::Bus& bus,
    const std::function<void(const QString& path, std::uint32_t type, std::uint32_t group,
                             std::uint64_t instance, std::uint32_t ordinal)>& open_hit);
/// Find references to a TGI via resource.findRefs. select_hit jumps to a source resource.
void show_find_refs_dialog(
    QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
    std::uint32_t type, std::uint32_t group, std::uint64_t instance, std::uint32_t ordinal,
    const std::function<void(std::uint32_t type, std::uint32_t group, std::uint64_t instance,
                             std::uint32_t ordinal)>& select_hit);
/// Read-only folder.scan hygiene. open_path opens a package path in SXPE.
void show_folder_scan_dialog(
    QWidget* parent, sxpe::commands::Bus& bus,
    const std::function<void(const QString& path)>& open_path);
/// Read-only Sims3Pack inspect / extract. open_package opens an extracted .package.
void show_sims3pack_dialog(
    QWidget* parent, sxpe::commands::Bus& bus,
    const std::function<void(const QString& path)>& open_package);

}  // namespace sxpe::gui
