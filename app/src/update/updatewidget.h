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

class UpdateWidget final : public QWidget {
    Q_OBJECT
public:
    explicit UpdateWidget(QWidget *parent = nullptr);
    ~UpdateWidget() override;
    bool startInstaller(QString *error);

signals:
    void installRequested();

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
    QNetworkReply *get(const QUrl &url);

    State state_ = State::Idle;
    QLabel *status_ = nullptr;
    QLabel *notes_ = nullptr;
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
