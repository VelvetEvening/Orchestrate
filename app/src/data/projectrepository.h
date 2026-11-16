#pragma once

#include "appdatabase.h"
#include "repositorybase.h"

class ProjectRepository final : public RepositoryBase
{
public:
    using RepositoryBase::RepositoryBase;
    using Project = AppDatabase::Project;
    using RecordFolder = AppDatabase::RecordFolder;
    using WorkRecord = AppDatabase::WorkRecord;

    QList<Project> projects(QString *errorMessage = nullptr) const;
    bool addProject(const QString &name,
                    const QString &directory,
                    int *projectId = nullptr,
                    QString *errorMessage = nullptr);
    bool updateProject(int id,
                       const QString &name,
                       const QString &directory,
                       QString *errorMessage = nullptr);
    bool addProject(const Project &project, int *projectId = nullptr, QString *errorMessage = nullptr);
    bool updateProject(const Project &project, QString *errorMessage = nullptr);
    bool deleteProject(int id, QString *errorMessage = nullptr);
    bool setProjectCompleted(int id, bool completed, QString *errorMessage = nullptr);
    QList<RecordFolder> recordFolders(int projectId, QString *errorMessage = nullptr) const;
    bool addRecordFolder(int projectId, const QString &name, int *folderId = nullptr, QString *errorMessage = nullptr);
    bool renameRecordFolder(int id, const QString &name, QString *errorMessage = nullptr);
    bool setRecordFolderStarred(int id, bool starred, QString *errorMessage = nullptr);
    // Removing a folder keeps its records at the project root.
    bool deleteRecordFolder(int id, QString *errorMessage = nullptr);
    bool setWorkRecordFolder(int id, int folderId, QString *errorMessage = nullptr);
    QList<WorkRecord> workRecords(int projectId, QString *errorMessage = nullptr) const;
    // Save all user-authored fields together; update identity/timestamps only on success.
    bool saveWorkRecord(WorkRecord *record, QString *errorMessage = nullptr);
    bool addWorkRecord(int projectId, const QString &content, QString *errorMessage = nullptr);
    bool addWorkRecord(int projectId, const QString &title, const QString &content, QString *errorMessage = nullptr);
    bool addWorkRecord(int projectId, const QString &title, const QString &content, int folderId, QString *errorMessage = nullptr);
    bool updateWorkRecord(int id, const QString &content, QString *errorMessage = nullptr);
    bool updateWorkRecord(int id, const QString &title, const QString &content, QString *errorMessage = nullptr);
    bool deleteWorkRecord(int id, QString *errorMessage = nullptr);

    bool reorderProjects(const QList<int> &ids, QString *errorMessage = nullptr);
    bool reorderRecordFolders(int projectId, bool starred, const QList<int> &ids, QString *errorMessage = nullptr);
    bool reorderWorkRecords(int projectId, int folderId, const QList<int> &ids, QString *errorMessage = nullptr);
};
