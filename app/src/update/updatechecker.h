#pragma once

#include "releaseinfo.h"
#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// One asynchronous metadata request per day, independent of the visible page.
// The cache also remembers failed attempts and notification acknowledgements.
class UpdateChecker final : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(const QString &cachePath = QString(),
                           QNetworkAccessManager *network = nullptr, QObject *parent = nullptr);
    ~UpdateChecker() override;
    void start();
    void checkNow();
    void cancel();
    void setPaused(bool paused);
    bool setAutomaticEnabled(bool enabled, QString *error = nullptr);
    bool automaticEnabled() const { return automaticEnabled_; }
    bool checking() const { return reply_ != nullptr; }
    bool hasUpdate() const;
    const ReleaseInfo &release() const { return release_; }

signals:
    void checkStarted(bool automatic);
    void checkFinished(bool automatic, const QString &error);
    void releaseChanged();
    void notificationRequested(const QString &tag);

private:
    friend class UpdateCheckerTest;
    static constexpr qint64 dayMs = 24 * 60 * 60 * 1000;
    static constexpr int startupDelaySeconds = 180;
    static constexpr qint64 maxResponseBytes = 4 * 1024 * 1024;
    void load();
    bool save(QString *error = nullptr);
    void schedule();
    void checkAutomatic();
    void check(bool automatic);
    void finish(const QString &error);
    void announce(bool automatic);

    QString cachePath_;
    QPointer<QNetworkAccessManager> network_;
    QPointer<QNetworkReply> reply_;
    QTimer timer_;
    QDateTime earliestCheck_;
    QDateTime lastAttempt_;
    QString notifiedTag_;
    QJsonObject cachedRelease_;
    ReleaseInfo release_;
    QByteArray response_;
    bool automaticEnabled_ = true;
    bool started_ = false;
    bool paused_ = false;
    bool automaticRequest_ = false;
    int requestTimeoutMs_ = 15000;
    std::function<QDateTime()> now_ = [] { return QDateTime::currentDateTimeUtc(); };
};
