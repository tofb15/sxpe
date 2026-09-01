#pragma once

#include "sxpe/commands/bus.hpp"
#include "plugin_host.hpp"

#include <QDialog>
#include <QString>

class QWidget;

namespace sxpe::gui {

void show_fnv_dialog(QWidget* parent, sxpe::commands::Bus& bus);
void show_details_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                         const sxpe::commands::UiRow& row);
void show_search_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session);
void show_import_dialog(QWidget* parent, sxpe::commands::Bus& bus, const QString& session,
                        bool dbc);
void show_handlers_dialog(QWidget* parent, sxpe::commands::Bus& bus, PluginHost& host);
void show_external_programs_dialog(QWidget* parent);

}  // namespace sxpe::gui
