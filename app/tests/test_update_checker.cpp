#include "test_support.h"
#include "update/updatechecker.h"
#include "update/updatewidget.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollBar>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QtTest>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace {
QJsonObject release(const QString &tag = QStringLiteral("v2.3.0"), const QString &notes = QStringLiteral("# 更新说明\n\n- 完整正文"))
{
    const QString name = QStringLiteral("Orchestrate-%1-windows-x64.zip").arg(tag);
    return {{QStringLiteral("tag_name"), tag}, {QStringLiteral("body"), notes},
        {QStringLiteral("assets"), QJsonArray{QJsonObject{{QStringLiteral("name"), name},
            {QStringLiteral("browser_download_url"), QStringLiteral("https://github.com/VelvetEvening/Orchestrate/releases/download/") + tag + '/' + name},
            {QStringLiteral("size"), 12345}, {QStringLiteral("digest"), QStringLiteral("sha256:") + QString(64, 'a')}}}}};
}

class Reply final : public QNetworkReply {
public:
    Reply(const QNetworkRequest &request, QByteArray data, int delay, bool failure, int status, QObject *parent)
        : QNetworkReply(parent), data_(std::move(data)), failure_(failure)
    {
        setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (status == 403) setRawHeader("X-RateLimit-Remaining", "0");
        if (delay >= 0) QTimer::singleShot(delay, this, [this] {
            if (isFinished()) return;
            if (failure_) setError(ConnectionRefusedError, QStringLiteral("测试离线"));
            available_ = true;
            emit readyRead();
            if (isFinished()) return;
            setFinished(true); emit finished();
        });
    }
    void abort() override {
        if (isFinished()) return;
        setError(OperationCanceledError, QStringLiteral("cancelled"));
        setFinished(true); emit finished();
    }
    qint64 bytesAvailable() const override { return (available_ ? data_.size() - offset_ : 0) + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char *target, qint64 size) override {
        if (!available_) return -1;
        const auto count = qMin<qint64>(size, data_.size() - offset_);
        if (!count) return -1;
        memcpy(target, data_.constData() + offset_, count); offset_ += count; return count;
    }
private:
    QByteArray data_;
    bool failure_;
    bool available_ = false;
    qint64 offset_ = 0;
};

class Network final : public QNetworkAccessManager {
public:
    QByteArray response = QJsonDocument(release()).toJson();
    int delay = 0;
    bool failure = false;
    int requests = 0;
    int status = 200;
    QList<QNetworkRequest> captured;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        ++requests; captured.append(request);
        return new Reply(request, response, delay, failure, status, this);
    }
};
}

class UpdateCheckerTest : public QObject {
    Q_OBJECT
private slots:
    void startupAndDailyCadenceSurviveRestart()
    {
        QTemporaryDir dir; Network network;
        auto now = QDateTime::currentDateTimeUtc();
        UpdateChecker checker(dir.filePath("check.json"), &network);
        checker.now_ = [&] { return now; };
        QSignalSpy done(&checker, &UpdateChecker::checkFinished);
        QSignalSpy notifications(&checker, &UpdateChecker::notificationRequested);
        checker.start();
        QVERIFY(checker.timer_.isSingleShot());
        QVERIFY(checker.timer_.remainingTime() >= 179000);
        checker.checkAutomatic(); QCOMPARE(network.requests, 0);
        now = now.addSecs(120); checker.checkAutomatic(); QCOMPARE(network.requests, 0);
        now = now.addSecs(61); checker.checkAutomatic();
        QTRY_COMPARE(done.count(), 1);
        QCOMPARE(network.requests, 1); QCOMPARE(notifications.count(), 1);
        QCOMPARE(network.captured.first().url().path(), QStringLiteral("/repos/VelvetEvening/Orchestrate/releases/latest"));
        checker.checkAutomatic(); QCOMPARE(network.requests, 1);
        UpdateChecker reopened(dir.filePath("check.json"), &network);
        reopened.now_ = [&] { return now; };
        QSignalSpy again(&reopened, &UpdateChecker::notificationRequested);
        QSignalSpy next(&reopened, &UpdateChecker::checkFinished);
        QVERIFY(reopened.hasUpdate()); reopened.start();
        QVERIFY(reopened.timer_.remainingTime() > 23 * 60 * 60 * 1000);
        now = now.addSecs(24 * 60 * 60 + 1); reopened.checkAutomatic();
        QTRY_COMPARE(next.count(), 1); QCOMPARE(network.requests, 2); QCOMPARE(again.count(), 0);
    }

    void offlinePreservesKnownUpdateAndDoesNotRetry()
    {
        QTemporaryDir dir; Network network;
        auto now = QDateTime::currentDateTimeUtc();
        UpdateChecker checker(dir.filePath("check.json"), &network);
        checker.now_ = [&] { return now; };
        UpdateWidget widget(nullptr, &checker); widget.startAutomaticChecks();
        QSignalSpy done(&checker, &UpdateChecker::checkFinished);
        checker.checkNow(); QTRY_COMPARE(done.count(), 1);
        auto *status = widget.findChild<QLabel *>("updateStatus");
        const auto previous = status->text();
        network.failure = true;
        now = now.addDays(1).addSecs(1); checker.checkAutomatic();
        QTRY_COMPARE(done.count(), 2); QVERIFY(!done.last().at(1).toString().isEmpty());
        QVERIFY(checker.hasUpdate()); QCOMPARE(status->text(), previous);
        for (int i = 0; i < 10; ++i) checker.checkAutomatic();
        QCOMPARE(network.requests, 2);
        UpdateChecker restarted(dir.filePath("check.json"), &network);
        restarted.now_ = [&] { return now; }; restarted.start();
        now = now.addSecs(181); restarted.checkAutomatic(); QCOMPARE(network.requests, 2);
        checker.checkNow(); QTRY_COMPARE(done.count(), 3);
        QVERIFY(status->text().contains(QStringLiteral("失败")));
        QVERIFY(widget.findChild<QPushButton *>("installUpdateButton")->isEnabled());
    }

    void disableCancelsAndStillAllowsManualCheck()
    {
        QTemporaryDir dir; Network network; network.delay = -1;
        auto now = QDateTime::currentDateTimeUtc();
        UpdateChecker checker(dir.filePath("check.json"), &network);
        checker.now_ = [&] { return now; }; checker.start();
        QSignalSpy done(&checker, &UpdateChecker::checkFinished);
        now = now.addSecs(181); checker.checkAutomatic(); QVERIFY(checker.checking());
        QString error; QVERIFY(checker.setAutomaticEnabled(false, &error));
        QVERIFY(!checker.checking()); QVERIFY(!checker.timer_.isActive()); QCOMPARE(done.count(), 1);
        UpdateChecker restarted(dir.filePath("check.json"), &network);
        QVERIFY(!restarted.automaticEnabled());
        network.delay = 0; checker.checkNow(); QTRY_COMPARE(done.count(), 2);
        QVERIFY(checker.hasUpdate()); QVERIFY(!checker.timer_.isActive());
    }

    void pausedChecksAndDuplicateManualClicks()
    {
        QTemporaryDir dir; Network network; network.delay = 30;
        auto now = QDateTime::currentDateTimeUtc();
        UpdateChecker checker(dir.filePath("check.json"), &network);
        checker.now_ = [&] { return now; }; checker.start(); checker.setPaused(true);
        now = now.addDays(1); checker.checkAutomatic(); checker.checkNow(); QCOMPARE(network.requests, 0);
        checker.setPaused(false); checker.checkAutomatic(); checker.checkNow(); checker.checkNow();
        QCOMPARE(network.requests, 1); QTRY_VERIFY(!checker.checking());
        QVERIFY(checker.timer_.remainingTime() > 23 * 60 * 60 * 1000);
    }

    void timeoutMalformedAndOversizedResponses()
    {
        QTemporaryDir dir; Network network;
        UpdateChecker checker(dir.filePath("check.json"), &network);
        checker.requestTimeoutMs_ = 30;
        QSignalSpy done(&checker, &UpdateChecker::checkFinished);
        network.delay = -1; checker.checkNow(); QTRY_COMPARE(done.count(), 1);
        QVERIFY(done.last().at(1).toString().contains(QStringLiteral("超时")));
        network.delay = 0; network.response = "not json";
        checker.checkNow(); QTRY_COMPARE(done.count(), 2);
        QVERIFY(!checker.hasUpdate()); QVERIFY(!done.last().at(1).toString().isEmpty());
        network.response = QByteArray(4 * 1024 * 1024 + 1, 'x');
        checker.checkNow(); QTRY_COMPARE(done.count(), 3);
        QVERIFY(done.last().at(1).toString().contains(QStringLiteral("过大")));
        QTRY_COMPARE(network.findChildren<QNetworkReply *>().size(), 0);
        QCOMPARE(checker.response_.size(), 0);
        network.response = "{}"; network.failure = true; network.status = 403;
        checker.checkNow(); QTRY_COMPARE(done.count(), 4);
        QVERIFY(done.last().at(1).toString().contains(QStringLiteral("限制")));
    }

    void destroyedOwnerAndCacheWriteFailure()
    {
        QTemporaryDir dir; Network network; network.delay = -1;
        auto *checker = new UpdateChecker(dir.filePath("check.json"), &network);
        QSignalSpy done(checker, &UpdateChecker::checkFinished);
        checker->checkNow(); delete checker;
        QCOMPARE(done.count(), 0); QTRY_COMPARE(network.findChildren<QNetworkReply *>().size(), 0);
        const auto blocked = dir.filePath("not-a-directory"); QFile file(blocked);
        QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        UpdateChecker unwritable(blocked + "/check.json", &network);
        QString error; QVERIFY(!unwritable.setAutomaticEnabled(false, &error));
        QVERIFY(unwritable.automaticEnabled()); QVERIFY(!error.isEmpty());
        unwritable.start(); unwritable.earliestCheck_ = QDateTime::currentDateTimeUtc().addSecs(-1);
        unwritable.checkAutomatic(); QCOMPARE(network.requests, 1);
        QVERIFY(unwritable.timer_.remainingTime() > 23 * 60 * 60 * 1000);
    }

    void cachedReleaseClearsAfterUpgradeAndClockCorrection()
    {
        QTemporaryDir dir; Network network;
        UpdateChecker checker(dir.filePath("check.json"), &network);
        checker.checkNow(); QTRY_VERIFY(!checker.checking());
        const auto oldVersion = QCoreApplication::applicationVersion();
        QCoreApplication::setApplicationVersion("2.3.0");
        UpdateChecker upgraded(dir.filePath("check.json"), &network);
        QVERIFY(!upgraded.hasUpdate());
        QCoreApplication::setApplicationVersion(oldVersion);
        upgraded.lastAttempt_ = QDateTime::currentDateTimeUtc().addDays(10); upgraded.start();
        QVERIFY(upgraded.timer_.remainingTime() <= 24 * 60 * 60 * 1000);
    }

    void previousUpdateResultSurvivesChecks()
    {
        QTemporaryDir dir; Network network;
        const auto data = QCoreApplication::applicationDirPath() + "/data";
        QDir().mkpath(data);
        const auto path = data + "/update-result.json";
        QVERIFY(!QFile::exists(path));
        const auto cleanup = qScopeGuard([&] { QFile::remove(path); });
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"message", "更新已完成"}, {"cleanup_error", "测试：备份清理待重试"}}).toJson()); file.close();
        UpdateChecker checker(dir.filePath("check.json"), &network);
        UpdateWidget widget(nullptr, &checker);
        checker.checkNow(); QTRY_VERIFY(!checker.checking());
        QVERIFY(widget.findChild<QLabel *>("previousUpdateResult")->text().contains("备份清理待重试"));
        QVERIFY(widget.findChild<QLabel *>("updateStatus")->text().contains("发现新版本"));
    }

    void completeNotesAreLazyScrollableAndReadable()
    {
        QTemporaryDir dir; Network network;
        QString notes = QStringLiteral("# 本次更新\n\n## 改进\n\n");
        for (int i = 0; i < 90; ++i) notes += QStringLiteral("- 第 %1 项：更新说明完整显示，支持滚动与复制。\n").arg(i + 1);
        notes += QStringLiteral("\n## 最后一节\n\n完整末尾标记_END\n\n![图片](https://example.com/should-not-load.png)\n<script>unsafe()</script>");
        network.response = QJsonDocument(release("v2.3.0", notes)).toJson();
        UpdateChecker checker(dir.filePath("check.json"), &network);
        UpdateWidget widget(nullptr, &checker);
        checker.checkNow(); QTRY_VERIFY(!checker.checking());
        auto *browser = widget.findChild<QTextBrowser *>("releaseNotes");
        QVERIFY(browser->toPlainText().isEmpty()); // Hidden settings do not lay out Markdown.
        widget.resize(650, 560); widget.show(); QTest::qWait(30);
        QVERIFY(browser->toPlainText().contains("完整末尾标记_END"));
        QVERIFY(browser->isReadOnly()); QVERIFY(browser->verticalScrollBar()->maximum() > 0);
        QVERIFY(!browser->document()->resource(QTextDocument::ImageResource, QUrl("https://example.com/should-not-load.png")).isValid());
        TestSupport::capture(widget, "update-notes-top");
        browser->verticalScrollBar()->setValue(browser->verticalScrollBar()->maximum());
        TestSupport::capture(widget, "update-notes-bottom");
        QCOMPARE(network.requests, 1);
        network.response = QJsonDocument(release("v2.3.0", QString())).toJson();
        checker.checkNow(); QTRY_VERIFY(!checker.checking());
        QCOMPARE(browser->toPlainText(), QStringLiteral("此版本未提供更新说明。"));
        network.response = QJsonDocument(release("v2.2.0")).toJson();
        checker.checkNow(); QTRY_VERIFY(!checker.checking());
        QVERIFY(browser->isHidden());
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv); app.setApplicationVersion("2.2.0");
#ifdef Q_OS_WIN
    const QDir fonts(QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts")));
    for (const auto &name : {QStringLiteral("msyh.ttc"), QStringLiteral("msyhbd.ttc"), QStringLiteral("segoeui.ttf")})
        QFontDatabase::addApplicationFont(fonts.filePath(name));
#endif
    app.setFont(QFont(QStringLiteral("Microsoft YaHei"), 9)); app.setStyle("Fusion");
#ifdef Q_OS_WIN
    if (app.arguments().contains("--idle-enabled") || app.arguments().contains("--idle-disabled")) {
        QTemporaryDir dir;
        UpdateChecker checker(dir.filePath("check.json"));
        if (app.arguments().contains("--idle-disabled")) checker.setAutomaticEnabled(false);
        checker.start();
        FILETIME created, exited, kernelBefore, userBefore, kernelAfter, userAfter;
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernelBefore, &userBefore);
        QTimer::singleShot(10000, &app, &QCoreApplication::quit); app.exec();
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernelAfter, &userAfter);
        const auto ticks = [](FILETIME value) { return (quint64(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
        PROCESS_MEMORY_COUNTERS_EX memory {}; memory.cb = sizeof(memory);
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory));
        QTextStream(stdout) << QJsonDocument(QJsonObject{{"automatic", checker.automaticEnabled()},
            {"seconds", 10}, {"cpu_ms", double(ticks(kernelAfter) + ticks(userAfter) - ticks(kernelBefore) - ticks(userBefore)) / 10000},
            {"private_bytes", qint64(memory.PrivateUsage)}, {"working_set_bytes", qint64(memory.WorkingSetSize)}}).toJson(QJsonDocument::Compact) << Qt::endl;
        return 0;
    }
#endif
    UpdateCheckerTest test; return QTest::qExec(&test, argc, argv);
}

#include "test_update_checker.moc"
