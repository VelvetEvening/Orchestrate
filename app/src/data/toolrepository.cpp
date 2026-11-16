#include "toolrepository.h"
#include "repositoryhelpers.h"
#include <QSqlDatabase>
#include <QSet>

using namespace RepositoryHelpers;

namespace {
QString toolOrderKey(int groupId) { return QStringLiteral("order.tools.%1").arg(groupId); }
} // namespace

QList<AppDatabase::ToolGroup> ToolRepository::toolGroups(QString *errorMessage, bool includeUngrouped) const
{
    QList<ToolGroup> result;
    if (!isOpen()) {
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    if (!query.exec(QStringLiteral("SELECT id, name, sort_order FROM tool_groups ORDER BY sort_order, name"))) {
        setError(errorMessage, query);
        return result;
    }
    while (query.next()) {
        ToolGroup group;
        group.id = query.value(0).toInt();
        group.name = query.value(1).toString();
        group.sortOrder = query.value(2).toInt();
        result.append(group);
    }
    if (includeUngrouped) result.append(ToolGroup{});
    applySiblingOrder(result, setting(QStringLiteral("order.tool_groups")), [](const auto &) { return true; });
    return result;
}

bool ToolRepository::addToolGroup(const QString &name, int *groupId, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("INSERT INTO tool_groups(name, sort_order) VALUES (?, COALESCE((SELECT MAX(sort_order) + 1 FROM tool_groups), 0))"));
    query.addBindValue(name);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    if (groupId != nullptr) {
        *groupId = query.lastInsertId().toInt();
    }
    return true;
}

bool ToolRepository::renameToolGroup(int id, const QString &name, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("UPDATE tool_groups SET name = ? WHERE id = ?"));
    query.addBindValue(name);
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool ToolRepository::deleteToolGroup(int id, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("DELETE FROM tool_groups WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

QList<AppDatabase::AutomationTool> ToolRepository::automationTools(int groupId, QString *errorMessage) const
{
    QList<AutomationTool> result;
    if (!isOpen()) {
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    if (groupId > 0) {
        query.prepare(QStringLiteral(
            "SELECT id, external_id, COALESCE(group_id, 0), name, description, target_type, registration_path, state_path, "
            "ssh_host, working_directory, refresh_enabled, refresh_mode, refresh_interval_seconds, daily_refresh_time, builtin, wsl_distribution, wsl_user, ssh_user, customized, environment "
            "FROM automation_tools WHERE group_id = ? ORDER BY name"));
        query.addBindValue(groupId);
    } else {
        query.prepare(QStringLiteral(
            "SELECT id, external_id, COALESCE(group_id, 0), name, description, target_type, registration_path, state_path, "
            "ssh_host, working_directory, refresh_enabled, refresh_mode, refresh_interval_seconds, daily_refresh_time, builtin, wsl_distribution, wsl_user, ssh_user, customized, environment "
            "FROM automation_tools ORDER BY name"));
    }
    if (!query.exec()) {
        setError(errorMessage, query);
        return result;
    }
    while (query.next()) {
        AutomationTool tool;
        tool.id = query.value(0).toInt();
        tool.externalId = query.value(1).toString();
        tool.groupId = query.value(2).toInt();
        tool.name = query.value(3).toString();
        tool.description = query.value(4).toString();
        tool.targetType = query.value(5).toString();
        tool.registrationPath = query.value(6).toString();
        tool.statePath = query.value(7).toString();
        tool.sshHost = query.value(8).toString();
        tool.workingDirectory = query.value(9).toString();
        tool.refreshEnabled = query.value(10).toInt() != 0;
        tool.refreshMode = query.value(11).toString();
        if (tool.refreshMode.isEmpty()) {
            tool.refreshMode = QStringLiteral("interval");
        }
        tool.refreshIntervalSeconds = qMax(60, query.value(12).toInt());
        tool.dailyRefreshTime = query.value(13).toString();
        if (tool.dailyRefreshTime.isEmpty()) {
            tool.dailyRefreshTime = QStringLiteral("08:00");
        }
        tool.builtin = query.value(14).toInt() != 0;
        tool.wslDistribution = query.value(15).toString();
        tool.wslUser = query.value(16).toString();
        tool.sshUser = query.value(17).toString();
        tool.customized = query.value(18).toBool();
        tool.environment = QJsonDocument::fromJson(query.value(19).toString().toUtf8()).object();
        result.append(tool);
    }
    QSet<int> groups;
    for (const auto &tool : result) groups.insert(tool.groupId);
    for (int id : groups)
        applySiblingOrder(result, setting(toolOrderKey(id)), [id](const auto &tool) { return tool.groupId == id; });
    return result;
}

bool ToolRepository::addAutomationTool(const AutomationTool &tool,
                                    int *toolId,
                                    QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "INSERT INTO automation_tools(external_id, group_id, name, description, target_type, registration_path, state_path, "
        "ssh_host, working_directory, refresh_enabled, refresh_mode, refresh_interval_seconds, daily_refresh_time, builtin, wsl_distribution, wsl_user, ssh_user, customized, environment) "
        "VALUES (?, NULLIF(?, 0), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(text(tool.externalId));
    query.addBindValue(tool.groupId);
    query.addBindValue(text(tool.name));
    query.addBindValue(text(tool.description));
    query.addBindValue(text(tool.targetType));
    query.addBindValue(text(tool.registrationPath));
    query.addBindValue(text(tool.statePath));
    query.addBindValue(text(tool.sshHost));
    query.addBindValue(text(tool.workingDirectory));
    query.addBindValue(tool.refreshEnabled ? 1 : 0);
    query.addBindValue(tool.refreshMode.isEmpty() ? QStringLiteral("interval") : tool.refreshMode);
    query.addBindValue(qMax(60, tool.refreshIntervalSeconds));
    query.addBindValue(tool.dailyRefreshTime.isEmpty() ? QStringLiteral("08:00") : tool.dailyRefreshTime);
    query.addBindValue(tool.builtin ? 1 : 0);
    query.addBindValue(text(tool.wslDistribution));
    query.addBindValue(text(tool.wslUser));
    query.addBindValue(text(tool.sshUser));
    query.addBindValue(tool.customized ? 1 : 0);
    query.addBindValue(QString::fromUtf8(QJsonDocument(tool.environment).toJson(QJsonDocument::Compact)));
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    if (toolId != nullptr) {
        *toolId = query.lastInsertId().toInt();
    }
    return true;
}

bool ToolRepository::updateAutomationTool(const AutomationTool &tool, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    // Built-in ownership is app-managed, not part of the manifest. Startup sync
    // may promote a registration, but reload/re-import must never demote one.
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "UPDATE automation_tools SET external_id = ?, name = ?, description = ?, target_type = ?, "
        "registration_path = ?, state_path = ?, ssh_host = ?, working_directory = ?, "
        "builtin = MAX(builtin, ?), wsl_distribution = ?, wsl_user = ?, ssh_user = ?, customized = ?, environment = ? WHERE id = ?"));
    query.addBindValue(text(tool.externalId));
    query.addBindValue(text(tool.name));
    query.addBindValue(text(tool.description));
    query.addBindValue(text(tool.targetType));
    query.addBindValue(text(tool.registrationPath));
    query.addBindValue(text(tool.statePath));
    query.addBindValue(text(tool.sshHost));
    query.addBindValue(text(tool.workingDirectory));
    query.addBindValue(tool.builtin ? 1 : 0);
    query.addBindValue(text(tool.wslDistribution));
    query.addBindValue(text(tool.wslUser));
    query.addBindValue(text(tool.sshUser));
    query.addBindValue(tool.customized ? 1 : 0);
    query.addBindValue(QString::fromUtf8(QJsonDocument(tool.environment).toJson(QJsonDocument::Compact)));
    query.addBindValue(tool.id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool ToolRepository::setToolGroup(int toolId, int groupId, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("UPDATE automation_tools SET group_id = NULLIF(?, 0) WHERE id = ?"));
    query.addBindValue(groupId);
    query.addBindValue(toolId);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool ToolRepository::deleteAutomationTool(int id, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("DELETE FROM automation_tools WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}


bool ToolRepository::updateToolRefreshSettings(int id,
                                               bool enabled,
                                               const QString &mode,
                                               int intervalSeconds,
                                               const QString &dailyTime,
                                               QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "UPDATE automation_tools SET refresh_enabled = ?, refresh_mode = ?, "
        "refresh_interval_seconds = ?, daily_refresh_time = ? WHERE id = ?"));
    query.addBindValue(enabled ? 1 : 0);
    query.addBindValue(mode.isEmpty() ? QStringLiteral("interval") : mode);
    query.addBindValue(qMax(60, intervalSeconds));
    query.addBindValue(dailyTime.isEmpty() ? QStringLiteral("08:00") : dailyTime);
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}


QList<AppDatabase::ToolCommand> ToolRepository::toolCommands(int toolId, QString *errorMessage) const
{
    QList<ToolCommand> result;
    if (!isOpen()) {
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "SELECT id, tool_id, name, description, executable, arguments, parameters, interactive, high_risk, arguments_json, enabled "
        "FROM tool_commands WHERE tool_id = ? ORDER BY position, id"));
    query.addBindValue(toolId);
    if (!query.exec()) {
        setError(errorMessage, query);
        return result;
    }
    while (query.next()) {
        ToolCommand command;
        command.id = query.value(0).toInt();
        command.toolId = query.value(1).toInt();
        command.name = query.value(2).toString();
        command.description = query.value(3).toString();
        command.executable = query.value(4).toString();
        command.arguments = query.value(5).toString().split(QChar('\x1f'), Qt::SkipEmptyParts);
        // New registrations preserve empty strings and separator characters. Old
        // rows retain the legacy representation until their manifest is reloaded.
        const auto argumentsJson = QJsonDocument::fromJson(query.value(9).toString().toUtf8());
        if (argumentsJson.isArray()) {
            command.arguments.clear();
            for (const auto &argument : argumentsJson.array()) command.arguments.append(argument.toString());
        }
        command.parameters = QJsonDocument::fromJson(query.value(6).toString().toUtf8()).array();
        command.interactive = query.value(7).toInt() != 0;
        command.highRisk = query.value(8).toInt() != 0;
        command.enabled = query.value(10).toBool();
        result.append(command);
    }
    return result;
}

bool ToolRepository::addToolCommand(const ToolCommand &command, int *commandId, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "INSERT INTO tool_commands(tool_id, name, description, executable, arguments, parameters, "
        "interactive, high_risk, arguments_json, enabled, id, position) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULLIF(?,0), "
        "(SELECT COALESCE(MAX(position),-1)+1 FROM tool_commands WHERE tool_id = ?))"));
    query.addBindValue(command.toolId);
    query.addBindValue(text(command.name));
    query.addBindValue(text(command.description));
    query.addBindValue(text(command.executable));
    query.addBindValue(text(command.arguments.join(QChar('\x1f'))));
    query.addBindValue(command.parameters.isEmpty()
                           ? QStringLiteral("")
                           : QString::fromUtf8(QJsonDocument(command.parameters).toJson(QJsonDocument::Compact)));
    query.addBindValue(command.interactive ? 1 : 0);
    query.addBindValue(command.highRisk ? 1 : 0);
    query.addBindValue(QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(command.arguments)).toJson(QJsonDocument::Compact)));
    query.addBindValue(command.enabled ? 1 : 0);
    query.addBindValue(command.id);
    query.addBindValue(command.toolId);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    if (commandId != nullptr) {
        *commandId = query.lastInsertId().toInt();
    }
    return true;
}

bool ToolRepository::replaceToolCommands(int toolId, const QList<ToolCommand> &commands, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlDatabase database = QSqlDatabase::database(connectionName_);
    if (!database.transaction()) {
        return fail(errorMessage, database.lastError().text());
    }
    if (!replaceToolCommandsInTransaction(toolId, commands, errorMessage)) {
        database.rollback();
        return false;
    }
    if (!database.commit()) {
        const QString error = database.lastError().text();
        database.rollback();
        return fail(errorMessage, error);
    }
    return true;
}

bool ToolRepository::saveAutomationTool(const AutomationTool &tool, const QList<ToolCommand> &commands,
                                     int *toolId, QString *errorMessage)
{
    if (!isOpen()) return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    auto database = QSqlDatabase::database(connectionName_);
    if (!database.transaction()) return fail(errorMessage, database.lastError().text());
    int id = tool.id;
    if (id > 0) {
        QSqlQuery exists(database);
        exists.prepare(QStringLiteral("SELECT id FROM automation_tools WHERE id = ?"));
        exists.addBindValue(id);
        if (!exists.exec() || !exists.next()) {
            const QString error = exists.lastError().isValid() ? exists.lastError().text()
                                                              : QStringLiteral("工具注册已不存在。");
            database.rollback();
            return fail(errorMessage, error);
        }
    }
    const bool saved = id > 0 ? updateAutomationTool(tool, errorMessage)
                              : addAutomationTool(tool, &id, errorMessage);
    if (!saved || !replaceToolCommandsInTransaction(id, commands, errorMessage)) {
        database.rollback();
        return false;
    }
    if (!database.commit()) {
        const QString error = database.lastError().text();
        database.rollback();
        return fail(errorMessage, error);
    }
    if (toolId) *toolId = id;
    return true;
}

bool ToolRepository::replaceToolCommandsInTransaction(int toolId, const QList<ToolCommand> &commands,
                                                   QString *errorMessage)
{
    auto database = QSqlDatabase::database(connectionName_);
    QSqlQuery clear(database);
    clear.prepare(QStringLiteral("DELETE FROM tool_commands WHERE tool_id = ?"));
    clear.addBindValue(toolId);
    if (!clear.exec()) {
        setError(errorMessage, clear);
        return false;
    }
    for (ToolCommand command : commands) {
        command.toolId = toolId;
        if (!addToolCommand(command, nullptr, errorMessage)) {
            return false;
        }
    }
    return true;
}

bool ToolRepository::reorderToolGroups(const QList<int> &ids, QString *errorMessage)
{
    return saveSiblingOrder(QStringLiteral("order.tool_groups"), QStringLiteral("SELECT id FROM tool_groups UNION ALL SELECT 0"), {}, ids, errorMessage);
}

bool ToolRepository::reorderTools(int groupId, const QList<int> &ids, QString *errorMessage)
{
    return saveSiblingOrder(toolOrderKey(groupId), QStringLiteral("SELECT id FROM automation_tools WHERE COALESCE(group_id, 0) = ?"),
                            {groupId}, ids, errorMessage);
}
