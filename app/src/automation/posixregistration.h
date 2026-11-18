#pragma once
#include <QString>

namespace PosixRegistration {
inline bool validUser(const QString &user)
{
    if (user.isEmpty() || user.startsWith(QLatin1Char('-')) || user.contains(QLatin1Char('@'))) return false;
    for (const auto c : user) if (c.isSpace() || c.unicode() < 32) return false;
    return true;
}
inline bool validPath(const QString &path)
{
    return !path.isEmpty() && !path.contains(QChar::Null) && !path.contains(QLatin1Char('\\'))
        && !path.contains(QLatin1Char('\n')) && !path.contains(QLatin1Char('\r'));
}
inline QString quote(QString value)
{
    value.replace(QStringLiteral("'"), QStringLiteral("'\"'\"'"));
    return QLatin1Char('\'') + value + QLatin1Char('\'');
}
}
