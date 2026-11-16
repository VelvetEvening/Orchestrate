#include "projectrepository.h"
#include "repositoryhelpers.h"
#include <QSqlDatabase>
#include <QSet>

using namespace RepositoryHelpers;

namespace {
QString folderOrderKey(int projectId, bool starred) { return QStringLiteral("order.folders.%1.%2").arg(projectId).arg(starred ? 1 : 0); }
QString recordOrderKey(int projectId, int folderId) { return QStringLiteral("order.records.%1.%2").arg(projectId).arg(folderId); }
} // namespace

QList<AppDatabase::Project> ProjectRepository::projects(QString *errorMessage) const
{
    QList<Project> result;
    if (!isOpen()) {
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    if (!query.exec(QStringLiteral(
            "SELECT id, name, directory, target_type, created_at, updated_at, completed, target_name, target_user "
            "FROM projects ORDER BY updated_at DESC, id DESC"))) {
        setError(errorMessage, query);
        return result;
    }
    while (query.next()) {
        Project project;
        project.id = query.value(0).toInt();
        project.name = query.value(1).toString();
        project.directory = query.value(2).toString();
        project.targetType = query.value(3).toString();
        project.targetName = query.value(7).toString();
        project.targetUser = query.value(8).toString();
        project.createdAt = QDateTime::fromString(query.value(4).toString(), Qt::ISODateWithMs);
        project.updatedAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODateWithMs);
        project.completed = query.value(6).toBool();
        result.append(project);
    }
    applySiblingOrder(result, setting(QStringLiteral("order.projects")), [](const auto &) { return true; });
    return result;
}

bool ProjectRepository::addProject(const QString &name,
                             const QString &directory,
                             int *projectId,
                             QString *errorMessage)
{
    Project project;
    project.name = name;
    project.directory = directory;
    return addProject(project, projectId, errorMessage);
}

bool ProjectRepository::addProject(const Project &project, int *projectId, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "INSERT INTO projects(name, directory, target_type, target_name, target_user, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)"));
    const QString timestamp = nowIso();
    query.addBindValue(project.name);
    query.addBindValue(text(project.directory));
    query.addBindValue(project.targetType);
    query.addBindValue(text(project.targetName));
    query.addBindValue(text(project.targetUser));
    query.addBindValue(timestamp);
    query.addBindValue(timestamp);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    if (projectId != nullptr) {
        *projectId = query.lastInsertId().toInt();
    }
    return true;
}

bool ProjectRepository::updateProject(int id,
                                const QString &name,
                                const QString &directory,
                                QString *errorMessage)
{
    for (auto project : projects(errorMessage)) {
        if (project.id != id) continue;
        project.name = name;
        project.directory = directory;
        return updateProject(project, errorMessage);
    }
    return fail(errorMessage, QStringLiteral("项目不存在。"));
}

bool ProjectRepository::updateProject(const Project &project, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "UPDATE projects SET name = ?, directory = ?, target_type = ?, target_name = ?, target_user = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(project.name);
    query.addBindValue(text(project.directory));
    query.addBindValue(project.targetType);
    query.addBindValue(text(project.targetName));
    query.addBindValue(text(project.targetUser));
    query.addBindValue(nowIso());
    query.addBindValue(project.id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool ProjectRepository::deleteProject(int id, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("DELETE FROM projects WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool ProjectRepository::setProjectCompleted(int id, bool completed, QString *errorMessage)
{
    if (!isOpen()) return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("UPDATE projects SET completed = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(completed);
    query.addBindValue(nowIso());
    query.addBindValue(id);
    if (!query.exec()) { setError(errorMessage, query); return false; }
    return query.numRowsAffected() == 1 || fail(errorMessage, QStringLiteral("项目不存在，请重新打开项目列表。"));
}

QList<AppDatabase::RecordFolder> ProjectRepository::recordFolders(int projectId, QString *errorMessage) const
{
    QList<RecordFolder> result;
    if (!isOpen()) { fail(errorMessage, QStringLiteral("数据库尚未打开。")); return result; }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("SELECT id, project_id, name, starred FROM project_record_folders WHERE project_id = ? ORDER BY starred DESC, name COLLATE NOCASE, id"));
    query.addBindValue(projectId);
    if (!query.exec()) { setError(errorMessage, query); return result; }
    while (query.next()) result.append({query.value(0).toInt(), query.value(1).toInt(), query.value(2).toString(), query.value(3).toBool()});
    for (bool starred : {true, false})
        applySiblingOrder(result, setting(folderOrderKey(projectId, starred)), [starred](const auto &folder) { return folder.starred == starred; });
    return result;
}

bool ProjectRepository::addRecordFolder(int projectId, const QString &name, int *folderId, QString *errorMessage)
{
    if (!isOpen()) return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    if (name.trimmed().isEmpty()) return fail(errorMessage, QStringLiteral("请输入文件夹名称。"));
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("INSERT INTO project_record_folders(project_id, name) VALUES (?, ?)"));
    query.addBindValue(projectId);
    query.addBindValue(name.trimmed());
    if (!query.exec()) { setError(errorMessage, query); return false; }
    if (folderId) *folderId = query.lastInsertId().toInt();
    return true;
}

bool ProjectRepository::renameRecordFolder(int id, const QString &name, QString *errorMessage)
{
    if (!isOpen()) return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    if (name.trimmed().isEmpty()) return fail(errorMessage, QStringLiteral("请输入文件夹名称。"));
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("UPDATE project_record_folders SET name = ? WHERE id = ?"));
    query.addBindValue(name.trimmed());
    query.addBindValue(id);
    if (!query.exec()) { setError(errorMessage, query); return false; }
    return query.numRowsAffected() == 1 || fail(errorMessage, QStringLiteral("文件夹不存在，请重新打开项目。"));
}

bool ProjectRepository::setRecordFolderStarred(int id, bool starred, QString *errorMessage)
{
    if (!isOpen()) return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("UPDATE project_record_folders SET starred = ? WHERE id = ?"));
    query.addBindValue(starred);
    query.addBindValue(id);
    if (!query.exec()) { setError(errorMessage, query); return false; }
    return query.numRowsAffected() == 1 || fail(errorMessage, QStringLiteral("文件夹不存在，请重新打开项目。"));
}

bool ProjectRepository::deleteRecordFolder(int id, QString *errorMessage)
{
    if (!isOpen()) return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("DELETE FROM project_record_folders WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) { setError(errorMessage, query); return false; }
    return query.numRowsAffected() == 1 || fail(errorMessage, QStringLiteral("文件夹不存在，请重新打开项目。"));
}

bool ProjectRepository::setWorkRecordFolder(int id, int folderId, QString *errorMessage)
{
    if (!isOpen()) return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("UPDATE project_work_records SET folder_id = ? WHERE id = ?"));
    query.addBindValue(folderId > 0 ? QVariant(folderId) : QVariant());
    query.addBindValue(id);
    if (!query.exec()) { setError(errorMessage, query); return false; }
    return query.numRowsAffected() == 1 || fail(errorMessage, QStringLiteral("协作记录不存在，请重新打开项目。"));
}

QList<AppDatabase::WorkRecord> ProjectRepository::workRecords(int projectId, QString *errorMessage) const
{
    QList<WorkRecord> result;
    if (!isOpen()) {
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "SELECT id, project_id, content, created_at, updated_at, title, folder_id, summary "
        "FROM project_work_records WHERE project_id = ? ORDER BY created_at DESC, id DESC"));
    query.addBindValue(projectId);
    if (!query.exec()) {
        setError(errorMessage, query);
        return result;
    }
    while (query.next()) {
        WorkRecord record;
        record.id = query.value(0).toInt();
        record.projectId = query.value(1).toInt();
        record.title = query.value(5).toString();
        record.folderId = query.value(6).toInt();
        record.summary = query.value(7).toString();
        record.content = query.value(2).toString();
        record.createdAt = QDateTime::fromString(query.value(3).toString(), Qt::ISODateWithMs);
        record.updatedAt = QDateTime::fromString(query.value(4).toString(), Qt::ISODateWithMs);
        result.append(record);
    }
    QSet<int> folders;
    for (const auto &record : result) folders.insert(record.folderId);
    for (int folderId : folders)
        applySiblingOrder(result, setting(recordOrderKey(projectId, folderId)), [folderId](const auto &record) { return record.folderId == folderId; });
    return result;
}

bool ProjectRepository::addWorkRecord(int projectId, const QString &content, QString *errorMessage)
{
    return addWorkRecord(projectId, QStringLiteral(""), content, errorMessage);
}

bool ProjectRepository::addWorkRecord(int projectId, const QString &title, const QString &content, QString *errorMessage)
{
    return addWorkRecord(projectId, title, content, 0, errorMessage);
}

bool ProjectRepository::addWorkRecord(int projectId, const QString &title, const QString &content, int folderId, QString *errorMessage)
{
    WorkRecord record;
    record.projectId = projectId;
    record.title = title;
    record.content = content;
    record.folderId = folderId;
    return saveWorkRecord(&record, errorMessage);
}

bool ProjectRepository::saveWorkRecord(WorkRecord *record, QString *errorMessage)
{
    if (!isOpen()) return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    if (!record) return fail(errorMessage, QStringLiteral("协作记录无效。"));
    const QString timestamp = nowIso();
    const bool creating = record->id <= 0;
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    if (creating) {
        query.prepare(QStringLiteral("INSERT INTO project_work_records(project_id, title, summary, content, created_at, updated_at, folder_id) VALUES (?, ?, ?, ?, ?, ?, ?)"));
        query.addBindValue(record->projectId);
        query.addBindValue(text(record->title));
        query.addBindValue(text(record->summary));
        query.addBindValue(text(record->content));
        query.addBindValue(timestamp);
        query.addBindValue(timestamp);
        query.addBindValue(record->folderId > 0 ? QVariant(record->folderId) : QVariant());
    } else {
        query.prepare(QStringLiteral("UPDATE project_work_records SET title = ?, summary = ?, content = ?, updated_at = ? WHERE id = ? AND project_id = ?"));
        query.addBindValue(text(record->title));
        query.addBindValue(text(record->summary));
        query.addBindValue(text(record->content));
        query.addBindValue(timestamp);
        query.addBindValue(record->id);
        query.addBindValue(record->projectId);
    }
    if (!query.exec()) { setError(errorMessage, query); return false; }
    if (query.numRowsAffected() != 1) return fail(errorMessage, QStringLiteral("协作记录不存在，请重新打开项目。"));
    if (creating) {
        record->id = query.lastInsertId().toInt();
        record->createdAt = QDateTime::fromString(timestamp, Qt::ISODateWithMs);
    }
    record->updatedAt = QDateTime::fromString(timestamp, Qt::ISODateWithMs);
    return true;
}

bool ProjectRepository::updateWorkRecord(int id, const QString &content, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("UPDATE project_work_records SET content = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(content);
    query.addBindValue(nowIso());
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool ProjectRepository::updateWorkRecord(int id, const QString &title, const QString &content, QString *errorMessage)
{
    if (!isOpen()) return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("UPDATE project_work_records SET title = ?, content = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(title.isNull() ? QStringLiteral("") : title);
    query.addBindValue(content);
    query.addBindValue(nowIso());
    query.addBindValue(id);
    if (!query.exec()) { setError(errorMessage, query); return false; }
    return true;
}

bool ProjectRepository::deleteWorkRecord(int id, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("DELETE FROM project_work_records WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool ProjectRepository::reorderProjects(const QList<int> &ids, QString *errorMessage)
{
    return saveSiblingOrder(QStringLiteral("order.projects"), QStringLiteral("SELECT id FROM projects"), {}, ids, errorMessage);
}

bool ProjectRepository::reorderRecordFolders(int projectId, bool starred, const QList<int> &ids, QString *errorMessage)
{
    return saveSiblingOrder(folderOrderKey(projectId, starred),
                            QStringLiteral("SELECT id FROM project_record_folders WHERE project_id = ? AND starred = ?"),
                            {projectId, starred ? 1 : 0}, ids, errorMessage);
}

bool ProjectRepository::reorderWorkRecords(int projectId, int folderId, const QList<int> &ids, QString *errorMessage)
{
    return saveSiblingOrder(recordOrderKey(projectId, folderId),
                            QStringLiteral("SELECT id FROM project_work_records WHERE project_id = ? AND COALESCE(folder_id, 0) = ?"),
                            {projectId, folderId}, ids, errorMessage);
}
