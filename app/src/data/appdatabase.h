#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QStringList>

class AppDatabase
{
public:
    struct DiaryEntry {
        int id = 0;
        QDate date;
        QString content;
        QString title;
        QDateTime createdAt;
        QDateTime updatedAt;
    };

    struct Project {
        int id = 0;
        QString name;
        QString directory;
        QString outline;
        QDateTime createdAt;
        QDateTime updatedAt;
    };

    struct WorkRecord {
        int id = 0;
        int projectId = 0;
        QString title;
        QString content;
        QDateTime createdAt;
        QDateTime updatedAt;
    };

    struct Reminder {
        int id = 0;
        QString title;
        QString content;
        QDateTime remindAt;
        QString repeatMode = QStringLiteral("none");
        int projectId = 0;
        bool completed = false;
        QDateTime lastNotifiedAt;
        QDateTime createdAt;
        QDateTime updatedAt;
    };

    struct ToolGroup {
        int id = 0;
        QString name;
        int sortOrder = 0;
    };

    struct AutomationTool {
        int id = 0;
        QString externalId;
        int groupId = 0;
        QString name;
        QString description;
        QString targetType;
        QString registrationPath;
        QString statePath;
        QString sshHost;
        QString sshUser;
        QString wslDistribution;
        QString wslUser;
        bool sameExecutionTarget(const AutomationTool &other) const
        {
            return targetType == other.targetType && sshHost == other.sshHost && sshUser == other.sshUser
                && wslDistribution == other.wslDistribution && wslUser == other.wslUser;
        }
        QString workingDirectory;
        bool refreshEnabled = false;
        QString refreshMode = QStringLiteral("interval");
        int refreshIntervalSeconds = 300;
        QString dailyRefreshTime = QStringLiteral("08:00");
        // Shipped in the app's own tools/ folder and synced on every start;
        // it cannot be removed and its paths follow the program directory.
        bool builtin = false;
    };

    struct ToolCommand {
        int id = 0;
        int toolId = 0;
        QString name;
        QString description;
        QString executable;
        QStringList arguments;
        // Parameter definitions from the tool manifest; arguments may reference
        // them with {key} placeholders that are filled in when the command runs.
        QJsonArray parameters;
        bool interactive = false;
        bool highRisk = false;
    };

    AppDatabase() = default;
    ~AppDatabase();

    AppDatabase(const AppDatabase &) = delete;
    AppDatabase &operator=(const AppDatabase &) = delete;

    bool open(QString *errorMessage = nullptr);
    bool isOpen() const;
    QString databasePath() const;

    QList<DiaryEntry> diaryEntries(const QDate &date, QString *errorMessage = nullptr) const;
    QList<DiaryEntry> allDiaryEntries(QString *errorMessage = nullptr) const;
    DiaryEntry diaryEntry(int id, QString *errorMessage = nullptr) const;
    QList<QDate> diaryDates(const QDate &from, const QDate &to, QString *errorMessage = nullptr) const;
    bool addDiaryEntry(const QDate &date, const QString &content, QString *errorMessage = nullptr);
    bool addDiaryEntry(const QDate &date, const QString &title, const QString &content, QString *errorMessage = nullptr);
    bool updateDiaryEntry(int id, const QString &content, QString *errorMessage = nullptr);
    bool updateDiaryEntry(int id, const QString &title, const QString &content, QString *errorMessage = nullptr);
    bool deleteDiaryEntry(int id, QString *errorMessage = nullptr);

    QList<Reminder> reminders(const QDate &date, QString *errorMessage = nullptr) const;
    QList<Reminder> remindersInMonth(int year, int month, QString *errorMessage = nullptr) const;
    QList<Reminder> dueReminders(const QDateTime &now, QString *errorMessage = nullptr) const;
    bool addReminder(const Reminder &reminder,
                     int *reminderId = nullptr,
                     QString *errorMessage = nullptr);
    bool updateReminder(const Reminder &reminder, QString *errorMessage = nullptr);
    bool deleteReminder(int id, QString *errorMessage = nullptr);
    bool completeReminder(int id, const QDateTime &now, QString *errorMessage = nullptr);
    bool processDueReminder(int id, const QDateTime &now, QString *errorMessage = nullptr);

    QList<Project> projects(QString *errorMessage = nullptr) const;
    bool addProject(const QString &name,
                    const QString &directory,
                    const QString &outline,
                    int *projectId = nullptr,
                    QString *errorMessage = nullptr);
    bool updateProject(int id,
                       const QString &name,
                       const QString &directory,
                       const QString &outline,
                       QString *errorMessage = nullptr);
    bool deleteProject(int id, QString *errorMessage = nullptr);
    QList<WorkRecord> workRecords(int projectId, QString *errorMessage = nullptr) const;
    bool addWorkRecord(int projectId, const QString &content, QString *errorMessage = nullptr);
    bool addWorkRecord(int projectId, const QString &title, const QString &content, QString *errorMessage = nullptr);
    bool updateWorkRecord(int id, const QString &content, QString *errorMessage = nullptr);
    bool updateWorkRecord(int id, const QString &title, const QString &content, QString *errorMessage = nullptr);
    bool deleteWorkRecord(int id, QString *errorMessage = nullptr);

    QList<ToolGroup> toolGroups(QString *errorMessage = nullptr) const;
    bool addToolGroup(const QString &name, int *groupId = nullptr, QString *errorMessage = nullptr);
    bool renameToolGroup(int id, const QString &name, QString *errorMessage = nullptr);
    bool deleteToolGroup(int id, QString *errorMessage = nullptr);

    QList<AutomationTool> automationTools(int groupId = 0, QString *errorMessage = nullptr) const;
    bool addAutomationTool(const AutomationTool &tool,
                           int *toolId = nullptr,
                           QString *errorMessage = nullptr);
    // Updates the manifest-derived fields of a tool; group and refresh settings are kept.
    // Built-in synchronization may promote a tool; updates never clear built-in ownership.
    bool updateAutomationTool(const AutomationTool &tool, QString *errorMessage = nullptr);
    // Save manifest fields and commands as one unit; publish a new id only after commit.
    bool saveAutomationTool(const AutomationTool &tool, const QList<ToolCommand> &commands,
                            int *toolId = nullptr, QString *errorMessage = nullptr);
    bool setToolGroup(int toolId, int groupId, QString *errorMessage = nullptr);
    bool deleteAutomationTool(int id, QString *errorMessage = nullptr);
    bool updateToolRefreshSettings(int id,
                                   bool enabled,
                                   const QString &mode,
                                   int intervalSeconds,
                                   const QString &dailyTime,
                                   QString *errorMessage = nullptr);
    QList<ToolCommand> toolCommands(int toolId, QString *errorMessage = nullptr) const;
    bool addToolCommand(const ToolCommand &command, int *commandId = nullptr, QString *errorMessage = nullptr);
    bool replaceToolCommands(int toolId, const QList<ToolCommand> &commands, QString *errorMessage = nullptr);

    QString setting(const QString &key, const QString &fallback = QString()) const;
    bool setSetting(const QString &key, const QString &value, QString *errorMessage = nullptr);

private:
    bool replaceToolCommandsInTransaction(int toolId, const QList<ToolCommand> &commands, QString *errorMessage);
    bool initializeSchema(QString *errorMessage);
    bool fail(QString *errorMessage, const QString &message) const;
    QString connectionName_;
    QString databasePath_;
};
