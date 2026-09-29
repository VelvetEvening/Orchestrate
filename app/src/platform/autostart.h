#pragma once

#include <QSettings>
#include <QString>
#include <memory>

// The operating system owns this preference; do not duplicate it in SQLite.
class AutoStart final
{
public:
    struct State {
        bool enabled = false;
        bool disabledByWindows = false;
        QString message;
        QString error;
    };

    AutoStart();
    // An explicit store lets tests exercise persistence without touching login settings.
    AutoStart(std::unique_ptr<QSettings> registration, std::unique_ptr<QSettings> approval,
              const QString &executablePath);
    State state() const;
    bool setEnabled(bool enabled, QString *error);

private:
    QString command_;
    std::unique_ptr<QSettings> registration_;
    std::unique_ptr<QSettings> approval_;
};
