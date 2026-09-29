#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>

namespace {
const QString entryName = QStringLiteral("Orchestrate");
QString commandFor(const QString &path)
{
    return QLatin1Char('"') + QDir::toNativeSeparators(QDir::cleanPath(path)) + QLatin1Char('"');
}
}

AutoStart::AutoStart() : command_(commandFor(QCoreApplication::applicationFilePath()))
{
#ifdef Q_OS_WIN
    // Existing offscreen UI suites must never read or alter real login settings.
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")
        || QGuiApplication::platformName() == QStringLiteral("minimal")) return;
    registration_ = std::make_unique<QSettings>(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
        QSettings::NativeFormat);
    approval_ = std::make_unique<QSettings>(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"),
        QSettings::NativeFormat);
#endif
}

AutoStart::AutoStart(std::unique_ptr<QSettings> registration, std::unique_ptr<QSettings> approval,
                     const QString &executablePath)
    : command_(commandFor(executablePath)), registration_(std::move(registration)), approval_(std::move(approval))
{
}

AutoStart::State AutoStart::state() const
{
    State result;
    if (!registration_) {
        result.error = QStringLiteral("当前环境不支持修改 Windows 开机启动设置。");
        return result;
    }
    // sync also imports changes made outside the application.
    QSettings registration(registration_->fileName(), registration_->format());
    registration.sync();
    const QString registered = registration.value(entryName).toString();
    if (registration.status() != QSettings::NoError) {
        result.error = QStringLiteral("无法读取开机启动设置，请检查当前用户的访问权限。");
        return result;
    }
    result.enabled = !registered.isEmpty();
    if (!result.enabled) {
        result.message = QStringLiteral("未开启。开启后将在当前用户登录 Windows 时自动打开 Orchestrate。");
        return result;
    }
    result.message = registered.compare(command_, Qt::CaseInsensitive) == 0
        ? QStringLiteral("已开启，当前用户登录 Windows 时将自动打开 Orchestrate。")
        : QStringLiteral("启动项指向其他位置；若移动过程序，请关闭后重新开启以更新路径。");
    if (approval_) {
        QSettings approvalStore(approval_->fileName(), approval_->format());
        approvalStore.sync();
        const QByteArray approval = approvalStore.value(entryName).toByteArray();
        // Windows keeps its user-disabled marker separate from the Run entry.
        // Observe known disabled states, but never overwrite Windows' approval data.
        result.disabledByWindows = !approval.isEmpty() && (approval.at(0) == 3 || approval.at(0) == 7);
        if (result.disabledByWindows) {
            result.message = QStringLiteral("启动项已登记，但已被 Windows 禁用。请在系统启动应用设置中启用 Orchestrate。");
            if (registered.compare(command_, Qt::CaseInsensitive) != 0)
                result.message += QStringLiteral("\n启动项还指向其他位置，请关闭后重新开启以更新路径。");
        }
        else if (approvalStore.status() != QSettings::NoError)
            result.message += QStringLiteral("\n无法读取 Windows 的启动许可状态，可在系统启动应用设置中核对。");
    }
    return result;
}

bool AutoStart::setEnabled(bool enabled, QString *error)
{
    if (error) error->clear();
    if (!registration_) {
        if (error) *error = QStringLiteral("当前环境不支持修改 Windows 开机启动设置。");
        return false;
    }
    QSettings registration(registration_->fileName(), registration_->format());
    if (enabled) registration.setValue(entryName, command_);
    else registration.remove(entryName);
    registration.sync();
    if (registration.status() == QSettings::NoError) return true;
    if (error) *error = QStringLiteral("保存开机启动设置失败，请检查当前用户的访问权限后重试。");
    return false;
}
