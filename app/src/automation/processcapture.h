#pragma once
#include <QProcess>
#include <QStringDecoder>
#include <QTimer>
#include <functional>
#include <memory>

namespace ProcessCapture {
inline QString diagnostic(const QByteArray &bytes)
{
    if (bytes.startsWith(QByteArray::fromHex("fffe")) || bytes.contains('\0')) {
        QStringDecoder decoder(QStringDecoder::Utf16LE);
        return decoder(bytes);
    }
    return QString::fromUtf8(bytes);
}

using CaptureCallback = std::function<void(bool, const QByteArray &, const QString &)>;
// Shared bounded asynchronous reader for discovery and manifest reads. Tests
// can supply a harmless process; callers select the SSH or WSL launcher.
inline void capture(QObject *context, QProcess *process, CaptureCallback callback,
                    const QString &channel, int timeoutMs = 30000)
{
    process->setParent(context);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    struct Buffer { QByteArray output; QByteArray errors; bool done = false; };
    const auto buffer = std::make_shared<Buffer>();
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    const auto finish = [process, timer, buffer, callback](bool ok, const QString &error) {
        if (buffer->done) return;
        buffer->done = true;
        timer->stop();
        if (process->state() != QProcess::NotRunning) process->kill();
        else process->deleteLater();
        callback(ok, ok ? buffer->output : QByteArray(), error);
    };
    const auto drain = [process, buffer, finish, channel] {
        if (buffer->done) return;
        constexpr qsizetype limit = 1024 * 1024;
        for (const auto stream : {QProcess::StandardOutput, QProcess::StandardError}) {
            process->setReadChannel(stream);
            auto &destination = stream == QProcess::StandardOutput ? buffer->output : buffer->errors;
            while (process->bytesAvailable() > 0) {
                destination += process->read(qMin<qint64>(64 * 1024, limit + 1 - destination.size()));
                if (destination.size() > limit) {
                    finish(false, QStringLiteral("%1 读取超过 1 MiB 上限。").arg(channel));
                    return;
                }
            }
        }
    };
    QObject::connect(process, &QProcess::readyReadStandardOutput, context, drain);
    QObject::connect(process, &QProcess::readyReadStandardError, context, drain);
    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), context,
                     [process, buffer, drain, finish, channel](int code, QProcess::ExitStatus status) {
        if (buffer->done) { process->deleteLater(); return; }
        drain();
        if (buffer->done) return;
        if (status == QProcess::NormalExit && code == 0) finish(true, {});
        else {
            const QString error = diagnostic(buffer->errors.isEmpty() ? buffer->output : buffer->errors).trimmed();
            finish(false, QStringLiteral("%1 读取失败（退出码 %2）：%3").arg(channel).arg(code).arg(error));
        }
    });
    QObject::connect(process, &QProcess::errorOccurred, context, [finish, process, channel](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(false, QStringLiteral("无法启动 %1：%2。请确认已安装对应客户端。").arg(channel, process->errorString()));
    });
    QObject::connect(timer, &QTimer::timeout, context, [finish, channel] {
        finish(false, QStringLiteral("%1 读取超时；请检查执行环境或连接后重试。").arg(channel));
    });
    timer->start(timeoutMs);
    process->start();
    process->closeWriteChannel();
}
}
