#include "mathpreview.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

MathPreview::MathPreview(QObject *parent) : QObject(parent)
{
    debounce_.setSingleShot(true);
    debounce_.setInterval(80);
    timeout_.setSingleShot(true);
    timeout_.setInterval(5000);
    connect(&debounce_, &QTimer::timeout, this, &MathPreview::start);
    connect(&timeout_, &QTimer::timeout, this, [this] { if (process_) process_->kill(); });
}
MathPreview::~MathPreview() { cancel(); }

void MathPreview::cancel()
{
    debounce_.stop(); timeout_.stop();
    if (process_) {
        process_->disconnect(this);
        auto *old = process_;
        process_ = nullptr;
        old->setParent(QCoreApplication::instance());
        connect(old, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), old, &QObject::deleteLater);
        connect(old, &QProcess::errorOccurred, old, [old](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) old->deleteLater();
        });
        if (old->state() == QProcess::NotRunning) old->deleteLater(); else old->kill();
    }
#ifdef Q_OS_WIN
    if (job_) CloseHandle(static_cast<HANDLE>(job_));
#endif
    job_ = nullptr;
    output_.clear(); request_.clear(); keys_.clear(); images_.clear();
    imageBytes_ = receivedBytes_ = 0;
}

void MathPreview::request(const QString &source, qreal pixels)
{
    cancel();
    QJsonArray formulas;
    for (const auto &span : MathText::spans(source)) {
        const auto key = source.mid(span.start, span.length);
        if (keys_.contains(key)) continue;
        keys_.append(key);
        formulas.append(QJsonObject{{"code", span.code}, {"display", span.display}});
    }
    if (formulas.isEmpty()) return;
    request_ = QJsonDocument(QJsonObject{{"formulas", formulas}, {"size", qBound(6.0, pixels, 192.0)}}).toJson(QJsonDocument::Compact);
    debounce_.start();
}

void MathPreview::start()
{
    QString worker = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("OrchestrateMath.exe"));
    if (!QFileInfo::exists(worker)) { emit ready(); return; }
    process_ = new QProcess(this);
#ifdef Q_OS_WIN
    process_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS;
    });
#endif
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("windows"));
    process_->setProcessEnvironment(environment);
    process_->setStandardErrorFile(QProcess::nullDevice());
    connect(process_, &QProcess::started, this, [this] {
#ifdef Q_OS_WIN
        // Input is sent only after the child is constrained. A malformed formula
        // cannot grow the renderer indefinitely or survive its owning preview.
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        limits.ProcessMemoryLimit = 192 * 1024 * 1024;
        HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(process_->processId()));
        const bool constrained = job && child
            && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))
            && AssignProcessToJobObject(job, child);
        if (child) CloseHandle(child);
        if (!constrained) { if (job) CloseHandle(job); process_->kill(); return; }
        job_ = job;
#endif
        process_->write(request_);
        process_->closeWriteChannel();
    });
    connect(process_, &QProcess::readyReadStandardOutput, this, &MathPreview::read);
    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &MathPreview::finish);
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish();
    });
    timeout_.start();
    process_->start(worker, {});
}

void MathPreview::read()
{
    const auto bytes = process_->readAllStandardOutput();
    receivedBytes_ += bytes.size();
    if (receivedBytes_ > 16 * 1024 * 1024) { process_->kill(); return; }
    output_ += bytes;
    qsizetype newline;
    while ((newline = output_.indexOf('\n')) >= 0) {
        const auto result = QJsonDocument::fromJson(output_.left(newline)).object();
        output_.remove(0, newline + 1);
        const int index = result.value("index").toInt(-1);
        if (index < 0 || index >= keys_.size() || result.value("error").toBool()) continue;
        const auto image = QImage::fromData(QByteArray::fromBase64(result.value("png").toString().toLatin1()), "PNG");
        if (image.isNull() || imageBytes_ + image.sizeInBytes() > 16 * 1024 * 1024) continue;
        imageBytes_ += image.sizeInBytes();
        images_.insert(keys_.at(index), image);
    }
}

void MathPreview::finish()
{
    if (!process_) return;
    timeout_.stop();
    read();
    process_->deleteLater(); process_ = nullptr;
#ifdef Q_OS_WIN
    if (job_) CloseHandle(static_cast<HANDLE>(job_));
#endif
    job_ = nullptr;
    output_.clear(); request_.clear();
    emit ready();
}
