#pragma once
#include "git/gitreader.h"
#include <QDialog>
#include <QDateTime>
class QTabWidget;
class QTableView;
class QTableWidget;
class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QPlainTextEdit;
class QListWidget;
class QCheckBox;
class GitGraphModel;

class GitHistoryWindow final : public QDialog {
    Q_OBJECT
public:
    GitHistoryWindow(const QString &projectName, const RepositoryTarget &target, QWidget *parent = nullptr);
    ~GitHistoryWindow() override;
    const RepositoryTarget &target() const { return target_; }
protected:
    void done(int result) override;
private:
    void refreshHistory();
    void loadPage(bool append, const QList<Git::Reference> &refs, const QString &head);
    void selectCommit();
    void loadFiles();
    void loadDiff();
    void loadRemotes();
    void compare();
    void displayBranches();
    void findNext();
    void historyStatus(const QString &message, bool error = false);
    RepositoryTarget target_;
    Git::Reader historyReader_, detailReader_, fileReader_, diffReader_, compareReader_;
    GitGraphModel *model_;
    QTabWidget *tabs_;
    QTableView *history_;
    QTableWidget *branches_;
    QComboBox *branch_, *remote_, *parent_;
    QLineEdit *search_, *branchSearch_;
    QPushButton *refresh_, *more_, *compareRefresh_;
    QLabel *status_, *compareStatus_, *detailStatus_;
    QPlainTextEdit *message_, *diff_;
    QListWidget *files_;
    QCheckBox *differences_;
    QList<Git::Reference> refs_;
    QList<Git::Branch> comparison_;
    QList<Git::FileChange> changes_;
    Git::Commit selected_;
    QString head_, loadedBranch_, comparedRemote_, comparisonError_;
    QDateTime historyTime_, compareTime_;
    bool hasMore_ = false;
    bool historyBusy_ = false;
    bool compareLoaded_ = false;
};
