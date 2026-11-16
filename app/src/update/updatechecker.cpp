#include "updatechecker.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <algorithm>

UpdateChecker::UpdateChecker(const QString &cachePath, QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), cachePath_(cachePath.isEmpty()
          ? QCoreApplication::applicationDirPath() + QStringLiteral("/data/update-check.json") : cachePath),
      network_(network)
{
    timer_.setSingleShot(true);
    timer_.setTimerType(Qt::VeryCoarseTimer);
    connect(&timer_, &QTimer::timeout, this, &UpdateChecker::checkAutomatic);
    load();
}

UpdateChecker::~UpdateChecker()
{
    if (reply_) {
        disconnect(reply_, nullptr, this, nullptr);
        reply_->abort();
        reply_->deleteLater();
    }
}

void UpdateChecker::load()
{
    QFile file(cachePath_);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxResponseBytes + 4096) return;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    automaticEnabled_ = object.value(QStringLiteral("automatic_enabled")).toBool(true);
    lastAttempt_ = QDateTime::fromString(object.value(QStringLiteral("last_attempt")).toString(), Qt::ISODateWithMs);
    notifiedTag_ = object.value(QStringLiteral("notified_tag")).toString();
    cachedRelease_ = object.value(QStringLiteral("release")).toObject();
    QString error;
    if (!ReleaseInfo::parse(cachedRelease_, &release_, &error)) cachedRelease_ = {};
}

bool UpdateChecker::save(QString *error)
{
    QSaveFile file(cachePath_);
    const QJsonObject object {{QStringLiteral("automatic_enabled"), automaticEnabled_},
        {QStringLiteral("last_attempt"), lastAttempt_.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("notified_tag"), notifiedTag_}, {QStringLiteral("release"), cachedRelease_}};
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (!QDir().mkpath(QFileInfo(cachePath_).absolutePath()) || !file.open(QIODevice::WriteOnly)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = QStringLiteral("无法保存自动更新检查设置：%1").arg(file.errorString());
        return false;
    }
    return true;
}

bool UpdateChecker::hasUpdate() const
{
    return !release_.tag.isEmpty() && QVersionNumber::compare(release_.version,
        QVersionNumber::fromString(QCoreApplication::applicationVersion())) > 0;
}

void UpdateChecker::start()
{
    if (started_) return;
    started_ = true;
    earliestCheck_ = now_().addSecs(startupDelaySeconds);
    emit releaseChanged(); // Make a cached update visible before contacting GitHub.
    schedule();
}

void UpdateChecker::schedule()
{
    timer_.stop();
    if (!started_ || !automaticEnabled_ || paused_ || checking()) return;
    const auto now = now_();
    // A clock correction must not postpone checks indefinitely.
    if (lastAttempt_ > now) { lastAttempt_ = now; save(); }
    auto due = earliestCheck_;
    if (lastAttempt_.isValid()) due = std::max(due, lastAttempt_.addMSecs(dayMs));
    timer_.start(int(std::clamp<qint64>(now.msecsTo(due), 1000, dayMs)));
}

void UpdateChecker::checkAutomatic()
{
    if (!started_ || !automaticEnabled_ || paused_ || checking()) return;
    const auto now = now_();
    if (now < earliestCheck_ || (lastAttempt_.isValid() && lastAttempt_.msecsTo(now) < dayMs)) {
        schedule(); // Coarse timers may wake slightly early.
        return;
    }
    check(true);
}

void UpdateChecker::checkNow()
{
    if (!paused_ && !checking()) check(false);
}

bool UpdateChecker::setAutomaticEnabled(bool enabled, QString *error)
{
    if (automaticEnabled_ == enabled) return true;
    const bool previous = automaticEnabled_;
    automaticEnabled_ = enabled;
    if (!save(error)) { automaticEnabled_ = previous; return false; }
    if (!enabled && checking() && automaticRequest_) cancel();
    if (enabled) earliestCheck_ = now_().addSecs(startupDelaySeconds);
    schedule();
    return true;
}

void UpdateChecker::setPaused(bool paused)
{
    if (paused_ == paused) return;
    paused_ = paused;
    schedule();
}

void UpdateChecker::check(bool automatic)
{
    timer_.stop();
    lastAttempt_ = now_();
    // Persist the attempt first so offline restarts cannot cause retry storms.
    // A manual request is still allowed when the cache directory is read-only.
    if (!save() && automatic) { schedule(); return; }
    automaticRequest_ = automatic;
    response_.clear();
    if (!network_) network_ = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/VelvetEvening/Orchestrate/releases/latest")));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Orchestrate/") + QCoreApplication::applicationVersion());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(requestTimeoutMs_);
    auto *reply = network_->get(request);
    reply_ = reply;
    reply->setReadBufferSize(64 * 1024);
    // Bound total time, not only the interval between received packets.
    auto *deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, [reply] {
        reply->setProperty("checkError", QStringLiteral("检查更新超时，请稍后重试。"));
        reply->abort();
    });
    deadline->start(requestTimeoutMs_);
    const auto drain = [this, reply] {
        response_ += reply->read(maxResponseBytes + 1 - response_.size());
        if (response_.size() > maxResponseBytes) {
            reply->setProperty("checkError", QStringLiteral("Release 响应过大。"));
            reply->abort();
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, drain);
    connect(reply, &QNetworkReply::finished, this, [this, reply, deadline, drain] {
        deadline->stop();
        if (response_.size() <= maxResponseBytes) drain();
        QString error = reply->property("checkError").toString();
        if (error.isEmpty() && reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            error = status == 429 || (status == 403 && reply->rawHeader("X-RateLimit-Remaining") == "0")
                ? QStringLiteral("GitHub 暂时限制了更新检查次数，请稍后手动重试。")
                : QStringLiteral("检查更新失败：%1").arg(reply->errorString());
        }
        if (error.isEmpty()) {
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(response_, &parseError);
            ReleaseInfo result;
            if (parseError.error != QJsonParseError::NoError || !document.isObject())
                error = QStringLiteral("GitHub 返回了无效的 Release 信息。");
            else if (ReleaseInfo::parse(document.object(), &result, &error)) {
                release_ = result;
                cachedRelease_ = document.object();
                save();
            }
        }
        reply_ = nullptr;
        reply->deleteLater();
        response_ = {}; // Do not retain a large network buffer while idle.
        finish(error);
    });
    emit checkStarted(automatic);
}

void UpdateChecker::announce(bool automatic)
{
    if (!hasUpdate() || QVersionNumber::compare(release_.version,
        QVersionNumber::fromString(notifiedTag_.mid(1))) <= 0) return;
    notifiedTag_ = release_.tag;
    // Never toast repeatedly across restarts if the acknowledgement cannot be saved.
    if (save() && automatic) emit notificationRequested(release_.tag);
}

void UpdateChecker::finish(const QString &error)
{
    if (error.isEmpty()) {
        emit releaseChanged();
        announce(automaticRequest_);
    }
    emit checkFinished(automaticRequest_, error);
    schedule();
}

void UpdateChecker::cancel()
{
    if (!reply_) return;
    auto *reply = reply_.data();
    reply_ = nullptr;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
    response_ = {};
    finish(QStringLiteral("已取消更新检查。"));
}
