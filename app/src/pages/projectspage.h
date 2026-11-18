#pragma once
#include <QWidget>
#include <QPointer>
#include <QHash>
#include "../data/appdatabase.h"
class QListWidget;
class QTreeWidget;
class GitHistoryWindow;
class QLabel;
class QPushButton;
class QDialog;
class QTabBar;
namespace Git { class Reader; }

class ProjectsPage final : public QWidget
{
    Q_OBJECT
public:
    explicit ProjectsPage(AppDatabase *database, QWidget *parent = nullptr);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
private:
    void loadProjects(int preferred = 0);
    void selectProject(int id);
    void projectDialog(bool creating);
    void openProjectDirectory(const AppDatabase::Project &project);
    bool deleteProject(QWidget *parent);
    void refreshWorkRecords();
    void recordDialog(bool creating);
    void deleteWorkRecord();
    void openGitHistory();
    void refreshGitAvailability();
    void setGitAvailability(bool available, const QString &hint);
    void filterRecords();
    void showProjectContextMenu(const QPoint &position);
    void showRecordContextMenu(const QPoint &position);
    void folderDialog(bool creating);
    void deleteFolder();
    void saveExpandedFolders();
    int selectedFolderId() const;
    AppDatabase::WorkRecord currentWorkRecord() const;
    void showError(const QString &message);
    AppDatabase *database_;
    AppDatabase::Project current_;
    bool openingDirectory_ = false;
    QTabBar *projectStatusTabs_;
    int selectedProjectIds_[2] = {0, 0};
    QListWidget *projectList_;
    QWidget *details_;
    QLabel *projectTitle_;
    QPushButton *gitHistoryButton_;
    Git::Reader *gitAvailabilityReader_;
    bool gitCheckPending_ = false;
    QTreeWidget *workList_;
    QLabel *recordHint_;
    int recordsProjectId_ = 0;
    class QLineEdit *search_;
    QHash<int, QPointer<GitHistoryWindow>> gitWindows_;
};
