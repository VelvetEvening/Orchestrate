#include "manifestparser.h"
#include "commandparameters.h"
#include "sshconnection.h"
#include "toolconfiguration.h"
#include "wslprocess.h"
#include "platform/fileaccess.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace {
QString jsonString(const QJsonObject &object, const QString &key, const QString &fallback = QString())
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : fallback;
}

bool isAbsoluteRemotePath(const QString &path)
{
    return path.startsWith(QLatin1Char('/')) && !path.contains(QLatin1Char('\\'));
}

QString remoteDirectory(const QString &path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    if (slash < 0) {
        return QStringLiteral(".");
    }
    if (slash == 0) return QStringLiteral("/");
    return path.left(slash);
}

QString resolveRemotePath(const QString &baseDirectory, const QString &path)
{
    if (isAbsoluteRemotePath(path)) {
        return path;
    }
    if (baseDirectory.isEmpty()) {
        return path;
    }
    return QDir(baseDirectory).filePath(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
}

} // namespace

bool ManifestParser::read(const QString &sourcePath,
                          QByteArray *data,
                          QString *errorMessage)
{
    // Only the bundled manifests next to the executable use this bounded path.
    // User-registered locations always go through FileAccess.
    QFile file(sourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    *data = file.read(FileAccess::maxBytes + 1);
    if (data->size() > FileAccess::maxBytes) {
        if (errorMessage) *errorMessage = QStringLiteral("接入声明超过 1 MiB 上限。");
        return false;
    }
    return true;
}

bool ManifestParser::parse(const QByteArray &manifestData,
                           const QString &sourcePath,
                           const QString &sshHost,
                           Manifest *manifest,
                           QString *errorMessage,
                           const QString &wslDistribution,
                           const QString &wslUser,
                           const QString &sshUser)
{
    const auto fail = [errorMessage](const QString &message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(QStringLiteral("工具接入声明不是有效的 JSON：%1").arg(parseError.errorString()));
    }

    const QJsonObject object = document.object();
    const QString schema = jsonString(object, QStringLiteral("schema"));
    const QString externalId = jsonString(object, QStringLiteral("id"));
    const QString name = jsonString(object, QStringLiteral("name"));
    const QString targetType = jsonString(object, QStringLiteral("target_type"));
    const QString statePath = jsonString(object, QStringLiteral("state_path"));
    if (schema != QStringLiteral("orchestrate-tool/v1") || externalId.trimmed().isEmpty()
        || name.trimmed().isEmpty() || targetType.isEmpty()) {
        return fail(QStringLiteral("接入声明必须包含 schema=orchestrate-tool/v1，以及 id、name、target_type。"));
    }
    if (object.contains("state_path") && !object.value("state_path").isString()) return fail(QStringLiteral("state_path 必须是字符串，可留空。"));

    const bool remote = !sshHost.isEmpty();
    if ((remote && !SshConnection::validHost(sshHost)) || (!sshUser.isEmpty()
        && (!remote || !PosixRegistration::validUser(sshUser))))
        return fail(QStringLiteral("SSH 服务器与用户配置无效；用户名须单独填写。"));
    const bool wsl = !wslDistribution.isEmpty();
    if ((wsl && (remote || !WslProcess::validDistribution(wslDistribution) || !WslProcess::validName(wslUser)))
        || (!wsl && !wslUser.isEmpty())) {
        return fail(QStringLiteral("WSL 注册必须指定有效的发行版和 Linux 用户，且不能同时指定 SSH 主机。"));
    }
    const QString expectedTarget = wsl ? QStringLiteral("wsl")
        : (remote ? QStringLiteral("ssh") : QStringLiteral("windows-local"));
    if (targetType != expectedTarget) {
        return fail(QStringLiteral("此注册入口的 target_type 必须是 %1；请核对所选执行环境与工具配置。")
                        .arg(expectedTarget));
    }
    if (wsl && (!WslProcess::validPath(sourcePath) || (!statePath.isEmpty() && !WslProcess::validPath(statePath))
                || statePath.startsWith(QLatin1Char('~'))
                || (QDir::isAbsolutePath(statePath) && !statePath.startsWith(QLatin1Char('/'))))) {
        return fail(QStringLiteral("WSL 声明和状态须使用 Linux 路径；声明内不展开 ~ 或 Windows 路径。"));
    }
    if ((remote || wsl) && (!isAbsoluteRemotePath(sourcePath) || statePath.contains(QLatin1Char('\\')))) {
        return fail(QStringLiteral("Linux 路径须使用 / 分隔，接入声明路径必须是绝对路径。"));
    }
    if (object.contains(QStringLiteral("commands")) && !object.value(QStringLiteral("commands")).isArray()) {
        return fail(QStringLiteral("commands 必须是数组；只读监测工具请使用空数组 []。"));
    }

    const QDir manifestDirectory = QFileInfo(sourcePath).dir();
    QString workingDirectory = jsonString(object, QStringLiteral("working_directory")).trimmed();
    if (wsl) {
        const QString base = remoteDirectory(sourcePath);
        if (workingDirectory.isEmpty()) workingDirectory = base;
        if (!WslProcess::validPath(workingDirectory) || workingDirectory.startsWith(QLatin1Char('~'))
            || (QDir::isAbsolutePath(workingDirectory) && !workingDirectory.startsWith(QLatin1Char('/'))))
            return fail(QStringLiteral("WSL working_directory 必须是 Linux 绝对路径或相对于声明目录的路径，不能使用 ~。"));
        if (!isAbsoluteRemotePath(workingDirectory)) workingDirectory = resolveRemotePath(base, workingDirectory);
        workingDirectory = QDir::cleanPath(workingDirectory);
    } else if (remote) {
        if (workingDirectory.isEmpty()) {
            workingDirectory = remoteDirectory(sourcePath);
        }
        if (!isAbsoluteRemotePath(workingDirectory)) {
            return fail(QStringLiteral("服务器工具必须提供以 / 开头的 working_directory。"));
        }
    } else if (!workingDirectory.isEmpty() && QFileInfo(workingDirectory).isRelative()) {
        workingDirectory = QDir::cleanPath(manifestDirectory.filePath(workingDirectory));
    }

    AppDatabase::AutomationTool &tool = manifest->tool;
    tool.externalId = externalId;
    tool.name = name;
    tool.description = jsonString(object, QStringLiteral("description"));
    tool.targetType = targetType;
    tool.registrationPath = sourcePath;
    tool.sshHost = sshHost;
    tool.sshUser = sshUser;
    tool.wslDistribution = wslDistribution;
    tool.wslUser = wslUser;
    tool.workingDirectory = workingDirectory;
    tool.statePath = wsl ? QDir::cleanPath(resolveRemotePath(remoteDirectory(sourcePath), statePath)) : remote
        ? resolveRemotePath(workingDirectory, statePath)
        : (QFileInfo(statePath).isAbsolute() ? statePath : manifestDirectory.filePath(statePath));
    if (statePath.trimmed().isEmpty()) tool.statePath.clear();
    if (object.contains("environment") && !object.value("environment").isObject()) return fail(QStringLiteral("environment 必须是对象。"));
    tool.environment = object.value("environment").toObject();
    if (!ToolConfiguration::validEnvironment(tool.environment, errorMessage)) return false;
    manifest->group = jsonString(object, QStringLiteral("group")).trimmed();

    manifest->commands.clear();
    for (const QJsonValue &value : object.value(QStringLiteral("commands")).toArray()) {
        if (!value.isObject()) {
            return fail(QStringLiteral("commands 中的每一项都必须是命令对象。"));
        }
        const QJsonObject commandObject = value.toObject();
        AppDatabase::ToolCommand command;
        command.name = jsonString(commandObject, QStringLiteral("name"));
        command.description = jsonString(commandObject, QStringLiteral("description"));
        command.executable = jsonString(commandObject, QStringLiteral("executable"));
        for (const auto &key : {QStringLiteral("arguments"), QStringLiteral("parameters")}) {
            if (commandObject.contains(key) && !commandObject.value(key).isArray()) {
                return fail(QStringLiteral("命令“%1”的 %2 必须是数组。").arg(command.name, key));
            }
        }
        for (const auto &key : {QStringLiteral("interactive"), QStringLiteral("high_risk"), QStringLiteral("enabled")}) {
            if (commandObject.contains(key) && !commandObject.value(key).isBool()) {
                return fail(QStringLiteral("命令“%1”的 %2 必须是 JSON 布尔值。").arg(command.name, key));
            }
        }
        for (const QJsonValue &argument : commandObject.value(QStringLiteral("arguments")).toArray()) {
            if (!argument.isString()) return fail(QStringLiteral("命令“%1”的 arguments 只能包含字符串。").arg(command.name));
            command.arguments.append(argument.toString());
        }
        command.parameters = commandObject.value(QStringLiteral("parameters")).toArray();
        command.interactive = commandObject.value(QStringLiteral("interactive")).toBool(false);
        command.highRisk = commandObject.value(QStringLiteral("high_risk")).toBool(false);
        command.enabled = commandObject.value(QStringLiteral("enabled")).toBool(true);
        if (command.name.trimmed().isEmpty() || command.executable.trimmed().isEmpty()) {
            return fail(QStringLiteral("每条命令都必须提供非空 name 和 executable。"));
        }
        if (wsl) {
            if (!WslProcess::validPath(command.executable) || command.executable.startsWith(QLatin1Char('~'))
                || (QDir::isAbsolutePath(command.executable) && !command.executable.startsWith(QLatin1Char('/'))))
                return fail(QStringLiteral("WSL executable 必须是 Linux 程序名或路径，不能使用 ~。"));
            for (const auto &argument : command.arguments)
                if (argument.contains(QChar::Null)) return fail(QStringLiteral("WSL 参数不能包含 NUL。"));
        }
        if (!CommandParameters::validate(command.name, command.parameters, command.arguments, errorMessage)) {
            return false;
        }
        manifest->commands.append(command);
    }
    return true;
}

bool ManifestParser::applyUpdate(AppDatabase &database, int toolId, Manifest manifest, QString *errorMessage)
{
    manifest.tool.id = toolId;
    return database.saveAutomationTool(manifest.tool, manifest.commands, nullptr, errorMessage);
}
