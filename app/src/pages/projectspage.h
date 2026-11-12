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
class QVBoxLayout;
class QDialog;

class ProjectsPage final : public QWidget
{
    Q_OBJECT
public:
    explicit ProjectsPage(AppDatabase *database, QWidget *parent = nullptr);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
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
    void filterRecords();
    void updateRecordActions();
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
    QListWidget *projectList_;
    QWidget *details_;
    QLabel *projectTitle_;
    QPushButton *gitHistoryButton_;
    QVBoxLayout *detailsLayout_;
    QTreeWidget *workList_;
    QLabel *recordHint_;
    int recordsProjectId_ = 0;
    class QLineEdit *search_;
    QPushButton *deleteWorkButton_;
    QHash<int, QPointer<GitHistoryWindow>> gitWindows_;
};
