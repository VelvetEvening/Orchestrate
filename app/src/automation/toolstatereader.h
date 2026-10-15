#pragma once

#include "processcapture.h"
#include "sshconnection.h"
#include "toolstate.h"
#include "wslprocess.h"
#include "../data/appdatabase.h"
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPromise>
#include <QThreadPool>

namespace ToolStateReader {
using Callback = std::function<void(bool, const QJsonObject &, const QString &)>;

struct Result {
    bool success = false;
    QJsonObject state;
    QString error;
};

// File I/O stays off the GUI thread even for Windows paths (including network
// shares). The worker owns no widgets; closing the form drops its callbacks.
inline void readLocal(QObject *context, const AppDatabase::AutomationTool &tool,
                      Callback callback, int timeoutMs = 30000)
{
    auto *request = new QObject(context);
    auto *watcher = new QFutureWatcher<Result>(request);
    auto *timer = new QTimer(request);
    timer->setSingleShot(true);
    const auto done = std::make_shared<bool>(false);
    const auto finish = [request, timer, done, callback](const Result &result) {
        if (*done) return;
        *done = true;
        timer->stop();
        request->deleteLater();
        callback(result.success, result.state, result.error);
    };
    QObject::connect(watcher, &QFutureWatcher<Result>::finished, request, [watcher, finish] {
        finish(watcher->result());
    });
    QObject::connect(timer, &QTimer::timeout, request, [finish] {
        finish({false, {}, QStringLiteral("本地状态读取超时；请检查文件路径后重试。")});
    });
    QPromise<Result> promise;
    promise.start();
    watcher->setFuture(promise.future());
    timer->start(timeoutMs);
    QThreadPool::globalInstance()->start([tool, promise = std::move(promise)]() mutable {
        Result result;
        if (!QFileInfo::exists(tool.statePath)) {
            result.success = true;
            result.state = {{QStringLiteral("result"), QStringLiteral("none")},
                {QStringLiteral("summary"), QStringLiteral("还没有状态文件。")}};
        } else {
            QFile file(tool.statePath);
            if (!file.open(QIODevice::ReadOnly)) result.error = file.errorString();
            else result.success = ToolState::parse(file.read(ToolState::maxBytes + 1),
                                                   tool.externalId, &result.state, &result.error);
        }
        promise.addResult(result);
        promise.finish();
    });
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
