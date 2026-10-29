#pragma once

#include "releaseinfo.h"

#include <QWidget>
#include <memory>

class QLabel;
class QPushButton;
class QProgressBar;
class QNetworkAccessManager;
class QNetworkReply;
class QTemporaryDir;
class QFile;
class QCryptographicHash;
class QProcess;
class QShowEvent;
class QCheckBox;
class QTextBrowser;
class UpdateChecker;

void scheduleCompletedUpdateCleanup();

class UpdateWidget final : public QWidget {
    Q_OBJECT
public:
    explicit UpdateWidget(QWidget *parent = nullptr, UpdateChecker *checker = nullptr);
    ~UpdateWidget() override;
    bool startInstaller(QString *error);
    void startAutomaticChecks();

signals:
    void installRequested();
    void updateAvailableChanged(const QString &tag);
    void updateNotificationRequested(const QString &tag);

protected:
    void showEvent(QShowEvent *event) override;

private:
    enum class State { Idle, Checking, Available, Downloading, Preparing, Ready };
    void check();
    void download();
    void downloadArchive();
    void prepare();
    void cancel();
    void fail(const QString &message);
    void setState(State state, const QString &message);
    void refreshResult();
    void showRelease();
    void refreshNotes();
    QNetworkReply *get(const QUrl &url);

    State state_ = State::Idle;
    QLabel *status_ = nullptr;
    QLabel *resultStatus_ = nullptr;
    QTextBrowser *notes_ = nullptr;
    QCheckBox *automaticCheck_ = nullptr;
    UpdateChecker *checker_ = nullptr;
    QString statusBeforeCheck_;
    QString displayedNotes_;
    QPushButton *check_ = nullptr;
    QPushButton *action_ = nullptr;
    QPushButton *cancel_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QNetworkAccessManager *network_ = nullptr;
    QNetworkReply *reply_ = nullptr;
    QProcess *prepareProcess_ = nullptr;
    ReleaseInfo release_;
    std::unique_ptr<QTemporaryDir> workspace_;
    std::unique_ptr<QFile> archive_;
    std::unique_ptr<QCryptographicHash> hash_;
    QByteArray lastResult_;
};
