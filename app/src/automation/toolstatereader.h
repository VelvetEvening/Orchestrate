#pragma once

#include "processcapture.h"
#include "sshconnection.h"
#include "toolstate.h"
#include "wslprocess.h"
#include "../data/appdatabase.h"
#include "../platform/fileaccess.h"
#include <QFile>
#include <QFileInfo>

namespace ToolStateReader {
using Callback = std::function<void(bool, const QJsonObject &, const QString &)>;

// The same bounded, cancellable reader is used by the page and parameter forms.
inline void readLocal(QObject *context, const AppDatabase::AutomationTool &tool,
                      Callback callback, int timeoutMs = 30000)
{
    FileAccess::request(context, QStringLiteral("read-optional"), tool.statePath,
        [tool, callback](bool success, const QByteArray &bytes, const QString &diagnostic) {
            QJsonObject state;
            QString error = diagnostic;
            if (success && bytes == QByteArray(1, '\0'))
                state = {{QStringLiteral("result"), QStringLiteral("none")},
                    {QStringLiteral("summary"), QStringLiteral("还没有状态文件。")}};
            else if (success) success = ToolState::parse(bytes, tool.externalId, &state, &error);
            callback(success, state, error);
        }, timeoutMs);
}

// This reads only the registered JSON file. It never runs a tool command.
inline void read(QObject *context, const AppDatabase::AutomationTool &tool, Callback callback)
{
    if (tool.targetType != QStringLiteral("wsl") && tool.sshHost.isEmpty()) {
        readLocal(context, tool, std::move(callback));
        return;
    }
    auto *process = new QProcess(context);
    const bool wsl = tool.targetType == QStringLiteral("wsl");
    if (wsl) {
        process->setProgram(WslProcess::program());
        process->setArguments(WslProcess::arguments(tool.wslDistribution, tool.wslUser,
            QStringLiteral("cat"), {QStringLiteral("--"), tool.statePath}));
    } else {
        QString path = tool.statePath;
        path.replace(QStringLiteral("'"), QStringLiteral("'\"'\"'"));
        process->setProgram(QStringLiteral("ssh"));
        process->setArguments(SshConnection::arguments(tool.sshHost, tool.sshUser,
            QStringLiteral("cat -- '%1'").arg(path)));
    }
    ProcessCapture::capture(context, process,
        [tool, callback](bool success, const QByteArray &bytes, const QString &diagnostic) {
            QJsonObject state;
            QString error = diagnostic;
            if (success) success = ToolState::parse(bytes, tool.externalId, &state, &error);
            callback(success, state, error);
        }, wsl ? QStringLiteral("WSL 状态") : QStringLiteral("SSH 状态"));
}
}
