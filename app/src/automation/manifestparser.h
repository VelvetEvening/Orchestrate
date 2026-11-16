#pragma once

#include "data/appdatabase.h"

// Manifest validation and path resolution have no QWidget dependency.
namespace ManifestParser {
struct Manifest {
    AppDatabase::AutomationTool tool;
    QList<AppDatabase::ToolCommand> commands;
    QString group;
};

bool read(const QString &sourcePath, QByteArray *data, QString *errorMessage);
bool parse(const QByteArray &data, const QString &sourcePath, const QString &sshHost,
           Manifest *manifest, QString *errorMessage,
           const QString &wslDistribution = {}, const QString &wslUser = {},
           const QString &sshUser = {});
bool applyUpdate(AppDatabase &database, int toolId, Manifest manifest, QString *errorMessage);
}
