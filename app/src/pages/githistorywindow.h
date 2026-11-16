#pragma once
#include "git/gitreader.h"
#include <QDialog>
#include <QPointer>
class QTabWidget;
class QTableView;
class QTableWidget;
class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QCheckBox;
class GitGraphModel;
class GitCommitDialog;

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
    void openCommit();
    void restoreFilter(const QString &filter);
    void loadRemotes();
    void compare();
    void displayBranches();
    void findNext();
    void historyStatus(const QString &message, bool error = false);
    RepositoryTarget target_;
    Git::Reader historyReader_, compareReader_;
    QPointer<GitCommitDialog> detail_;
    GitGraphModel *model_;
    QTabWidget *tabs_;
    QTableView *history_;
    QTableWidget *branches_;
    QComboBox *branch_, *tag_, *remote_;
    QLineEdit *search_, *branchSearch_;
    QPushButton *refresh_, *more_, *compareRefresh_;
    QLabel *status_, *compareStatus_;
    QCheckBox *differences_;
    QList<Git::Reference> refs_;
    QList<Git::Branch> comparison_;
    QString head_, loadedBranch_, comparedRemote_, comparisonError_;
    bool hasComparison_ = false;
    bool hasMore_ = false;
    bool historyBusy_ = false;
    bool compareLoaded_ = false;
};
