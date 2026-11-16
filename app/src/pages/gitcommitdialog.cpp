#include "gitcommitdialog.h"
#include "ui/theme.h"
#include <QComboBox>
#include <QFontDatabase>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QVBoxLayout>

namespace {
class DiffHighlighter final : public QSyntaxHighlighter {
public:
    explicit DiffHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}
    void highlightBlock(const QString &text) override {
        QTextCharFormat format;
        if (text.startsWith(QStringLiteral("+++")) || text.startsWith(QStringLiteral("---")) || text.startsWith(QStringLiteral("diff "))) {
            format.setForeground(QColor(UiTheme::Text)); format.setFontWeight(QFont::DemiBold);
        } else if (text.startsWith('+')) { format.setForeground(QColor(UiTheme::Success)); format.setBackground(QColor(UiTheme::SuccessBackground)); }
        else if (text.startsWith('-')) { format.setForeground(QColor(UiTheme::Danger)); format.setBackground(QColor(UiTheme::DangerBackground)); }
        else if (text.startsWith(QStringLiteral("@@"))) { format.setForeground(QColor(UiTheme::Accent)); format.setBackground(QColor(UiTheme::Selection)); }
        else return;
        setFormat(0, text.size(), format);
    }
};
}

GitCommitDialog::GitCommitDialog(const RepositoryTarget &target, QWidget *parent)
    : QDialog(parent, Qt::Window), target_(target), detailReader_(this), fileReader_(this), diffReader_(this)
{
    setObjectName(QStringLiteral("gitCommitDialog"));
    setAttribute(Qt::WA_DeleteOnClose); setWindowFlag(Qt::WindowMaximizeButtonHint);
    resize(1000, 700); setMinimumSize(720, 480);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(20,16,20,16); layout->setSpacing(12);
    message_ = new QPlainTextEdit(this); message_->setObjectName(QStringLiteral("gitCommitMessage")); message_->setReadOnly(true); message_->setMaximumHeight(150); message_->setPlaceholderText(QStringLiteral("提交说明"));
    layout->addWidget(message_);
    auto *parentRow = new QHBoxLayout;
    parentRow->addWidget(new QLabel(QStringLiteral("对比基准"), this));
    parent_ = new QComboBox(this); parent_->setAccessibleName(QStringLiteral("对比父提交")); parentRow->addWidget(parent_);
    status_ = new QLabel(this); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); UiTheme::setRole(status_, "muted"); parentRow->addWidget(status_,1); layout->addLayout(parentRow);
    auto *fileSplit = new QSplitter(this); fileSplit->setChildrenCollapsible(false);
    files_ = new QListWidget(fileSplit); files_->setObjectName(QStringLiteral("gitFiles")); files_->setAccessibleName(QStringLiteral("变更文件")); files_->setMinimumWidth(150);
    diff_ = new QPlainTextEdit(fileSplit); diff_->setObjectName(QStringLiteral("gitDiff")); diff_->setReadOnly(true); diff_->setLineWrapMode(QPlainTextEdit::NoWrap); diff_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    diff_->setPlaceholderText(QStringLiteral("选择文件查看差异")); new DiffHighlighter(diff_->document());
    fileSplit->setSizes({250,800}); layout->addWidget(fileSplit,1);
    connect(parent_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{ loadFiles(); });
    connect(files_,&QListWidget::currentRowChanged,this,[this]{ loadDiff(); });
}
void GitCommitDialog::done(int result) {
    detailReader_.cancel(); fileReader_.cancel(); diffReader_.cancel();
    QDialog::done(result);
}
void GitCommitDialog::setCommit(const Git::Commit &commit)
{
    if (selected_.id == commit.id) return;
    detailReader_.cancel(); fileReader_.cancel(); diffReader_.cancel();
    selected_ = {}; changes_.clear(); message_->clear(); files_->clear(); diff_->clear(); status_->clear();
    const QSignalBlocker block(parent_); parent_->clear();
    selected_ = commit;
    setWindowTitle(selected_.id.left(10) + QStringLiteral(" · 提交详情 · Orchestrate"));
    if (selected_.parents.isEmpty()) parent_->addItem(QStringLiteral("空树（首次提交）"),QString());
    for (int i=0;i<selected_.parents.size();++i) parent_->addItem(QStringLiteral("父提交 %1 · %2").arg(i+1).arg(selected_.parents[i].left(10)),selected_.parents[i]);
    message_->setPlainText(selected_.id + QStringLiteral("\n") + selected_.subject);
    detailReader_.run(target_,{QStringLiteral("show"),QStringLiteral("-s"),QStringLiteral("--format=%B"),selected_.id,QStringLiteral("--")},[this](Git::Result result){
        if (!result.ok) { message_->setPlainText(selected_.id+QLatin1Char('\n')+selected_.subject+QLatin1Char('\n')+result.error); return; }
        message_->setPlainText(selected_.id + QStringLiteral("\n") + selected_.author + QStringLiteral(" · ") + selected_.date.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + QStringLiteral("\n\n") + QString::fromUtf8(result.output));
    });
    loadFiles();
}
void GitCommitDialog::loadFiles()
{
    fileReader_.cancel(); diffReader_.cancel(); changes_.clear(); files_->clear(); diff_->clear();
    if (selected_.id.isEmpty()) return;
    status_->setText(QStringLiteral("正在读取变更文件…"));
    auto args = Git::fileArguments(selected_.id,parent_->currentData().toString()); args << QStringLiteral("--");
    fileReader_.run(target_,args,[this](Git::Result result){
        if (!result.ok || !Git::parseFiles(result.output,&changes_)) { status_->setText(result.ok ? QStringLiteral("无法解析变更文件。") : result.error); return; }
        for (const auto &file : changes_) {
            auto *item = new QListWidgetItem(file.status + QStringLiteral("  ") + (file.oldPath.isEmpty() ? file.path : file.oldPath + QStringLiteral(" → ") + file.path),files_);
            item->setToolTip(item->text());
        }
        status_->setText(changes_.isEmpty() ? QStringLiteral("相对所选基准没有文件变更。") : QStringLiteral("%1 个变更文件").arg(changes_.size()));
        if (!changes_.isEmpty()) files_->setCurrentRow(0);
    });
}
void GitCommitDialog::loadDiff()
{
    diffReader_.cancel(); diff_->clear();
    const int row = files_->currentRow(); if (row < 0 || row >= changes_.size()) return;
    diff_->setPlainText(QStringLiteral("正在读取差异…"));
    const auto file = changes_[row];
    auto args = Git::fileArguments(selected_.id,parent_->currentData().toString(),true); args << QStringLiteral("--");
    if (!file.oldPath.isEmpty()) args << Git::literalFilePath(file.oldPath);
    args << Git::literalFilePath(file.path);
    diffReader_.run(target_,args,[this](Git::Result result){ diff_->setPlainText(result.ok ? QString::fromUtf8(result.output) : result.error); },4*1024*1024);
}
