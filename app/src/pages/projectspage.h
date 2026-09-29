#pragma once
#include <QWidget>
#include <QPointer>
#include "../data/appdatabase.h"
class QListWidget;
class HeadingTextEdit;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QDialog;

class ProjectsPage final : public QWidget
{
    Q_OBJECT
public:
    explicit ProjectsPage(AppDatabase *database, QWidget *parent = nullptr);
    bool savePendingChanges();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void loadProjects(int preferred = 0);
    void selectProject(int id);
    void projectDialog(bool creating);
    bool saveOutline();
    void deleteProject();
    void refreshWorkRecords();
    void recordDialog(bool creating);
    void deleteWorkRecord();
    void openOutline();
    void filterRecords();
    AppDatabase::WorkRecord currentWorkRecord() const;
    void showError(const QString &message);
    AppDatabase *database_;
    AppDatabase::Project current_;
    QListWidget *projectList_;
    QWidget *details_;
    QLabel *projectTitle_;
    QLabel *saveStatus_;
    HeadingTextEdit *outlineEdit_;
    QLabel *outlineTitle_;
    QVBoxLayout *detailsLayout_;
    QListWidget *workList_;
    class QLineEdit *search_;
    QPushButton *editWorkButton_;
    QPushButton *deleteWorkButton_;
    QPushButton *deleteProjectButton_;
    QPointer<QDialog> outlineWindow_;
};
