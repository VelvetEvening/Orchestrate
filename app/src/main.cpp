#include "mainwindow.h"
#include "version.h"
#include "platform/instancelock.h"

#include <QApplication>
#include <QIcon>
#include <QStyleFactory>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Orchestrate"));
    QApplication::setApplicationDisplayName(QStringLiteral("Orchestrate"));
    QApplication::setApplicationVersion(QStringLiteral(ORCHESTRATE_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Orchestrate"));

    InstanceLock instance(QCoreApplication::applicationDirPath());
    if (!instance.acquired()) {
        QMessageBox::information(nullptr, QStringLiteral("Orchestrate"), QStringLiteral("此目录的 Orchestrate 已在运行或正在更新。请从系统托盘打开。"));
        return 1;
    }

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

    const auto arguments = app.arguments();
    const int healthArgument = arguments.indexOf(QStringLiteral("--update-health-file"));
    if (window.ready() && healthArgument > 0 && healthArgument + 1 < arguments.size()) {
        const QFileInfo healthFile(arguments.at(healthArgument + 1));
        const QFileInfo workspace(healthFile.absolutePath());
        if (healthFile.fileName() == QStringLiteral("startup.json")
            && workspace.fileName().startsWith(QStringLiteral(".Orchestrate-update-"))
            && workspace.dir().absolutePath() == QFileInfo(app.applicationDirPath()).dir().absolutePath()) {
            QSaveFile file(healthFile.absoluteFilePath());
            const QJsonObject health {{QStringLiteral("version"), app.applicationVersion()},
                                      {QStringLiteral("process_id"), app.applicationPid()}};
            if (file.open(QIODevice::WriteOnly)) {
                file.write(QJsonDocument(health).toJson());
                file.commit();
            }
        }
    }

    return app.exec();
}
