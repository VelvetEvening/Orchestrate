#include "instancelock.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

QString installationMutexName(const QString &directory)
{
    const QString path = QDir::cleanPath(QFileInfo(directory).absoluteFilePath()).toLower();
    return QStringLiteral("Local\\Orchestrate-")
        + QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
}

InstanceLock::InstanceLock(const QString &directory)
{
#ifdef Q_OS_WIN
    const auto name = installationMutexName(directory).toStdWString();
    handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
    if (handle_) {
        const DWORD result = WaitForSingleObject(static_cast<HANDLE>(handle_), 0);
        acquired_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    }
#else
    Q_UNUSED(directory);
    acquired_ = true;
#endif
}

InstanceLock::~InstanceLock()
{
#ifdef Q_OS_WIN
    if (acquired_) ReleaseMutex(static_cast<HANDLE>(handle_));
    if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
#endif
}
