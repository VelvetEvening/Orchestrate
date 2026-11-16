#pragma once

#include "data/appdatabase.h"
#include <functional>

class QWidget;

// Dialogs read and validate a source; the page owns registration and grouping.
namespace ManifestImport {
struct Source {
    QByteArray data;
    QString path;
    QString sshHost;
    QString wslDistribution;
    QString wslUser;
    QString sshUser;
};
using Callback = std::function<void(bool, const Source &, const QString &)>;

bool readLocal(QWidget *parent, Callback callback);
bool readSsh(QWidget *parent, const AppDatabase &database, Source *source);
bool readWsl(QWidget *parent, const AppDatabase &database, Source *source);
}
