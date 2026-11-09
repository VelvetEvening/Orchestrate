#pragma once

#include "../automation/processcapture.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <cstdio>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

// File metadata/open/read and the shell may wait on an unavailable network
// share. Isolate those calls in a short-lived, console-free instance of this
// executable, before QApplication, the database or any tools are initialized.
// No worker remains resident; concurrency, queue size, bytes and time are bound.
namespace FileAccess {
constexpr qint64 maxBytes = 1024 * 1024;
constexpr int maxConcurrent = 2;
constexpr int maxPending = 32;
inline const QString helperFlag = QStringLiteral("--orchestrate-file-access");
using Callback = std::function<void(bool, const QByteArray &, const QString &)>;

inline bool isHelper(int argc, char **argv)
{
    return argc > 1 && QString::fromLocal8Bit(argv[1]) == helperFlag;
}

inline int runHelper(const QStringList &arguments, bool allowDirectoryOpen = true)
{
    if (arguments.size() != 4) return 2;
    const QString operation = arguments[2], path = arguments[3];
    QByteArray bytes;
    QString error;
    if (operation == QStringLiteral("open-directory") || operation == QStringLiteral("check-directory")) {
        if (!QFileInfo(path).isDir()) error = QStringLiteral("目录不存在、无法访问或不是文件夹。");
        else if (operation == QStringLiteral("open-directory")) {
            if (!allowDirectoryOpen) error = QStringLiteral("测试环境不打开文件管理器。");
#ifdef Q_OS_WIN
            else if (!QProcess::startDetached(QStringLiteral("explorer.exe"), {QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath())}))
                error = QStringLiteral("系统未能打开项目目录。");
#else
            else if (!QProcess::startDetached(QStringLiteral("xdg-open"), {QFileInfo(path).absoluteFilePath()}))
                error = QStringLiteral("系统未能打开项目目录。");
#endif
        }
    } else if (operation == QStringLiteral("read") || operation == QStringLiteral("read-optional")) {
        // One NUL is an unambiguous missing-file result: it cannot be valid JSON.
        if (operation == QStringLiteral("read-optional") && !QFileInfo::exists(path)) bytes = QByteArray(1, '\0');
        else {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) error = file.errorString();
            else {
                bytes = file.read(maxBytes + 1);
                if (bytes.size() > maxBytes) error = QStringLiteral("文件超过 1 MiB 上限。");
                else if (file.error() != QFileDevice::NoError) error = file.errorString();
            }
        }
    } else return 2;
    QFile output;
#ifdef Q_OS_WIN
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    if (!output.open(error.isEmpty() ? stdout : stderr, QIODevice::WriteOnly)) return 2;
    const auto payload = error.isEmpty() ? bytes : error.toUtf8();
    if (output.write(payload) != payload.size() || !output.flush()) return 2;
    return error.isEmpty() ? 0 : 1;
}

struct Request : QObject {
    Request(QObject *parent, QObject *context, QString action, QString location, Callback completion)
        : QObject(parent), owner(context), operation(std::move(action)), path(std::move(location)), callback(std::move(completion)) {
        deadline = new QTimer(this);
        deadline->setSingleShot(true);
    }
    QPointer<QObject> owner;
    QString operation, path;
    Callback callback;
    QTimer *deadline = nullptr;
    bool done = false;
    void retire() {
        deadline->stop();
        bool stopping = false;
        for (auto *process : findChildren<QProcess *>()) if (process->state() != QProcess::NotRunning) {
            stopping = true;
            process->kill();
        }
        // Do not destroy QProcess while it is running: its destructor waits.
        // A closing dialog drops ownership immediately; reap asynchronously.
        if (!stopping) deleteLater();
    }
    void cancel() { done = true; callback = {}; retire(); }
    void finish(bool ok, const QByteArray &bytes, const QString &error) {
        if (done) return;
        done = true;
        retire();
        const auto completion = std::move(callback);
        if (owner && completion) completion(ok, bytes, error);
    }
};

class Queue : public QObject {
public:
    explicit Queue(QObject *parent) : QObject(parent) {}
    QList<Request *> pending;
    int running = 0;
    void dispatch() {
        while (running < maxConcurrent && !pending.isEmpty()) {
            auto *request = pending.takeFirst();
            if (request->done) continue;
            ++running;
            auto *process = new QProcess(request);
            process->setProgram(QCoreApplication::applicationFilePath());
            process->setArguments({helperFlag, request->operation, request->path});
            const auto released = std::make_shared<bool>(false);
            const auto release = [this, released] {
                if (*released) return;
                *released = true;
                --running;
                QTimer::singleShot(0, this, [this] { dispatch(); });
            };
            connect(process, &QObject::destroyed, this, release);
            connect(process, &QProcess::finished, this, release);
            connect(process, &QProcess::finished, request, [request] { if (request->done) request->retire(); });
            ProcessCapture::capture(request, process, [request](bool ok, const QByteArray &bytes, const QString &error) {
                request->finish(ok, bytes, error);
            }, QStringLiteral("文件"));
        }
    }
};

inline Queue *queue()
{
    static QPointer<Queue> instance;
    if (!instance) instance = new Queue(QCoreApplication::instance());
    return instance;
}

inline void request(QObject *context, const QString &operation, const QString &path, Callback callback, int timeoutMs = 30000)
{
    auto *pool = queue();
    if (pool->pending.size() >= maxPending) {
        QTimer::singleShot(0, context, [callback] { callback(false, {}, QStringLiteral("文件读取繁忙，请稍后重试。")); });
        return;
    }
    auto *job = new Request(pool, context, operation, path, std::move(callback));
    QObject::connect(context, &QObject::destroyed, job, [job] { job->cancel(); });
    pool->pending.append(job);
    QObject::connect(job, &QObject::destroyed, pool, [pool, job] { pool->pending.removeAll(job); });
    // This includes time spent queued; a stalled share cannot retain requests.
    QObject::connect(job->deadline, &QTimer::timeout, job, [job] { job->finish(false, {}, QStringLiteral("文件访问超时，请检查路径后重试。")); });
    job->deadline->start(timeoutMs);
    pool->dispatch();
}
}
