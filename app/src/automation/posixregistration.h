#pragma once
#include <QString>
#include <QByteArray>

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
// Only this fixed, read-only bootstrap uses sh. The entered path is a separate
// positional parameter, never shell source. Resolve HOME in the selected Linux
// user context, and pin that actual username for future reads and commands.
inline QString registrationScript()
{
    return QString::fromLatin1(R"SH(set -eu
p=$1
case "$p" in
  '~') p=$HOME ;;
  '~/'*) p="$HOME/${p#\~/}" ;;
  /*) ;;
  *) printf '%s\n' 'Use an absolute Linux path or ~/.' >&2; exit 2 ;;
esac
if [ -d "$p" ]; then p="${p%/}/orchestrate-tool.json"; fi
if [ ! -f "$p" ]; then printf '%s\n' "Manifest is not a regular file: $p" >&2; exit 2; fi
dir=$(CDPATH= cd -P -- "$(dirname -- "$p")" && pwd -P)
p="${dir%/}/$(basename -- "$p")"
u=$(id -un)
printf '%s\000%s\000' "$u" "$p"
cat -- "$p"
)SH");
}

inline bool registrationResult(const QByteArray &bytes, QString *user, QString *path, QByteArray *manifest)
{
    const auto first = bytes.indexOf('\0');
    const auto second = first < 0 ? -1 : bytes.indexOf('\0', first + 1);
    if (first <= 0 || second <= first + 1) return false;
    *user = QString::fromUtf8(bytes.left(first));
    *path = QString::fromUtf8(bytes.mid(first + 1, second - first - 1));
    *manifest = bytes.mid(second + 1);
    return validUser(*user) && validPath(*path) && path->startsWith(QLatin1Char('/'));
}


}
