#include "mainwindow.h"
#include "version.h"

#include <QApplication>
#include <QIcon>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Orchestrate"));
    QApplication::setApplicationDisplayName(QStringLiteral("Orchestrate"));
    QApplication::setApplicationVersion(QStringLiteral(ORCHESTRATE_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Orchestrate"));

    // Window, taskbar and tray all share the product icon; exe icon comes from app.rc.
    QIcon appIcon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
        appIcon.addFile(QStringLiteral(":/app-icon/%1.png").arg(size), QSize(size, size));
    }
    QApplication::setWindowIcon(appIcon);

    // Keep the first skeleton predictable across Windows themes.
    if (QStyleFactory::keys().contains(QStringLiteral("Fusion"))) {
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    }

    MainWindow window;
    window.show();

    return app.exec();
}
