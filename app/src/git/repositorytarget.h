#pragma once
#include <QString>

struct RepositoryTarget {
    QString type = QStringLiteral("local");
    QString location; // WSL distribution or SSH host alias
    QString user;     // empty means the environment's default user
    QString directory;
    bool operator==(const RepositoryTarget &other) const {
        return type == other.type && location == other.location && user == other.user && directory == other.directory;
    }
    QString description() const {
        return type == QStringLiteral("local") ? QStringLiteral("本地 · ") + directory
            : QStringLiteral("%1 · %2%3 · %4").arg(type == QStringLiteral("wsl") ? QStringLiteral("WSL") : QStringLiteral("SSH"),
                location, user.isEmpty() ? QString() : QStringLiteral(" / ") + user, directory);
    }
};
