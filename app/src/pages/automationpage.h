#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QWidget>

#include "../data/appdatabase.h"

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTimeEdit;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

class AutomationPage final : public QWidget
{
    Q_OBJECT

public:
    explicit AutomationPage(AppDatabase *database, QWidget *parent = nullptr);

    ~AutomationPage() override;

    // Re-read every registered tool state once when the application starts.
    // This is intentionally separate from showing/restoring the tray window.
    void refreshAllStates();
    bool hasRunningCommands() const;

protected:
    void showEvent(QShowEvent *event) override;

private:
    friend class AutomationContractTest;
    struct ParsedManifest {
        AppDatabase::AutomationTool tool;
        QList<AppDatabase::ToolCommand> commands;
        QString group;
    };

    // Output and outcome of the most recent command run of one tool.
    struct RunRecord {
        QString title;
        QString status;
        QString output;
        bool running = false;
        bool failed = false;
        bool outputTruncated = false;
        qint64 outputOffset = 0;
        bool outputEndedWithCr = false;
    };

    void loadAll(int preferredToolId = 0);
    void handleTreeSelection();
    void showTreeContextMenu(const QPoint &position);
    void updateToolIcon(int toolId);
    QString ungroupedName() const;
    bool findTool(int toolId, AppDatabase::AutomationTool *tool) const;

    void loadToolDetails(int toolId);
    void showToolInformation(int toolId);
    void showEmptyDetails(int groupId = -1);
    void createGroup();
    void renameGroup();
    void deleteGroup();
    void importTool();
    void importRemoteTool();
    void importWslTool();
    void finishManifestReload(const AppDatabase::AutomationTool &tool, const QByteArray &data);
    bool readManifest(const QString &sourcePath,
                      QByteArray *data,
                      QString *errorMessage) const;
    bool parseManifest(const QByteArray &manifestData,
                       const QString &sourcePath,
                       const QString &sshHost,
                       ParsedManifest *manifest,
                       QString *errorMessage,
                       const QString &wslDistribution = QString(),
                       const QString &wslUser = QString(),
                       const QString &sshUser = QString()) const;
    void registerToolManifest(const QByteArray &manifestData,
                              const QString &sourcePath,
                              const QString &sshHost = QString(),
                              const QString &wslDistribution = QString(),
                              const QString &wslUser = QString(),
                              const QString &sshUser = QString(), int requestedGroupId = -1);
    bool applyManifestUpdate(int toolId, ParsedManifest manifest);
    int builtinToolGroupId();
    void syncBuiltinTools();
    void reloadManifest();
    void deleteTool();
    void startAsyncWslStateRead(const AppDatabase::AutomationTool &tool, bool updateVisibleState);
    void startAsyncSshStateRead(const AppDatabase::AutomationTool &tool, bool updateVisibleState);
    void startStateReadProcess(const AppDatabase::AutomationTool &tool, bool updateVisibleState,
                               QProcess *process, int timeoutMs = 30000);
    void applyStateResult(int toolId, bool success, const QJsonObject &state, const QString &error);
    void refreshState();
    void refreshStateForTool(int toolId, bool updateVisibleState);
    void refreshDueStates();
    void renderState(const QJsonObject &state);
    void renderStateMessage(const QString &badge, const QString &result, const QString &message);
    void clearStateItems();

    void configureRefreshControls(const AppDatabase::AutomationTool &tool);
    void updateRefreshControlVisibility();
    void saveRefreshSettings();

    void runCommand(QListWidgetItem *item);
    QString remoteCommandLine(const AppDatabase::AutomationTool &tool,
                              const QString &executable,
                              const QStringList &arguments) const;
    void renderRunRecord();
    void appendRunOutput(int toolId, const QString &text);
    static constexpr qsizetype maxRunOutputChars = 256 * 1024;
    void showError(const QString &message);

    QSet<int> manifestReadsInFlight_;
    bool importInFlight_ = false;

    QTreeWidget *tree_ = nullptr;
    QPushButton *renameGroupButton_ = nullptr;
    QPushButton *deleteGroupButton_ = nullptr;
    QLabel *groupHint_ = nullptr;

    QStackedWidget *detailsStack_ = nullptr;
    QLabel *emptyTitle_ = nullptr;
    QLabel *emptyMessage_ = nullptr;

    QLabel *toolName_ = nullptr;
    QLabel *toolSource_ = nullptr;
    QPushButton *reloadManifestButton_ = nullptr;
    QPushButton *deleteToolButton_ = nullptr;

    QLabel *stateBadge_ = nullptr;
    QLabel *stateSummary_ = nullptr;
    QLabel *stateItemsTitle_ = nullptr;
    QWidget *stateItemsBox_ = nullptr;
    QVBoxLayout *stateItemsLayout_ = nullptr;

    QListWidget *commandList_ = nullptr;
    QLabel *commandHint_ = nullptr;
    QFrame *runPanel_ = nullptr;
    QLabel *runTitle_ = nullptr;
    QLabel *runStatus_ = nullptr;
    QPlainTextEdit *runOutput_ = nullptr;
    int displayedOutputTool_ = -1;
    qint64 displayedOutputStart_ = 0;
    qint64 displayedOutputEnd_ = 0;

    QCheckBox *refreshEnabledCheck_ = nullptr;
    QWidget *refreshOptions_ = nullptr;
    QComboBox *refreshModeCombo_ = nullptr;
    QWidget *intervalRow_ = nullptr;
    QWidget *dailyRow_ = nullptr;
    QComboBox *refreshIntervalCombo_ = nullptr;
    QSpinBox *customIntervalMinutes_ = nullptr;
    QTimeEdit *dailyRefreshTimeEdit_ = nullptr;
    QLabel *refreshHint_ = nullptr;
    QTimer *refreshTimer_ = nullptr;

    AppDatabase *database_ = nullptr;
    int selectedGroupId_ = 0;
    int selectedToolId_ = 0;
    QHash<int, QDateTime> lastAutoRefreshAt_;
    QHash<int, QJsonObject> stateCache_;
    QHash<int, QString> stateErrors_;
    QSet<int> stateReadsInFlight_;
    QHash<int, RunRecord> runRecords_;
    QHash<int, QPointer<QProcess>> runningProcesses_;
    bool loadingRefreshSettings_ = false;
};
