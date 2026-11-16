#pragma once

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

// The test executable doubles as a help-only CLI. No Python installation,
// personal tool directory, accounts or authenticated commands are involved.
inline int runCliFixture(const QStringList &arguments)
{
    QFile log("fixture-invocations.jsonl");
    if (!log.open(QIODevice::WriteOnly | QIODevice::Append)) return 2;
    log.write(QJsonDocument(QJsonArray::fromStringList(arguments)).toJson(QJsonDocument::Compact) + '\n');
    if (arguments.first() != "-m" || arguments.value(1) != "cannlab"
        || arguments.last() != "--help" || (arguments.size() != 3 && arguments.size() != 4)) return 64;

    QByteArray help;
    if (arguments.size() == 3) {
        help = QStringLiteral(
            "usage: cannlab COMMAND\n\nCommands:\n"
            "  menu          交互菜单\n"
            "  accounts      List accounts\n"
            "  environments  List environments\n"
            "  start         Start a job\n"
            "  stop          Stop a job\n"
            "  status        Show job status\n"
            "  config        Show configuration\n"
            "  logout        Sign out\n").toUtf8();
    } else if (arguments.at(2) == "start") {
        help = "usage: cannlab start --account ACCOUNT --resource RESOURCE [--minutes MINUTES] "
               "[--interval INTERVAL] [--name NAME] [--dry-run] [--retries RETRIES] [--force]\n\n"
               "options:\n"
               "  --account ACCOUNT    accounts identifier\n"
               "  --resource RESOURCE  environments identifier\n"
               "  --minutes MINUTES    Duration\n"
               "  --interval INTERVAL  Poll interval\n"
               "  --name NAME          Job name\n"
               "  --dry-run            Preview\n"
               "  --retries RETRIES    Retry count\n"
               "  --force              Force\n";
    } else if (arguments.at(2) == "environments") {
        help = "usage: cannlab environments --account ACCOUNT\n\noptions:\n"
               "  --account ACCOUNT  accounts identifier\n";
    } else if (QStringList{"menu", "accounts", "stop", "status", "config", "logout"}.contains(arguments.at(2))) {
        help = "usage: cannlab " + arguments.at(2).toUtf8() + "\n\noptions:\n  --help  Show help\n";
    } else {
        return 64;
    }
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly)) return 2;
    return output.write(help) == help.size() ? 0 : 2;
}
