#include "ui/theme.h"
#include "collaborationrecorddialog.h"
#include "widgets/headingtextedit.h"
#include "widgets/persistenttextzoom.h"
#include "widgets/summaryfield.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QAbstractButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

CollaborationRecordDialog::CollaborationRecordDialog(AppDatabase *database,
    AppDatabase::WorkRecord record, QWidget *parent)
    : QDialog(parent), database_(database), record_(std::move(record))
{
    setObjectName(QStringLiteral("collaborationRecordDialog"));
    setWindowTitle(record_.id > 0 ? QStringLiteral("编辑记录") : QStringLiteral("添加记录"));
    setWindowFlag(Qt::WindowMaximizeButtonHint);
    resize(880, 660);
    setMinimumSize(560, 400);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(12);
    auto *header = new QHBoxLayout;
    header->setSpacing(16);
    auto *titleLayout = new QVBoxLayout;
    titleLayout->setContentsMargins(0, 0, 0, 0);
    title_ = addSummaryField(titleLayout, this, QStringLiteral("workRecordTitle"), QStringLiteral("标题"));
    title_->setMaximumWidth(520);
    header->addLayout(titleLayout, 1);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("recordSaveStatus"));
    status_->setAccessibleName(QStringLiteral("保存状态"));
    status_->setTextFormat(Qt::PlainText);
    status_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(status_, 1);
    sections_ = new QTabBar(this);
    sections_->setObjectName(QStringLiteral("recordSectionTabs"));
    sections_->setAccessibleName(QStringLiteral("协作记录内容"));
    sections_->setExpanding(false);
    // Two fixed tabs need no extra minimum width reserved for scroll buttons.
    sections_->setUsesScrollButtons(false);
    sections_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    sections_->setDrawBase(false);
    sections_->addTab(QStringLiteral("摘要"));
    sections_->addTab(QStringLiteral("正文"));

    header->addWidget(sections_);
    layout->addLayout(header);

    pages_ = new QStackedWidget(this);
    pages_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    summary_ = new HeadingTextEdit(pages_);
    summary_->setObjectName(QStringLiteral("recordSummaryEditor"));
    summary_->setContentLabel(QStringLiteral("摘要"));
    summary_->setPlaceholderText(QStringLiteral("记录关键要点、适用场景和重要约束……"));
    body_ = new HeadingTextEdit(pages_);
    body_->setObjectName(QStringLiteral("recordBodyEditor"));
    body_->setContentLabel(QStringLiteral("正文"));
    body_->setPlaceholderText(QStringLiteral("填写完整提示词或协作原文……"));
    for (auto *editor : {summary_, body_}) editor->setModeControlsVisible(false);
    // Start this typography revision at the requested sizes; subsequent zooms
    // remain independent, without changing preferences for other record types.
    summary_->setTextPointSize(24);
    body_->setTextPointSize(20);
    bindPersistentTextZoom(summary_, database_, QStringLiteral("text_zoom.work_record_summary.v2"));
    bindPersistentTextZoom(body_, database_, QStringLiteral("text_zoom.work_record.v2"));
    pages_->addWidget(summary_);
    pages_->addWidget(body_);

    layout->addWidget(pages_, 1);

    title_->setText(record_.id > 0 ? summaryText(record_.title, record_.content) : QString());
    summary_->setPlainText(record_.summary);
    body_->setPlainText(record_.content);
    rememberDraft();
    connect(sections_, &QTabBar::currentChanged, this, [this] { showSection(); });
    const auto changed = [this] { error_.clear(); updateStatus(); };
    connect(title_, &QLineEdit::textChanged, this, changed);
    connect(summary_, &HeadingTextEdit::textChanged, this, [this, changed] {
        draftSummaryText_ = summary_->toPlainText(); changed();
    });
    connect(body_, &HeadingTextEdit::textChanged, this, [this, changed] {
        draftBodyText_ = body_->toPlainText(); changed();
    });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
    saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveShortcut, &QShortcut::activated, this, [this] { save(); });
    showSection();
    updateStatus();
    if (record_.id <= 0) title_->setFocus();
}

void CollaborationRecordDialog::rememberDraft()
{
    savedTitle_ = title_->text().trimmed();
    savedBodyText_ = draftBodyText_ = body_->toPlainText();
    savedSummaryText_ = draftSummaryText_ = summary_->toPlainText();
}

bool CollaborationRecordDialog::dirty() const
{
    return title_->text().trimmed() != savedTitle_ || draftBodyText_ != savedBodyText_
        || draftSummaryText_ != savedSummaryText_;
}

QString CollaborationRecordDialog::bodyValue() const
{
    const auto &value = draftBodyText_;
    // Preserve original line endings when only the title or summary is edited.
    return value == savedBodyText_ ? record_.content : value;
}

QString CollaborationRecordDialog::summaryValue() const
{
    const auto &value = draftSummaryText_;
    return value == savedSummaryText_ ? record_.summary : value;
}

void CollaborationRecordDialog::resetDraft()
{
    title_->setText(savedTitle_);
    if (draftSummaryText_ != savedSummaryText_) summary_->setPlainText(record_.summary);
    if (draftBodyText_ != savedBodyText_) body_->setPlainText(record_.content);
    error_.clear();
    updateStatus();
}

void CollaborationRecordDialog::showSection()
{
    auto *editor = sections_->currentIndex() == 0 ? summary_ : body_;
    pages_->setCurrentWidget(editor);
}

void CollaborationRecordDialog::updateStatus()
{
    const bool missingTitle = title_->text().trimmed().isEmpty();
    const bool missingBody = draftBodyText_.trimmed().isEmpty();
    UiTheme::setRole(status_, !error_.isEmpty() ? "error" : dirty() ? "warning" : "muted");
    if (!error_.isEmpty()) status_->setText(QStringLiteral("保存失败"));
    else if (missingTitle) status_->setText(QStringLiteral("待填写标题"));
    else if (missingBody) status_->setText(QStringLiteral("待填写正文"));
    else if (dirty()) status_->setText(QStringLiteral("未保存 · Ctrl+S"));
    else status_->setText(QStringLiteral("已保存"));

    QString detail = QStringLiteral("Ctrl+S 保存标题、摘要和正文");
    if (!error_.isEmpty()) detail = error_;
    else if (missingTitle || missingBody) detail = QStringLiteral("请填写标题和正文，摘要可留空。");
    else if (!dirty() && record_.updatedAt.isValid())
        detail += QStringLiteral("\n更新于 %1").arg(record_.updatedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    status_->setToolTip(detail);
    status_->setAccessibleDescription(detail);
}

bool CollaborationRecordDialog::save()
{
    if (title_->text().trimmed().isEmpty() || draftBodyText_.trimmed().isEmpty()) {
        if (title_->text().trimmed().isEmpty()) title_->setFocus();
        else { sections_->setCurrentIndex(1); body_->setFocus(); }
        return false;
    }
    if (!dirty() && record_.id > 0) return true;
    auto draft = record_;
    draft.title = title_->text().trimmed();
    draft.summary = summaryValue();
    draft.content = bodyValue();
    QString error;
    if (!database_->saveWorkRecord(&draft, &error)) {
        error_ = QStringLiteral("保存失败：%1").arg(error);
        updateStatus();
        return false;
    }
    record_ = draft;
    saved_ = true;
    error_.clear();
    rememberDraft();
    setWindowTitle(QStringLiteral("编辑记录"));
    updateStatus();
    UiTheme::setRole(status_, "success");
    emit savedRecord(record_.id);
    return true;
}

bool CollaborationRecordDialog::confirmLeave()
{
    if (dirty()) {
        QMessageBox prompt(QMessageBox::Question, QStringLiteral("未保存的修改"),
            QStringLiteral("要保存这份协作记录的修改吗？"), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
        prompt.button(QMessageBox::Save)->setText(QStringLiteral("保存"));
        prompt.button(QMessageBox::Discard)->setText(QStringLiteral("放弃修改"));
        prompt.button(QMessageBox::Cancel)->setText(QStringLiteral("继续编辑"));
        prompt.setDefaultButton(QMessageBox::Cancel);
        const int choice = prompt.exec();
        if (choice == QMessageBox::Save) return save();
        if (choice != QMessageBox::Discard) return false;
        resetDraft();
    }
    return true;
}

void CollaborationRecordDialog::reject()
{
    if (confirmLeave()) QDialog::reject();
}
