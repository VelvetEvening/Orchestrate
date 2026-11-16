#pragma once

#include "appdatabase.h"
#include "repositorybase.h"

class ToolRepository final : public RepositoryBase
{
public:
    using RepositoryBase::RepositoryBase;
    using ToolGroup = AppDatabase::ToolGroup;
    using AutomationTool = AppDatabase::AutomationTool;
    using ToolCommand = AppDatabase::ToolCommand;

    // Include the virtual id=0 group when loading the reorderable tool tree.
    QList<ToolGroup> toolGroups(QString *errorMessage = nullptr, bool includeUngrouped = false) const;
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

    bool reorderToolGroups(const QList<int> &ids, QString *errorMessage = nullptr);
    bool reorderTools(int groupId, const QList<int> &ids, QString *errorMessage = nullptr);

private:
    bool replaceToolCommandsInTransaction(int toolId, const QList<ToolCommand> &commands, QString *errorMessage);
};
