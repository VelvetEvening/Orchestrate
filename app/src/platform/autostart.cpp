#include "autostart.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>

namespace {
const QString legacyEntryName = QStringLiteral("Orchestrate");
QString normalizedPath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(QDir::fromNativeSeparators(path)).absoluteFilePath()).toCaseFolded();
}

QString executableFromCommand(QString command)
{
    command = command.trimmed();
    if (command.startsWith(QLatin1Char('"')) && command.endsWith(QLatin1Char('"')))
        command = command.mid(1, command.size() - 2);
    return command;
}

QString commandFor(const QString &path)
{
    return QLatin1Char('"') + QDir::toNativeSeparators(QDir::cleanPath(path)) + QLatin1Char('"');
}

bool disabledMarker(const QByteArray &approval)
{
    return !approval.isEmpty() && (approval.at(0) == 3 || approval.at(0) == 7);
}
}

QString AutoStart::entryNameForExecutable(const QString &executablePath)
{
    // Use the logical directory, not its canonical target: updates replace it in place.
    const QString directory = QFileInfo(normalizedPath(executablePath)).absolutePath();
    const QByteArray identity = QCryptographicHash::hash(directory.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("Orchestrate-") + QString::fromLatin1(identity);
}

AutoStart::AutoStart()
    : command_(commandFor(QCoreApplication::applicationFilePath())),
      entryName_(entryNameForExecutable(QCoreApplication::applicationFilePath()))
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
    : command_(commandFor(executablePath)), entryName_(entryNameForExecutable(executablePath)),
      registration_(std::move(registration)), approval_(std::move(approval))
{
}

bool AutoStart::ownsLegacyEntry(const QSettings &registration) const
{
    const QString legacyCommand = registration.value(legacyEntryName).toString();
    return !legacyCommand.isEmpty()
        && normalizedPath(executableFromCommand(legacyCommand)) == normalizedPath(executableFromCommand(command_));
}

QString AutoStart::effectiveEntryName(const QSettings &registration) const
{
    // Keep an owned v1.1.0 entry in place, including Windows' existing disable marker.
    if (!registration.contains(entryName_) && ownsLegacyEntry(registration)) return legacyEntryName;
    return entryName_;
}

bool AutoStart::preserveLegacyApproval(QString *error) const
{
    if (!approval_) return true;
    QSettings approval(approval_->fileName(), approval_->format());
    approval.sync();
    const QByteArray legacy = approval.value(legacyEntryName).toByteArray();
    const QByteArray current = approval.value(entryName_).toByteArray();
    // A new registration must not bypass a disable decision made for the old one.
    if ((!approval.contains(entryName_) || (disabledMarker(legacy) && !disabledMarker(current)))
        && !legacy.isEmpty() && approval.status() == QSettings::NoError) {
        approval.setValue(entryName_, legacy);
        approval.sync();
    }
    if (approval.status() == QSettings::NoError) return true;
    if (error) *error = QStringLiteral("无法保留 Windows 的启动许可状态，未更改开机启动设置。");
    return false;
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
    const QString entryName = effectiveEntryName(registration);
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
        // Reading state never changes Windows' approval data.
        result.disabledByWindows = disabledMarker(approval);
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
    registration.sync();
    if (registration.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("无法读取开机启动设置，请检查当前用户的访问权限。");
        return false;
    }
    const bool ownedLegacy = ownsLegacyEntry(registration);
    if (ownedLegacy && !preserveLegacyApproval(error)) return false;
    if (enabled) registration.setValue(entryName_, command_);
    else registration.remove(entryName_);
    registration.sync();
    if (registration.status() == QSettings::NoError && ownedLegacy && ownsLegacyEntry(registration)) {
        registration.remove(legacyEntryName);
        registration.sync();
    }
    if (registration.status() == QSettings::NoError) return true;
    if (error) *error = QStringLiteral("保存开机启动设置失败，请检查当前用户的访问权限后重试。");
    return false;
}
