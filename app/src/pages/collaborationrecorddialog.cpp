#include "collaborationrecorddialog.h"
#include "widgets/headingtextedit.h"
#include "widgets/persistenttextzoom.h"
#include "widgets/summaryfield.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>

CollaborationRecordDialog::CollaborationRecordDialog(AppDatabase *database,
    AppDatabase::WorkRecord record, const QString &location, QWidget *parent)
    : QDialog(parent), database_(database), record_(std::move(record))
{
    setObjectName(QStringLiteral("collaborationRecordDialog"));
    setWindowTitle(record_.id > 0 ? QStringLiteral("协作记录") : QStringLiteral("添加协作记录"));
    setWindowFlag(Qt::WindowMaximizeButtonHint);
    resize(880, 660);
    setMinimumSize(560, 400);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(12);
    auto *locationLabel = new QLabel(QStringLiteral("位置：%1").arg(location), this);
    locationLabel->setObjectName(QStringLiteral("recordLocation"));
    locationLabel->setTextFormat(Qt::PlainText);
    locationLabel->setWordWrap(true);
    layout->addWidget(locationLabel);
    title_ = addSummaryField(layout, this, QStringLiteral("workRecordTitle"), QStringLiteral("标题"));
    auto *toolbar = new QHBoxLayout;
    sections_ = new QTabBar(this);
    sections_->setObjectName(QStringLiteral("recordSectionTabs"));
    sections_->setAccessibleName(QStringLiteral("协作记录内容"));
    sections_->setExpanding(false);
    sections_->addTab(QStringLiteral("摘要"));
    sections_->addTab(QStringLiteral("正文"));
    sections_->setStyleSheet(QStringLiteral(
        "QTabBar::tab { font-size: 14px; min-width: 40px; min-height: 20px; padding: 8px 12px; margin-right: 4px; border: 1px solid #d4deeb; border-radius: 6px; background: #f5f7fb; color: #526176; }"
        "QTabBar::tab:selected { background: #e8f0ff; color: #2456a6; border-color: #9bbbe8; font-weight: 600; }"
        "QTabBar::tab:hover { background: #edf3ff; }"));
    toolbar->addWidget(sections_);
    toolbar->addStretch();
    const auto action = [this, toolbar](const QString &text, const QString &name) {
        auto *button = new QPushButton(text, this);
        button->setObjectName(name);
        button->setAutoDefault(false);
        toolbar->addWidget(button);
        return button;
    };
    copy_ = action(QStringLiteral("复制正文"), QStringLiteral("copyRecordBody"));
    edit_ = action(QStringLiteral("编辑"), QStringLiteral("editRecord"));
    cancel_ = action(QStringLiteral("取消编辑"), QStringLiteral("cancelRecordEdit"));
    save_ = action(QStringLiteral("保存"), QStringLiteral("saveRecord"));
    save_->setToolTip(QStringLiteral("保存标题、摘要和正文（Ctrl+S）"));
    layout->addLayout(toolbar);

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
    bindPersistentTextZoom(summary_, database_, QStringLiteral("text_zoom.work_record_summary"));
    bindPersistentTextZoom(body_, database_, QStringLiteral("text_zoom.work_record"));
    pages_->addWidget(summary_);
    pages_->addWidget(body_);

    emptySummary_ = new QWidget(pages_);
    emptySummary_->setObjectName(QStringLiteral("emptyRecordSummary"));
    auto *empty = new QVBoxLayout(emptySummary_);
    empty->addStretch();
    auto *emptyTitle = new QLabel(QStringLiteral("暂无摘要"), emptySummary_);
    emptyTitle->setObjectName(QStringLiteral("emptyStateTitle"));
    emptyTitle->setAlignment(Qt::AlignCenter);
    empty->addWidget(emptyTitle);
    auto *emptyActions = new QHBoxLayout;
    emptyActions->addStretch();
    auto *write = new QPushButton(QStringLiteral("填写摘要"), emptySummary_);
    auto *read = new QPushButton(QStringLiteral("查看正文"), emptySummary_);
    write->setAutoDefault(false); read->setAutoDefault(false);
    emptyActions->addWidget(write); emptyActions->addWidget(read);
    emptyActions->addStretch();
    empty->addLayout(emptyActions);
    empty->addStretch();
    pages_->addWidget(emptySummary_);
    layout->addWidget(pages_, 1);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("recordSaveStatus"));
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);

    title_->setText(record_.id > 0 ? summaryText(record_.title, record_.content) : QString());
    summary_->setPlainText(record_.summary);
    body_->setPlainText(record_.content);
    rememberDraft();
    connect(sections_, &QTabBar::currentChanged, this, [this] { showSection(); });
    connect(edit_, &QPushButton::clicked, this, [this] { setEditing(true); });
    connect(write, &QPushButton::clicked, this, [this] { setEditing(true); summary_->setFocus(); });
    connect(read, &QPushButton::clicked, this, [this] { sections_->setCurrentIndex(1); });
    connect(save_, &QPushButton::clicked, this, [this] { save(); });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (record_.id <= 0) { QDialog::reject(); return; }
        resetDraft();
        setEditing(false);
    });
    connect(copy_, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(bodyValue());
        copy_->setText(QStringLiteral("已复制"));
        QTimer::singleShot(1800, this, [this] { copy_->setText(QStringLiteral("复制正文")); });
    });
    const auto changed = [this] { error_.clear(); updateActions(); };
    connect(title_, &QLineEdit::textChanged, this, changed);
    connect(summary_, &HeadingTextEdit::textChanged, this, [this, changed] {
        draftSummaryText_ = summary_->toPlainText(); changed();
    });
    connect(body_, &HeadingTextEdit::textChanged, this, [this, changed] {
        draftBodyText_ = body_->toPlainText(); changed();
    });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, [this] { if (editing_) save(); });
    setEditing(record_.id <= 0);
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
}

void CollaborationRecordDialog::setEditing(bool editing)
{
    editing_ = editing;
    title_->setReadOnly(!editing);
    edit_->setVisible(!editing);
    copy_->setVisible(!editing);
    save_->setVisible(editing);
    cancel_->setVisible(editing);
    showSection();
    updateActions();
    if (editing_) (sections_->currentIndex() == 0 ? summary_ : body_)->setFocus();
}

void CollaborationRecordDialog::showSection()
{
    if (sections_->currentIndex() == 0 && !editing_ && summaryValue().trimmed().isEmpty()) {
        pages_->setCurrentWidget(emptySummary_);
        return;
    }
    auto *editor = sections_->currentIndex() == 0 ? summary_ : body_;
    editor->setPreviewMode(!editing_);
    pages_->setCurrentWidget(editor);
}

void CollaborationRecordDialog::updateActions()
{
    const bool valid = !title_->text().trimmed().isEmpty() && !draftBodyText_.trimmed().isEmpty();
    save_->setEnabled(editing_ && valid && (record_.id <= 0 || dirty()));
    copy_->setEnabled(!bodyValue().isEmpty());
    if (!error_.isEmpty()) status_->setText(error_);
    else if (editing_ && !valid) status_->setText(QStringLiteral("请填写标题和正文，摘要可留空。"));
    else if (dirty()) status_->setText(QStringLiteral("有未保存的修改"));
    else if (record_.updatedAt.isValid()) status_->setText(QStringLiteral("更新于 %1").arg(record_.updatedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
    else status_->clear();
}

bool CollaborationRecordDialog::save()
{
    if (title_->text().trimmed().isEmpty() || draftBodyText_.trimmed().isEmpty()) {
        setEditing(true);
        if (title_->text().trimmed().isEmpty()) title_->setFocus();
        else { sections_->setCurrentIndex(1); body_->setFocus(); }
        return false;
    }
    if (!dirty() && record_.id > 0) { setEditing(false); return true; }
    auto draft = record_;
    draft.title = title_->text().trimmed();
    draft.summary = summaryValue();
    draft.content = bodyValue();
    QString error;
    if (!database_->saveWorkRecord(&draft, &error)) {
        error_ = QStringLiteral("保存失败：%1").arg(error);
        updateActions();
        return false;
    }
    record_ = draft;
    saved_ = true;
    error_.clear();
    rememberDraft();
    setWindowTitle(QStringLiteral("协作记录"));
    setEditing(false);
    return true;
}

void CollaborationRecordDialog::reject()
{
    if (dirty()) {
        QMessageBox prompt(QMessageBox::Question, QStringLiteral("未保存的修改"),
            QStringLiteral("要保存这份协作记录的修改吗？"), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
        prompt.button(QMessageBox::Save)->setText(QStringLiteral("保存"));
        prompt.button(QMessageBox::Discard)->setText(QStringLiteral("放弃修改"));
        prompt.button(QMessageBox::Cancel)->setText(QStringLiteral("继续编辑"));
        prompt.setDefaultButton(QMessageBox::Cancel);
        const int choice = prompt.exec();
        if (choice == QMessageBox::Cancel || (choice == QMessageBox::Save && !save())) return;
    }
    QDialog::reject();
}
