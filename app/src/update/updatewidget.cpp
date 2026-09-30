#include "updatewidget.h"
#include "platform/instancelock.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QString powershell()
{
    return QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows")))
        .filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
}

QStringList scriptArguments(const QString &workspace, const QString &mode)
{
    return {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"),
            QStringLiteral("Bypass"), QStringLiteral("-File"), QDir::toNativeSeparators(workspace + QStringLiteral("/Update-Orchestrate.ps1")),
            QStringLiteral("-PlanPath"), QDir::toNativeSeparators(workspace + QStringLiteral("/plan.json")), mode};
}
}

void scheduleCompletedUpdateCleanup()
{
    const QString install = QCoreApplication::applicationDirPath();
    const QDir parent = QFileInfo(install).dir();
    const QString script = install + QStringLiteral("/updater/Update-Orchestrate.ps1");
    if (!QFileInfo::exists(script)
        || parent.entryList({QStringLiteral(".Orchestrate-update-*")}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty()) return;
    QTimer::singleShot(1500, QCoreApplication::instance(), [install, parent, script] {
        QProcess cleanup;
        cleanup.setProgram(powershell());
        cleanup.setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-File"),
            QDir::toNativeSeparators(script), QStringLiteral("-CleanupCompleted"),
            QStringLiteral("-InstallDirectory"), QDir::toNativeSeparators(install)});
        cleanup.setWorkingDirectory(parent.absolutePath());
#ifdef Q_OS_WIN
        cleanup.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
        cleanup.startDetached();
    });
}

UpdateWidget::UpdateWidget(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("updateWidget"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    auto *title = new QLabel(QStringLiteral("版本与更新"), this);
    title->setObjectName(QStringLiteral("cardTitle"));
    layout->addWidget(title);
    auto *version = new QLabel(QStringLiteral("Orchestrate %1").arg(QCoreApplication::applicationVersion()), this);
    layout->addWidget(version);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("updateStatus"));
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    notes_ = new QLabel(this);
    notes_->setTextFormat(Qt::PlainText);
    notes_->setWordWrap(true);
    notes_->setMaximumHeight(100);
    notes_->hide();
    layout->addWidget(notes_);
    progress_ = new QProgressBar(this);
    progress_->hide();
    layout->addWidget(progress_);
    auto *buttons = new QHBoxLayout;
    check_ = new QPushButton(QStringLiteral("检查更新"), this);
    check_->setObjectName(QStringLiteral("checkUpdatesButton"));
    action_ = new QPushButton(this);
    action_->setObjectName(QStringLiteral("installUpdateButton"));
    cancel_ = new QPushButton(QStringLiteral("取消"), this);
    auto *releasePage = new QPushButton(QStringLiteral("发布记录"), this);
    buttons->addWidget(check_);
    buttons->addWidget(action_);
    buttons->addWidget(cancel_);
    buttons->addWidget(releasePage);
    buttons->addStretch();
    layout->addLayout(buttons);
    connect(releasePage, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/VelvetEvening/Orchestrate/releases")));
    });
    connect(check_, &QPushButton::clicked, this, &UpdateWidget::check);
    connect(cancel_, &QPushButton::clicked, this, &UpdateWidget::cancel);
    connect(action_, &QPushButton::clicked, this, [this] {
        if (state_ == State::Available) download();
        else if (state_ == State::Ready) emit installRequested();
    });
    network_ = new QNetworkAccessManager(this);
    setState(State::Idle, QStringLiteral("尚未检查更新。"));
    refreshResult();
}

void UpdateWidget::refreshResult()
{
    QFile file(QCoreApplication::applicationDirPath() + QStringLiteral("/data/update-result.json"));
    if (state_ != State::Idle || !file.open(QIODevice::ReadOnly)) return;
    const QByteArray data = file.read(64 * 1024);
    if (data == lastResult_) return;
    lastResult_ = data;
    const QJsonObject result = QJsonDocument::fromJson(data).object();
    QString message = result.value(QStringLiteral("message")).toString();
    const QString cleanupError = result.value(QStringLiteral("cleanup_error")).toString();
    if (!cleanupError.isEmpty()) message += QStringLiteral("\n") + cleanupError;
    if (!message.isEmpty()) status_->setText(message);
}

void UpdateWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshResult();
}

UpdateWidget::~UpdateWidget()
{
    if (reply_) {
        disconnect(reply_, nullptr, this, nullptr);
        reply_->abort();
    }
    if (prepareProcess_ && prepareProcess_->state() != QProcess::NotRunning) {
        disconnect(prepareProcess_, nullptr, this, nullptr);
        prepareProcess_->kill();
        prepareProcess_->waitForFinished(5000);
    }
}

void UpdateWidget::setState(State state, const QString &message)
{
    state_ = state;
    status_->setText(message);
    const bool busy = state == State::Checking || state == State::Downloading || state == State::Preparing;
    check_->setEnabled(!busy);
    action_->setVisible(state == State::Available || state == State::Ready);
    action_->setText(state == State::Ready ? QStringLiteral("安装并重启") : QStringLiteral("下载更新"));
    cancel_->setVisible(state == State::Checking || state == State::Downloading);
    progress_->setVisible(state == State::Downloading || state == State::Preparing);
    if (state == State::Preparing) progress_->setRange(0, 0);
}

QNetworkReply *UpdateWidget::get(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Orchestrate/") + QCoreApplication::applicationVersion());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);
    reply_ = network_->get(request);
    return reply_;
}

void UpdateWidget::check()
{
    workspace_.reset();
    notes_->hide();
    setState(State::Checking, QStringLiteral("正在检查 GitHub Release…"));
    auto *reply = get(QUrl(QStringLiteral("https://api.github.com/repos/VelvetEvening/Orchestrate/releases/latest")));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply_ = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("检查更新失败：%1").arg(reply->errorString()));
            return;
        }
        if (reply->bytesAvailable() > 4 * 1024 * 1024) { fail(QStringLiteral("Release 响应过大。")); return; }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(reply->readAll(), &parseError);
        QString error;
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            fail(QStringLiteral("GitHub 返回了无效的 Release 信息。")); return;
        }
        if (!ReleaseInfo::parse(document.object(), &release_, &error)) { fail(error); return; }
        if (QVersionNumber::compare(release_.version, QVersionNumber::fromString(QCoreApplication::applicationVersion())) <= 0) {
            setState(State::Idle, QStringLiteral("当前已是最新版本（最新正式版：%1）。").arg(release_.tag));
            return;
        }
        notes_->setText(release_.notes.left(1200));
        notes_->show();
        setState(State::Available, QStringLiteral("发现新版本 %1（%2 MB）。").arg(release_.tag).arg(release_.archiveBytes / (1024.0 * 1024.0), 0, 'f', 1));
    });
}

void UpdateWidget::download()
{
    const QString parent = QFileInfo(QCoreApplication::applicationDirPath()).dir().absolutePath();
    workspace_ = std::make_unique<QTemporaryDir>(parent + QStringLiteral("/.Orchestrate-update-XXXXXX"));
    if (!workspace_->isValid()) { fail(QStringLiteral("无法创建更新目录，请确认程序的上级目录可写。")); return; }
    setState(State::Downloading, QStringLiteral("正在下载 %1…").arg(release_.tag));
    progress_->setRange(0, 100);
    progress_->setValue(0);
    if (!release_.digest.isEmpty()) { downloadArchive(); return; }
    auto *reply = get(release_.checksumUrl);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply_ = nullptr;
        reply->deleteLater();
        QString error;
        if (reply->error() != QNetworkReply::NoError) { fail(reply->errorString()); return; }
        if (reply->bytesAvailable() > 4096 || !ReleaseInfo::parseChecksum(reply->readAll(), release_.assetName, &release_.digest, &error)) {
            fail(error.isEmpty() ? QStringLiteral("校验文件过大。") : error); return;
        }
        downloadArchive();
    });
}

void UpdateWidget::downloadArchive()
{
    archive_ = std::make_unique<QFile>(workspace_->filePath(QStringLiteral("release.zip")));
    hash_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    if (!archive_->open(QIODevice::WriteOnly)) { fail(archive_->errorString()); return; }
    auto *reply = get(release_.archiveUrl);
    reply->setReadBufferSize(1024 * 1024);
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64) {
        progress_->setValue(int(qMin<qint64>(100, received * 100 / release_.archiveBytes)));
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        const QByteArray bytes = reply->readAll();
        if (archive_->size() + bytes.size() > release_.archiveBytes || archive_->write(bytes) != bytes.size()) {
            reply->setProperty("writeError", QStringLiteral("ZIP 大小不符或磁盘写入失败。"));
            reply->abort();
            return;
        }
        hash_->addData(bytes);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply_ = nullptr;
        reply->deleteLater();
        archive_->close();
        if (reply->error() != QNetworkReply::NoError) {
            fail(reply->property("writeError").toString().isEmpty() ? reply->errorString() : reply->property("writeError").toString()); return;
        }
        if (archive_->size() != release_.archiveBytes || hash_->result().toHex() != release_.digest) {
            fail(QStringLiteral("ZIP 校验失败，请重新下载。")); return;
        }
        archive_.reset();
        hash_.reset();
        prepare();
    });
}

void UpdateWidget::prepare()
{
    const QString directory = QCoreApplication::applicationDirPath();
    const QString source = directory + QStringLiteral("/updater/Update-Orchestrate.ps1");
    if (!QFile::copy(source, workspace_->filePath(QStringLiteral("Update-Orchestrate.ps1")))) {
        fail(QStringLiteral("更新器缺失，或无法复制更新器。请重新解压完整发布包。")); return;
    }
    const QJsonObject plan {{QStringLiteral("install_directory"), directory},
                            {QStringLiteral("workspace"), workspace_->path()},
                            {QStringLiteral("archive_sha256"), QString::fromLatin1(release_.digest)},
                            {QStringLiteral("version"), release_.version.toString()},
                            {QStringLiteral("process_id"), QCoreApplication::applicationPid()},
                            {QStringLiteral("mutex_name"), installationMutexName(directory)}};
    QFile file(workspace_->filePath(QStringLiteral("plan.json")));
    const QByteArray data = QJsonDocument(plan).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        fail(QStringLiteral("无法写入更新计划。")); return;
    }
    file.close();
    setState(State::Preparing, QStringLiteral("正在解压并校验更新文件…"));
    prepareProcess_ = new QProcess(this);
#ifdef Q_OS_WIN
    prepareProcess_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    prepareProcess_->setProgram(powershell());
    prepareProcess_->setArguments(scriptArguments(workspace_->path(), QStringLiteral("-Prepare")));
    connect(prepareProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) fail(QStringLiteral("无法启动 Windows PowerShell 更新器。"));
    });
    connect(prepareProcess_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
            QFile result(workspace_->filePath(QStringLiteral("result.json")));
            const QString message = result.open(QIODevice::ReadOnly)
                ? QJsonDocument::fromJson(result.readAll()).object().value(QStringLiteral("message")).toString() : QString();
            result.close();
            fail(message.isEmpty() ? QStringLiteral("准备更新失败，请确认磁盘空间和目录权限。") : message);
            return;
        }
        setState(State::Ready, QStringLiteral("%1 已准备就绪。").arg(release_.tag));
    });
    prepareProcess_->start();
}

bool UpdateWidget::startInstaller(QString *error)
{
    if (state_ != State::Ready || !workspace_) return false;
    QProcess installer;
    installer.setProgram(powershell());
    installer.setArguments(scriptArguments(workspace_->path(), QStringLiteral("-Apply")));
    installer.setWorkingDirectory(QFileInfo(QCoreApplication::applicationDirPath()).dir().absolutePath());
#ifdef Q_OS_WIN
    installer.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    if (!installer.startDetached()) {
        if (error) *error = QStringLiteral("无法启动更新器，程序尚未退出。");
        return false;
    }
    workspace_->setAutoRemove(false);
    return true;
}

void UpdateWidget::cancel()
{
    if (reply_) {
        auto *reply = reply_;
        reply_ = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    fail(QStringLiteral("已取消更新。"));
}

void UpdateWidget::fail(const QString &message)
{
    archive_.reset();
    hash_.reset();
    workspace_.reset();
    setState(State::Idle, message);
}
