#pragma once
#include "git/gitreader.h"
#include <QDialog>

class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;

class GitCommitDialog final : public QDialog {
    Q_OBJECT
public:
    explicit GitCommitDialog(const RepositoryTarget &target, QWidget *parent = nullptr);
    void setCommit(const Git::Commit &commit);
protected:
    void done(int result) override;
private:
    void loadFiles();
    void loadDiff();
    RepositoryTarget target_;
    Git::Reader detailReader_, fileReader_, diffReader_;
    Git::Commit selected_;
    QList<Git::FileChange> changes_;
    QComboBox *parent_;
    QLabel *status_;
    QListWidget *files_;
    QPlainTextEdit *message_, *diff_;
};
