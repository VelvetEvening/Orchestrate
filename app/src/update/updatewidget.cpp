#include "updatewidget.h"
#include "updatechecker.h"
#include "platform/instancelock.h"

#include <QCoreApplication>
#include <QCheckBox>
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
#include <QSignalBlocker>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
class ReleaseNotesBrowser final : public QTextBrowser {
public:
    using QTextBrowser::QTextBrowser;
protected:
    // Release Markdown is remote content. Display text without fetching images
    // or reading files referenced by the release author.
    QVariant loadResource(int, const QUrl &) override { return {}; }
};

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
    if (!QFileInfo::exists(script)) return;
    QFile resultFile(install + QStringLiteral("/data/update-result.json"));
    QJsonObject result;
    if (resultFile.open(QIODevice::ReadOnly))
        result = QJsonDocument::fromJson(resultFile.read(64 * 1024)).object();
    const bool backupPending = result.value(QStringLiteral("success")).toBool()
        && result.value(QStringLiteral("tasks_restored")).toBool()
        && !result.value(QStringLiteral("backup_directory")).toString().isEmpty()
        && !result.value(QStringLiteral("backup_removed")).toBool();
    if (!backupPending && !result.value(QStringLiteral("cleanup_pending")).toBool()
        && parent.entryList({QStringLiteral(".Orchestrate-update-*")}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty()) return;
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

UpdateWidget::UpdateWidget(QWidget *parent, UpdateChecker *checker) : QWidget(parent),
    checker_(checker ? checker : new UpdateChecker(QString(), nullptr, this))
{
    setObjectName(QStringLiteral("updateWidget"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(12);
    auto *title = new QLabel(QStringLiteral("版本与更新"), this);
    title->setObjectName(QStringLiteral("cardTitle"));
    layout->addWidget(title);
    auto *version = new QLabel(QStringLiteral("Orchestrate %1").arg(QCoreApplication::applicationVersion()), this);
    layout->addWidget(version);
    automaticCheck_ = new QCheckBox(QStringLiteral("自动检查更新（每天一次）"), this);
    automaticCheck_->setObjectName(QStringLiteral("automaticUpdateCheck"));
    automaticCheck_->setChecked(checker_->automaticEnabled());
    automaticCheck_->setToolTip(QStringLiteral("启动三分钟后检查；只提醒新版本，下载和安装由你确认。"));
    layout->addWidget(automaticCheck_);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("updateStatus"));
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    resultStatus_ = new QLabel(this);
    resultStatus_->setObjectName(QStringLiteral("previousUpdateResult"));
    resultStatus_->setTextFormat(Qt::PlainText);
    resultStatus_->setWordWrap(true);
    resultStatus_->hide();
    layout->addWidget(resultStatus_);
    notes_ = new ReleaseNotesBrowser(this);
    notes_->setObjectName(QStringLiteral("releaseNotes"));
    notes_->setAccessibleName(QStringLiteral("更新说明"));
    notes_->setOpenLinks(false);
    notes_->setOpenExternalLinks(false);
    notes_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    notes_->setMinimumHeight(180);
    notes_->setMaximumHeight(280);
    notes_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(notes_, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
        if (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http"))
            QDesktopServices::openUrl(url);
    });
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
    releasePage->setObjectName(QStringLiteral("releasePageButton"));
    buttons->addWidget(check_);
    buttons->addWidget(action_);
    buttons->addWidget(cancel_);
    buttons->addWidget(releasePage);
    buttons->addStretch();
    layout->addLayout(buttons);
    connect(releasePage, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(release_.pageUrl.isEmpty()
            ? QUrl(QStringLiteral("https://github.com/VelvetEvening/Orchestrate/releases")) : release_.pageUrl);
    });
    connect(check_, &QPushButton::clicked, this, &UpdateWidget::check);
    connect(cancel_, &QPushButton::clicked, this, &UpdateWidget::cancel);
    connect(action_, &QPushButton::clicked, this, [this] {
        if (state_ == State::Available) download();
        else if (state_ == State::Ready) emit installRequested();
    });
    connect(automaticCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
        QString error;
        if (!checker_->setAutomaticEnabled(enabled, &error)) {
            const QSignalBlocker blocker(automaticCheck_);
            automaticCheck_->setChecked(checker_->automaticEnabled());
            status_->setText(error);
        }
    });
    connect(checker_, &UpdateChecker::checkStarted, this, [this](bool) {
        statusBeforeCheck_ = status_->text();
        setState(State::Checking, QStringLiteral("正在检查 GitHub Release…"));
    });
    connect(checker_, &UpdateChecker::checkFinished, this, [this](bool automatic, const QString &error) {
        if (!error.isEmpty()) {
            setState(checker_->hasUpdate() ? State::Available : State::Idle,
                automatic ? statusBeforeCheck_ : error);
            return;
        }
        showRelease();
    });
    connect(checker_, &UpdateChecker::releaseChanged, this, [this] {
        emit updateAvailableChanged(checker_->hasUpdate() ? checker_->release().tag : QString());
    });
    connect(checker_, &UpdateChecker::notificationRequested, this, &UpdateWidget::updateNotificationRequested);
    setState(State::Idle, QStringLiteral("尚未检查更新。"));
    if (checker_->hasUpdate()) showRelease();
    refreshResult();
}

void UpdateWidget::startAutomaticChecks()
{
    checker_->start();
}

void UpdateWidget::refreshResult()
{
    QFile file(QCoreApplication::applicationDirPath() + QStringLiteral("/data/update-result.json"));
    if (!file.open(QIODevice::ReadOnly)) return;
    const QByteArray data = file.read(64 * 1024);
    if (data == lastResult_) return;
    lastResult_ = data;
    const QJsonObject result = QJsonDocument::fromJson(data).object();
    QString message = result.value(QStringLiteral("message")).toString();
    const QString cleanupError = result.value(QStringLiteral("cleanup_error")).toString();
    if (!cleanupError.isEmpty()) message += QStringLiteral("\n") + cleanupError;
    resultStatus_->setText(message.isEmpty() ? QString() : QStringLiteral("上次更新：") + message);
    resultStatus_->setVisible(!message.isEmpty());
}

void UpdateWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshNotes();
    refreshResult();
}

UpdateWidget::~UpdateWidget()
{
    disconnect(checker_, nullptr, this, nullptr);
    checker_->cancel();
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
    checker_->setPaused(state == State::Downloading || state == State::Preparing || state == State::Ready);
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
    if (!network_) network_ = new QNetworkAccessManager(this);
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
    if (state_ == State::Checking || state_ == State::Downloading || state_ == State::Preparing) return;
    workspace_.reset();
    checker_->setPaused(false);
    checker_->checkNow();
}

void UpdateWidget::showRelease()
{
    release_ = checker_->release();
    notes_->setVisible(checker_->hasUpdate());
    auto *page = findChild<QPushButton *>(QStringLiteral("releasePageButton"));
    page->setText(checker_->hasUpdate() ? QStringLiteral("查看该版本发布页") : QStringLiteral("发布记录"));
    refreshNotes();
    if (checker_->hasUpdate())
        setState(State::Available, QStringLiteral("发现新版本 %1（%2 MB）。").arg(release_.tag).arg(release_.archiveBytes / (1024.0 * 1024.0), 0, 'f', 1));
    else
        setState(State::Idle, QStringLiteral("当前已是最新版本（最新正式版：%1）。").arg(release_.tag));
}

void UpdateWidget::refreshNotes()
{
    if (!isVisible() || notes_->isHidden()) return;
    const auto content = release_.notes.trimmed().isEmpty() ? QStringLiteral("此版本未提供更新说明。") : release_.notes;
    if (content == displayedNotes_) return;
    displayedNotes_ = content;
    notes_->document()->setMarkdown(content,
        QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub) | QTextDocument::MarkdownNoHTML);
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
    if (checker_->checking()) { checker_->cancel(); return; }
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
