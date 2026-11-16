#pragma once
#include "posixregistration.h"
#include "processcapture.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace SshConnection {
inline bool validHost(const QString &host)
{
    if (host.isEmpty() || host.startsWith(QLatin1Char('-'))) return false;
    for (auto c : host) if (c.isSpace() || c.unicode() < 32) return false;
    return true;
}
inline QStringList arguments(const QString &host, const QString &user, const QString &command)
{
    QStringList args {QStringLiteral("-T"), QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
        QStringLiteral("-o"), QStringLiteral("ConnectTimeout=10"),
        QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=yes")};
    if (!user.isEmpty()) args << QStringLiteral("-l") << user;
    args << host << command;
    return args;
}
inline void capture(QObject *context, const QString &host, const QString &user,
                    const QString &command, ProcessCapture::CaptureCallback callback)
{
    auto *process = new QProcess(context);
    process->setProgram(QStringLiteral("ssh"));
    process->setArguments(arguments(host, user, command));
    ProcessCapture::capture(context, process, std::move(callback), QStringLiteral("SSH"));
}

struct Directive { QString key; QStringList values; };
struct Config {
    QStringList aliases;
    QList<Directive> directives;
    QStringList warnings;
    static QStringList words(const QString &line)
    {
        QStringList result;
        QString word;
        QChar quote;
        bool escaped = false;
        for (qsizetype i = 0; i < line.size(); ++i) {
            const auto c = line[i];
            if (escaped) { word += c; escaped = false; continue; }
            if (c == QLatin1Char('\\') && i + 1 < line.size()
                && (line[i + 1].isSpace() || line[i + 1] == QLatin1Char('"')
                    || line[i + 1] == QLatin1Char('\'') || line[i + 1] == QLatin1Char('\\') || line[i + 1] == QLatin1Char('#'))) {
                escaped = true;
                continue;
            }
            if (!quote.isNull()) {
                if (c == quote) quote = {}; else word += c;
            } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) quote = c;
            else if (c == QLatin1Char('#')) break;
            else if (c.isSpace()) { if (!word.isEmpty()) { result << word; word.clear(); } }
            else word += c;
        }
        if (escaped) word += QLatin1Char('\\');
        if (!word.isEmpty()) result << word;
        return result;
    }
    // Static, read-only hints only: never invoke ssh -G (Match exec may run
    // commands), resolve credentials, read private keys, or contact servers.
    void read(const QString &path, const QString &sshDirectory, QSet<QString> &stack, int depth, qint64 &budget)
    {
        const QFileInfo info(path);
        if (!info.exists()) return;
        const QString canonical = info.canonicalFilePath();
        if (depth > 12 || stack.contains(canonical)) { warnings << QStringLiteral("跳过循环或过深的 Include：%1").arg(path); return; }
        if (info.size() > budget) { warnings << QStringLiteral("SSH 配置超过 1 MiB 读取上限。"); return; }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { warnings << QStringLiteral("无法读取 SSH 配置：%1").arg(path); return; }
        const QByteArray data = file.read(budget + 1);
        budget -= data.size();
        if (budget < 0) { warnings << QStringLiteral("SSH 配置超过读取上限。"); return; }
        stack.insert(canonical);
        for (QString line : QString::fromUtf8(data).split(QLatin1Char('\n'))) {
            // OpenSSH supports Keyword=value and Keyword = value.
            line.replace(QRegularExpression(QStringLiteral("^\\s*([A-Za-z]+)\\s*=\\s*")), QStringLiteral("\\1 "));
            auto tokens = words(line);
            if (tokens.isEmpty()) continue;
            const QString key = tokens.takeFirst().toLower();
            if (key == QStringLiteral("include")) {
                for (QString pattern : tokens) {
                    if (pattern.startsWith(QStringLiteral("~/"))) pattern = QDir::homePath() + pattern.mid(1);
                    else if (QDir::isRelativePath(pattern)) pattern = QDir(sshDirectory).filePath(pattern);
                    const QFileInfo included(pattern);
                    const auto files = included.dir().entryList({included.fileName()}, QDir::Files, QDir::Name);
                    for (const auto &name : files) read(included.dir().filePath(name), sshDirectory, stack, depth + 1, budget);
                }
            } else if (key == QStringLiteral("host") || key == QStringLiteral("match") || key == QStringLiteral("user")) {
                directives.append({key, tokens});
                // Only directly declared hosts belong in the default picker.
                // Included files may be private connection inventories managed
                // by extensions. Still retain their directives for user hints.
                if (key == QStringLiteral("host") && depth == 0) {
                    for (const auto &name : tokens)
                        if (validHost(name) && !name.contains(QRegularExpression(QStringLiteral("[*!?]"))) && !aliases.contains(name))
                            aliases << name;
                }
            }
        }
        stack.remove(canonical);
    }
    QString userHint(const QString &host) const
    {
        bool active = true;
        for (const auto &directive : directives) {
            if (directive.key == QStringLiteral("match")) active = false; // context-dependent: SSH decides on connection
            else if (directive.key == QStringLiteral("host")) {
                bool matched = false, excluded = false;
                for (QString pattern : directive.values) {
                    const bool negated = pattern.startsWith(QLatin1Char('!'));
                    if (negated) pattern.remove(0, 1);
                    const auto regex = QRegularExpression::fromWildcard(pattern, Qt::CaseInsensitive,
                        QRegularExpression::NonPathWildcardConversion);
                    if (regex.match(host).hasMatch()) { if (negated) excluded = true; else matched = true; }
                }
                active = matched && !excluded;
            } else if (active && directive.key == QStringLiteral("user") && !directive.values.isEmpty()) {
                const auto user = directive.values.first();
                return PosixRegistration::validUser(user) && !user.contains(QLatin1Char('%')) ? user : QString();
            }
        }
        return {};
    }
    static Config load(const QString &path = QDir::home().filePath(QStringLiteral(".ssh/config")))
    {
        Config config;
        QSet<QString> stack;
        qint64 budget = 1024 * 1024;
        config.read(path, QFileInfo(path).absolutePath(), stack, 0, budget);
        return config;
    }
};
}
