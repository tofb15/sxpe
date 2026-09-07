#include "main_window.hpp"

#include "sxpe/version.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QPalette>
#include <QStyleHints>

namespace {

void apply_theme(QApplication& app) {
    app.setStyle("Fusion");
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (app.styleHints()->colorScheme() != Qt::ColorScheme::Dark) {
        return;
    }
#endif
    QPalette p;
    p.setColor(QPalette::Window, QColor(45, 45, 48));
    p.setColor(QPalette::WindowText, Qt::white);
    p.setColor(QPalette::Base, QColor(30, 30, 30));
    p.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
    p.setColor(QPalette::Text, Qt::white);
    p.setColor(QPalette::Button, QColor(45, 45, 48));
    p.setColor(QPalette::ButtonText, Qt::white);
    p.setColor(QPalette::Highlight, QColor(0, 120, 212));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, QColor(45, 45, 48));
    p.setColor(QPalette::ToolTipText, Qt::white);
    app.setPalette(p);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("SXPE");
    app.setOrganizationName("SXPE");
    app.setApplicationVersion(QStringLiteral(SXPE_VERSION));
    apply_theme(app);

    sxpe::gui::MainWindow w;
    const auto args = QCoreApplication::arguments();
    const bool smoke = args.contains("--smoke");
    if (smoke) {
        w.set_smoke_mode(true);
    }
    QString open;
    for (int i = 1; i < args.size(); ++i) {
        if (args[i].startsWith('-')) {
            continue;
        }
        open = args[i];
        break;
    }
    bool opened = false;
    if (!open.isEmpty()) {
        opened = w.open_path(open, !smoke);
    }
    if (smoke) {
        // Open synthetic package and list/filter resources (headless / offscreen CI).
        if (open.isEmpty() || !opened || !w.smoke_filter({})) {
            return 1;
        }
        return 0;
    }
    w.show();
    return app.exec();
}
