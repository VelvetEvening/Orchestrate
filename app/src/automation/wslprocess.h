#pragma once

#include "processcapture.h"
#include "posixregistration.h"
#include <QByteArray>
#include <QDir>
#include <QProcess>
#include <QStringDecoder>
#include <QTimer>
#include <functional>
#include <memory>

// Windows is only the launcher. Linux executables receive a literal argv via
// --exec; no PowerShell or user-composed shell command is involved.
namespace WslProcess {
inline QString program()
{
#ifdef Q_OS_WIN
    return QDir(qEnvironmentVariable("SystemRoot", "C:/Windows"))
        .filePath(QStringLiteral("System32/wsl.exe"));
#else
    return QStringLiteral("wsl.exe");
#endif
}

inline bool validDistribution(const QString &name)
{
    if (name.isEmpty() || name != name.trimmed() || name.startsWith(QLatin1Char('-'))) return false;
    for (const QChar c : name) if (c.unicode() < 32) return false;
    return true;
}

inline bool validName(const QString &name)
{
    if (!validDistribution(name)) return false;
    for (const QChar c : name) if (c.isSpace()) return false;
    return true;
}

inline bool validPath(const QString &path)
{
    return !path.isEmpty() && !path.contains(QChar::Null)
        && !path.contains(QLatin1Char('\\')) && !path.contains(QLatin1Char('\n'))
        && !path.contains(QLatin1Char('\r'));
}

inline QStringList arguments(const QString &distribution, const QString &user,
                             const QString &executable, const QStringList &args = {},
                             const QString &directory = QStringLiteral("/"))
{
    QStringList result {QStringLiteral("--distribution"), distribution};
    if (!user.isEmpty()) result << QStringLiteral("--user") << user;
    result << QStringLiteral("--cd") << directory << QStringLiteral("--exec") << executable;
    result.append(args);
    return result;
}

// wsl.exe's own listing/errors may be UTF-16LE, whereas Linux stdout is UTF-8.
inline QString diagnostic(const QByteArray &bytes) { return ProcessCapture::diagnostic(bytes); }

inline QStringList distributions(const QByteArray &bytes)
{
    QStringList names;
    for (const auto &line : diagnostic(bytes).split(QLatin1Char('\n'))) {
        const QString name = line.trimmed();
        if (validDistribution(name) && !names.contains(name)) names.append(name);
    }
    return names;
}

// First line is the actual default user; remaining lines are /etc/passwd.
inline QStringList users(const QByteArray &bytes)
{
    const auto lines = QString::fromUtf8(bytes).split(QLatin1Char('\n'));
    QStringList names;
    if (!lines.isEmpty() && PosixRegistration::validUser(lines.first().trimmed())) names << lines.first().trimmed();
    for (const auto &line : lines.mid(1)) {
        const auto fields = line.split(QLatin1Char(':'));
        if (fields.size() < 7 || !PosixRegistration::validUser(fields[0])) continue;
        bool ok = false;
        const int uid = fields[2].toInt(&ok);
        if (ok && (uid == 0 || (uid >= 1000 && uid < 65534))
            && !fields[6].endsWith(QStringLiteral("/nologin")) && !fields[6].endsWith(QStringLiteral("/false"))
            && !names.contains(fields[0])) names << fields[0];
    }
    return names;
}

using PosixRegistration::registrationScript;
using PosixRegistration::registrationResult;

using CaptureCallback = ProcessCapture::CaptureCallback;
inline void capture(QObject *context, const QStringList &args, CaptureCallback callback,
                    int timeoutMs = 30000, QProcess *process = nullptr)
{
    if (!process) {
        process = new QProcess(context);
        process->setProgram(program());
        process->setArguments(args);
    }
    ProcessCapture::capture(context, process, std::move(callback), QStringLiteral("WSL"), timeoutMs);
}
}
