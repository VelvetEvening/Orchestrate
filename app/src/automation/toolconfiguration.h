#pragma once
#include "data/appdatabase.h"
#include "commandline.h"
#include "commandparameters.h"
#include <QJsonDocument>

namespace ToolConfiguration {
inline QJsonObject command(const AppDatabase::ToolCommand &c)
{
    return {{"name",c.name},{"description",c.description},{"executable",c.executable},
        {"arguments",QJsonArray::fromStringList(c.arguments)},{"parameters",c.parameters},
        {"interactive",c.interactive},{"high_risk",c.highRisk},{"enabled",c.enabled}};
}
inline QJsonObject tool(const AppDatabase::AutomationTool &t)
{
    return {{"id",t.externalId},{"name",t.name},{"description",t.description},{"target_type",t.targetType},
        {"registration_path",t.registrationPath},{"state_path",t.statePath},{"working_directory",t.workingDirectory},
        {"ssh_host",t.sshHost},{"ssh_user",t.sshUser},{"wsl_distribution",t.wslDistribution},
        {"wsl_user",t.wslUser},{"environment",t.environment}};
}
inline QString snapshot(const AppDatabase::AutomationTool &t, const QList<AppDatabase::ToolCommand> &commands)
{
    auto object = tool(t); QJsonArray array;
    for (const auto &c : commands) array.append(command(c));
    object["commands"] = array;
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
}
inline QString description(const AppDatabase::AutomationTool &t, const QList<AppDatabase::ToolCommand> &commands)
{
    QStringList lines {QStringLiteral("工具：%1").arg(t.name), t.description,
        QStringLiteral("执行环境：%1").arg(t.targetType),
        QStringLiteral("工作目录：%1").arg(t.workingDirectory.isEmpty() ? QStringLiteral("默认目录") : t.workingDirectory),
        QStringLiteral("状态文件：%1").arg(t.statePath.isEmpty() ? QStringLiteral("未配置") : t.statePath)};
    if (t.targetType == "ssh") lines << QStringLiteral("SSH：%1  用户：%2").arg(t.sshHost, t.sshUser);
    if (t.targetType == "wsl") lines << QStringLiteral("WSL：%1  用户：%2").arg(t.wslDistribution, t.wslUser);
    for (auto it = t.environment.begin(); it != t.environment.end(); ++it) lines << QStringLiteral("环境变量：%1=%2").arg(it.key(), it.value().toString());
    lines << QString() << QStringLiteral("命令（按运行列表顺序，共 %1 条）").arg(commands.size());
    for (const auto &c : commands) {
        lines << QString() << QStringLiteral("%1 · %2 · %3%4").arg(c.name,
            c.enabled ? QStringLiteral("启用") : QStringLiteral("停用"),
            c.interactive ? QStringLiteral("外部终端") : QStringLiteral("应用内输出"),
            c.highRisk ? QStringLiteral(" · 运行前确认") : QString());
        if (!c.description.isEmpty()) lines << c.description;
        lines << CommandLine::join(QStringList{c.executable} + c.arguments);
        if (c.parameters.isEmpty()) lines << QStringLiteral("运行参数：无，直接运行");
        for (const auto &value : c.parameters) {
            const auto p = value.toObject();
            lines << QStringLiteral("参数：%1 {%2} · %3 · %4 · 默认：%5").arg(
                p.value("label").toString(p.value("key").toString()), p.value("key").toString(), p.value("type").toString(),
                p.value("required").toBool() ? QStringLiteral("必填") : QStringLiteral("可选"),
                CommandParameters::scalarText(p.value("default")));
            // Include constraints and source mappings so replacement is reviewable.
            lines << QString::fromUtf8(QJsonDocument(p).toJson(QJsonDocument::Compact));
        }
    }
    return lines.join('\n');
}
}
