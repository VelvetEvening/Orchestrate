#include "calendarpage.h"
#include "widgets/headingtext.h"
#include "widgets/headingtextedit.h"
#include "widgets/workspacecalendar.h"
#include "widgets/summaryfield.h"

#include <QAbstractItemView>
#include <QCalendarWidget>
#include <QColor>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSystemTrayIcon>
#include <QTextCharFormat>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QFrame *makeCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("card"));
    card->setFrameShape(QFrame::StyledPanel);
    card->setFrameShadow(QFrame::Plain);
    return card;
}

QLabel *makeTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("pageTitle"));
    return label;
}

QLabel *makeCardTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("cardTitle"));
    return label;
}

void showDatabaseError(QWidget *parent, const QString &message)
{
    QMessageBox::critical(parent, QStringLiteral("数据操作失败"), message);
}

QString repeatLabel(const QString &mode)
{
    if (mode == QStringLiteral("daily")) return QStringLiteral("每天");
    if (mode == QStringLiteral("weekly")) return QStringLiteral("每周");
    if (mode == QStringLiteral("monthly")) return QStringLiteral("每月");
    return QString();
}

} // namespace

CalendarPage::CalendarPage(AppDatabase *database,
                           QSystemTrayIcon *trayIcon,
                           QWidget *parent)
    : QWidget(parent)
    , database_(database)
    , trayIcon_(trayIcon)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(34, 30, 34, 34);
    layout->setSpacing(18);

    layout->addWidget(makeTitle(QStringLiteral("时序记录"), this));


    auto *contentLayout = new QHBoxLayout;
    contentLayout->setSpacing(18);

    auto *calendarCard = makeCard(this);
    auto *calendarLayout = new QVBoxLayout(calendarCard);
    calendarLayout->setContentsMargins(18, 18, 18, 18);
    calendarLayout->setSpacing(10);
    auto *calendarHeader = new QHBoxLayout;
    calendarHeader->addWidget(makeCardTitle(QStringLiteral("日历"), calendarCard),1);
    auto *diaryLegend = new QLabel(QStringLiteral("■ 日记"),calendarCard);
    diaryLegend->setStyleSheet(QStringLiteral("color: #168b79;"));
    auto *reminderLegend = new QLabel(QStringLiteral("▲ 提醒"),calendarCard);
    reminderLegend->setStyleSheet(QStringLiteral("color: #b57812;"));
    calendarHeader->addWidget(diaryLegend);
    calendarHeader->addSpacing(10);
    calendarHeader->addWidget(reminderLegend);
    calendarLayout->addLayout(calendarHeader);

    calendar_ = new WorkspaceCalendar(calendarCard);
    QPalette calendarPalette = calendar_->palette();
    calendarPalette.setColor(QPalette::Highlight, QColor(QStringLiteral("#3b82d6")));
    calendarPalette.setColor(QPalette::HighlightedText, Qt::white);
    calendar_->setPalette(calendarPalette);
    calendar_->setGridVisible(false);
    calendar_->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    calendar_->setFirstDayOfWeek(Qt::Monday);
    calendar_->setSelectedDate(QDate::currentDate());
    calendarLayout->addWidget(calendar_);
    contentLayout->addWidget(calendarCard, 3);

    auto *detailsCard = makeCard(this);
    auto *detailsLayout = new QVBoxLayout(detailsCard);
    detailsLayout->setContentsMargins(20, 18, 20, 18);
    detailsLayout->setSpacing(10);

    detailsLayout->addWidget(makeCardTitle(QStringLiteral("当天记录"), detailsCard));
    selectedDateLabel_ = new QLabel(detailsCard);
    selectedDateLabel_->setObjectName(QStringLiteral("muted"));
    detailsLayout->addWidget(selectedDateLabel_);
    summaryLabel_ = new QLabel(detailsCard);
    summaryLabel_->setObjectName(QStringLiteral("muted"));
    detailsLayout->addWidget(summaryLabel_);

    detailsLayout->addWidget(makeCardTitle(QStringLiteral("提醒事项"), detailsCard));
    reminderList_ = new QListWidget(detailsCard);
    reminderList_->setObjectName(QStringLiteral("reminderList"));
    reminderList_->setAlternatingRowColors(true);
    reminderList_->setSelectionMode(QAbstractItemView::SingleSelection);
    reminderList_->setMinimumHeight(110);
    detailsLayout->addWidget(reminderList_, 2);

    auto *reminderButtonRow = new QHBoxLayout;
    auto *addReminderButton = new QPushButton(QStringLiteral("添加提醒"), detailsCard);
    addReminderButton->setObjectName(QStringLiteral("primaryButton"));
    reminderButtonRow->addWidget(addReminderButton);
    editReminderButton_ = new QPushButton(QStringLiteral("编辑"), detailsCard);
    editReminderButton_->setEnabled(false);
    reminderButtonRow->addWidget(editReminderButton_);
    completeReminderButton_ = new QPushButton(QStringLiteral("完成"), detailsCard);
    completeReminderButton_->setEnabled(false);
    reminderButtonRow->addWidget(completeReminderButton_);
    deleteReminderButton_ = new QPushButton(QStringLiteral("删除"), detailsCard);
    deleteReminderButton_->setEnabled(false);
    reminderButtonRow->addWidget(deleteReminderButton_);
    reminderButtonRow->addStretch(1);
    detailsLayout->addLayout(reminderButtonRow);

    auto *diaryHeader = new QHBoxLayout;
    diaryHeader->addWidget(makeCardTitle(QStringLiteral("日记"),detailsCard),1);
    auto *collectionButton = new QPushButton(QStringLiteral("日记集合"),detailsCard);
    collectionButton->setObjectName(QStringLiteral("diaryCollectionButton"));
    diaryHeader->addWidget(collectionButton);
    detailsLayout->addLayout(diaryHeader);
    connect(collectionButton,&QPushButton::clicked,this,&CalendarPage::openDiaryCollection);
    diaryList_ = new QListWidget(detailsCard);
    diaryList_->setObjectName(QStringLiteral("diaryList"));
    diaryList_->setSpacing(4);
    diaryList_->setTextElideMode(Qt::ElideRight);
    diaryList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    diaryList_->setAlternatingRowColors(true);
    diaryList_->setSelectionMode(QAbstractItemView::SingleSelection);
    diaryList_->setMinimumHeight(110);
    detailsLayout->addWidget(diaryList_, 3);

    auto *diaryButtonRow = new QHBoxLayout;
    auto *addDiaryButton = new QPushButton(QStringLiteral("添加日记"), detailsCard);
    addDiaryButton->setObjectName(QStringLiteral("primaryButton"));
    diaryButtonRow->addWidget(addDiaryButton);
    editButton_ = new QPushButton(QStringLiteral("编辑"), detailsCard);
    editButton_->setEnabled(false);
    diaryButtonRow->addWidget(editButton_);
    deleteButton_ = new QPushButton(QStringLiteral("删除"), detailsCard);
    deleteButton_->setEnabled(false);
    diaryButtonRow->addWidget(deleteButton_);
    diaryButtonRow->addStretch(1);
    detailsLayout->addLayout(diaryButtonRow);

    contentLayout->addWidget(detailsCard, 2);
    layout->addLayout(contentLayout, 1);

    connect(calendar_, &QCalendarWidget::selectionChanged, this, [this] {
        updateSelectedDate(calendar_->selectedDate());
    });
    connect(calendar_, &QCalendarWidget::currentPageChanged, this,
            [this](int, int) { updateCalendarAppearance(); });
    connect(addReminderButton, &QPushButton::clicked, this, &CalendarPage::addReminder);
    connect(editReminderButton_, &QPushButton::clicked, this, &CalendarPage::editReminder);
    connect(completeReminderButton_, &QPushButton::clicked, this, &CalendarPage::completeReminder);
    connect(deleteReminderButton_, &QPushButton::clicked, this, &CalendarPage::deleteReminder);
    connect(reminderList_, &QListWidget::itemSelectionChanged, this, [this] {
        const bool hasSelection = reminderList_->currentItem()
            && reminderList_->currentItem()->data(Qt::UserRole).isValid();
        editReminderButton_->setEnabled(hasSelection);
        completeReminderButton_->setEnabled(hasSelection
                                             && !reminderList_->currentItem()->data(Qt::UserRole + 1).toBool());
        deleteReminderButton_->setEnabled(hasSelection);
    });
    connect(reminderList_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { editReminder(); });

    connect(addDiaryButton, &QPushButton::clicked, this, &CalendarPage::addDiaryEntry);
    connect(editButton_, &QPushButton::clicked, this, &CalendarPage::editDiaryEntry);
    connect(deleteButton_, &QPushButton::clicked, this, &CalendarPage::deleteDiaryEntry);
    connect(diaryList_, &QListWidget::itemSelectionChanged, this, [this] {
        const bool hasSelection = diaryList_->currentItem()
            && diaryList_->currentItem()->data(Qt::UserRole).isValid();
        editButton_->setEnabled(hasSelection);
        deleteButton_->setEnabled(hasSelection);
    });
    connect(diaryList_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { editDiaryEntry(); });

    reminderTimer_ = new QTimer(this);
    reminderTimer_->setInterval(30000);
    connect(reminderTimer_, &QTimer::timeout, this, &CalendarPage::checkDueReminders);
    reminderTimer_->start();

    updateSelectedDate(calendar_->selectedDate());
    updateCalendarAppearance();
    checkDueReminders();
}

void CalendarPage::updateSelectedDate(const QDate &date)
{
    selectedDateLabel_->setText(date.toString(QStringLiteral("yyyy 年 MM 月 dd 日")));
    refreshReminderList();
    refreshDiaryList();
}

void CalendarPage::updateCalendarAppearance()
{
    if (calendar_ == nullptr || database_ == nullptr) {
        return;
    }

    const QDate firstDay(calendar_->yearShown(),calendar_->monthShown(),1);
    QString error;
    const auto dates=database_->diaryDates(firstDay.addMonths(-1),firstDay.addMonths(2).addDays(-1),&error);
    if (!error.isEmpty()) return;
    QSet<QDate> diaryDates(dates.begin(),dates.end());
    QSet<QDate> reminderDates;
    for (int offset=-1; offset<=1; ++offset) {
        const QDate month=firstDay.addMonths(offset);
        const auto reminders=database_->remindersInMonth(month.year(),month.month(),&error);
        if (!error.isEmpty()) return;
        for (const auto &reminder:reminders) reminderDates.insert(reminder.remindAt.date());
    }
    static_cast<WorkspaceCalendar *>(calendar_)->setRecordDates(diaryDates,reminderDates);
    QTextCharFormat todayFormat;
    todayFormat.setBackground(QColor(QStringLiteral("#60a5fa")));
    calendar_->setDateTextFormat(QDate::currentDate(),todayFormat);
}

void CalendarPage::refreshReminderList()
{
    reminderList_->clear();
    editReminderButton_->setEnabled(false);
    completeReminderButton_->setEnabled(false);
    deleteReminderButton_->setEnabled(false);
    if (database_ == nullptr) {
        return;
    }

    QString error;
    const QList<AppDatabase::Reminder> reminders = database_->reminders(calendar_->selectedDate(), &error);
    if (!error.isEmpty()) {
        showDatabaseError(this, error);
        return;
    }
    for (const AppDatabase::Reminder &reminder : reminders) {
        const QString time = reminder.remindAt.time().toString(QStringLiteral("HH:mm"));
        const QString repeat = repeatLabel(reminder.repeatMode);
        QString text = QStringLiteral("%1  %2").arg(time, reminder.title);
        if (!repeat.isEmpty()) {
            text += QStringLiteral("（%1）").arg(repeat);
        }
        if (reminder.completed) {
            text.prepend(QStringLiteral("✓ "));
        }
        auto *item = new QListWidgetItem(text, reminderList_);
        item->setData(Qt::UserRole, reminder.id);
        item->setData(Qt::UserRole + 1, reminder.completed);
        item->setToolTip(HeadingText::toolTipHtml(reminder.content));
        if (reminder.completed) {
            QFont font = item->font();
            font.setStrikeOut(true);
            item->setFont(font);
            item->setForeground(QColor(QStringLiteral("#94a3b8")));
        }
    }
    summaryLabel_->setText(QStringLiteral("提醒 %1 条 · 日记 %2 条")
                               .arg(reminderList_->count())
                               .arg(diaryList_ == nullptr ? 0 : diaryList_->count()));
}

AppDatabase::Reminder CalendarPage::selectedReminder() const
{
    AppDatabase::Reminder result;
    if (database_ == nullptr || reminderList_ == nullptr || reminderList_->currentItem() == nullptr) {
        return result;
    }
    const int id = reminderList_->currentItem()->data(Qt::UserRole).toInt();
    if (id <= 0) {
        return result;
    }
    const QList<AppDatabase::Reminder> reminders = database_->reminders(calendar_->selectedDate());
    for (const AppDatabase::Reminder &reminder : reminders) {
        if (reminder.id == id) {
            return reminder;
        }
    }
    return result;
}

bool CalendarPage::showReminderDialog(AppDatabase::Reminder *reminder, bool editing)
{
    if (database_ == nullptr || reminder == nullptr) {
        return false;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(editing ? QStringLiteral("编辑提醒") : QStringLiteral("添加提醒"));
    dialog.setWindowFlag(Qt::WindowMaximizeButtonHint);
    dialog.setMinimumWidth(560);
    dialog.resize(640, 560);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;

    auto *titleEdit = new QLineEdit(&dialog);
    titleEdit->setObjectName(QStringLiteral("reminderTitle"));
    titleEdit->setText(reminder->title);
    titleEdit->setPlaceholderText(QStringLiteral("例如：提交论文初稿"));
    form->addRow(QStringLiteral("标题："), titleEdit);

    auto *contentEdit = new HeadingTextEdit(&dialog);
    contentEdit->setObjectName(QStringLiteral("reminderContent"));
    contentEdit->setPlainText(reminder->content);
    contentEdit->setPlaceholderText(QStringLiteral("可选：补充说明"));
    form->addRow(QStringLiteral("说明："), contentEdit);

    auto *dateTimeEdit = new QDateTimeEdit(&dialog);
    dateTimeEdit->setObjectName(QStringLiteral("reminderTime"));
    dateTimeEdit->setCalendarPopup(true);
    dateTimeEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    if (editing && reminder->remindAt.isValid()) {
        dateTimeEdit->setDateTime(reminder->remindAt);
    } else {
        dateTimeEdit->setDateTime(QDateTime(calendar_->selectedDate(), QTime(9, 0)));
    }
    form->addRow(QStringLiteral("提醒时间："), dateTimeEdit);

    auto *repeatCombo = new QComboBox(&dialog);
    repeatCombo->setObjectName(QStringLiteral("reminderRepeat"));
    repeatCombo->addItem(QStringLiteral("不重复"), QStringLiteral("none"));
    repeatCombo->addItem(QStringLiteral("每天"), QStringLiteral("daily"));
    repeatCombo->addItem(QStringLiteral("每周"), QStringLiteral("weekly"));
    repeatCombo->addItem(QStringLiteral("每月"), QStringLiteral("monthly"));
    const int repeatIndex = repeatCombo->findData(reminder->repeatMode.isEmpty()
                                                       ? QStringLiteral("none")
                                                       : reminder->repeatMode);
    repeatCombo->setCurrentIndex(repeatIndex >= 0 ? repeatIndex : 0);
    form->addRow(QStringLiteral("重复："), repeatCombo);

    auto *projectCombo = new QComboBox(&dialog);
    projectCombo->setObjectName(QStringLiteral("reminderProject"));
    projectCombo->addItem(QStringLiteral("不关联项目"), 0);
    QString projectError;
    const QList<AppDatabase::Project> projects = database_->projects(&projectError);
    if (!projectError.isEmpty()) {
        showDatabaseError(this, projectError);
        return false;
    }
    for (const AppDatabase::Project &project : projects) {
        projectCombo->addItem(project.name, project.id);
    }
    const int projectIndex = projectCombo->findData(reminder->projectId);
    projectCombo->setCurrentIndex(projectIndex >= 0 ? projectIndex : 0);
    form->addRow(QStringLiteral("关联项目："), projectCombo);

    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto setMode = [=](bool preview) {
        titleEdit->setReadOnly(preview);
        dateTimeEdit->setReadOnly(preview);
        repeatCombo->setEnabled(!preview);
        projectCombo->setEnabled(!preview);
    };
    connect(contentEdit, &HeadingTextEdit::previewModeChanged, &dialog, setMode);
    contentEdit->setPreviewMode(editing);
    setMode(editing);
    auto validate = [=] {
        const bool changed = titleEdit->text().trimmed() != reminder->title
            || contentEdit->toPlainText() != reminder->content
            || dateTimeEdit->dateTime() != reminder->remindAt
            || repeatCombo->currentData().toString() != reminder->repeatMode
            || projectCombo->currentData().toInt() != reminder->projectId;
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!titleEdit->text().trimmed().isEmpty()
                                                         && (!editing || changed));
    };
    connect(titleEdit, &QLineEdit::textChanged, &dialog, validate);
    connect(contentEdit, &HeadingTextEdit::textChanged, &dialog, validate);
    connect(dateTimeEdit, &QDateTimeEdit::dateTimeChanged, &dialog, validate);
    connect(repeatCombo, &QComboBox::currentIndexChanged, &dialog, validate);
    connect(projectCombo, &QComboBox::currentIndexChanged, &dialog, validate);
    validate();

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    if (titleEdit->text().trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("缺少标题"), QStringLiteral("请填写提醒标题。"));
        return false;
    }

    reminder->title = titleEdit->text().trimmed();
    reminder->content = contentEdit->toPlainText();
    reminder->remindAt = dateTimeEdit->dateTime();
    reminder->repeatMode = repeatCombo->currentData().toString();
    reminder->projectId = projectCombo->currentData().toInt();
    reminder->completed = false;
    reminder->lastNotifiedAt = QDateTime();
    return true;
}

void CalendarPage::addReminder()
{
    AppDatabase::Reminder reminder;
    if (!showReminderDialog(&reminder, false)) {
        return;
    }
    QString error;
    if (!database_->addReminder(reminder, nullptr, &error)) {
        showDatabaseError(this, error);
        return;
    }
    refreshReminderList();
    updateCalendarAppearance();
}

void CalendarPage::editReminder()
{
    AppDatabase::Reminder reminder = selectedReminder();
    if (reminder.id <= 0 || !showReminderDialog(&reminder, true)) {
        return;
    }
    QString error;
    if (!database_->updateReminder(reminder, &error)) {
        showDatabaseError(this, error);
        return;
    }
    updateSelectedDate(calendar_->selectedDate());
    updateCalendarAppearance();
}

void CalendarPage::completeReminder()
{
    const AppDatabase::Reminder reminder = selectedReminder();
    if (reminder.id <= 0) {
        return;
    }
    QString error;
    if (!database_->completeReminder(reminder.id, QDateTime::currentDateTime(), &error)) {
        showDatabaseError(this, error);
        return;
    }
    updateSelectedDate(calendar_->selectedDate());
    updateCalendarAppearance();
}

void CalendarPage::deleteReminder()
{
    const AppDatabase::Reminder reminder = selectedReminder();
    if (reminder.id <= 0) {
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("删除提醒"),
                              QStringLiteral("确定删除“%1”吗？").arg(reminder.title),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!database_->deleteReminder(reminder.id, &error)) {
        showDatabaseError(this, error);
        return;
    }
    updateSelectedDate(calendar_->selectedDate());
    updateCalendarAppearance();
}

void CalendarPage::checkDueReminders()
{
    if (database_ == nullptr) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    QString error;
    const QList<AppDatabase::Reminder> due = database_->dueReminders(now, &error);
    if (!error.isEmpty()) {
        return;
    }
    for (const AppDatabase::Reminder &reminder : due) {
        if (!database_->processDueReminder(reminder.id, now, &error)) {
            continue;
        }
        QString message = reminder.title;
        if (!reminder.content.trimmed().isEmpty()) {
            message += QStringLiteral("\n") + reminder.content.trimmed();
        }
        if (trayIcon_ != nullptr && trayIcon_->isVisible()) {
            trayIcon_->showMessage(QStringLiteral("Orchestrate 提醒"), message,
                                   QSystemTrayIcon::Information, 8000);
        }
    }
    if (!due.isEmpty()) {
        updateSelectedDate(calendar_->selectedDate());
        updateCalendarAppearance();
    }
}

void CalendarPage::refreshDiaryList()
{
    diaryList_->clear();
    editButton_->setEnabled(false);
    deleteButton_->setEnabled(false);
    if (database_ == nullptr) {
        return;
    }

    QString error;
    const QList<AppDatabase::DiaryEntry> entries = database_->diaryEntries(
        calendar_->selectedDate(), &error);
    if (!error.isEmpty()) {
        showDatabaseError(this, error);
        return;
    }
    if (entries.isEmpty()) {
        auto *item = new QListWidgetItem(QStringLiteral("这一天还没有日记"), diaryList_);
        item->setForeground(QColor(QStringLiteral("#94a3b8")));
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsEnabled);
    } else {
        for (const AppDatabase::DiaryEntry &entry : entries) {
            auto *item = new QListWidgetItem(summaryText(entry.title,entry.content), diaryList_);
            item->setSizeHint(QSize(0,42));
            item->setData(Qt::UserRole, entry.id);
            item->setToolTip(QStringLiteral("创建：%1\n更新：%2")
                                 .arg(entry.createdAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")),
                                      entry.updatedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        }
    }
    summaryLabel_->setText(QStringLiteral("提醒 %1 条 · 日记 %2 条")
                               .arg(reminderList_ == nullptr ? 0 : reminderList_->count())
                               .arg(entries.size()));
}

AppDatabase::DiaryEntry CalendarPage::selectedEntry() const
{
    AppDatabase::DiaryEntry result;
    if (diaryList_ == nullptr || diaryList_->currentItem() == nullptr || database_ == nullptr) {
        return result;
    }
    const int id = diaryList_->currentItem()->data(Qt::UserRole).toInt();
    if (id <= 0) {
        return result;
    }
    const QList<AppDatabase::DiaryEntry> entries = database_->diaryEntries(calendar_->selectedDate());
    for (const AppDatabase::DiaryEntry &entry : entries) {
        if (entry.id == id) {
            return entry;
        }
    }
    return result;
}

void CalendarPage::addDiaryEntry()
{
    if (!database_) return;
    AppDatabase::DiaryEntry entry;
    entry.date=calendar_->selectedDate();
    diaryDialog(entry,true);
}

void CalendarPage::editDiaryEntry()
{
    const auto entry=selectedEntry();
    if (entry.id>0) diaryDialog(entry,false);
}

void CalendarPage::diaryDialog(AppDatabase::DiaryEntry entry, bool creating, QWidget *owner)
{
    QDialog dialog(owner ? owner : this);
    dialog.setWindowTitle(creating ? QStringLiteral("添加日记") : QStringLiteral("编辑日记"));
    dialog.setWindowFlag(Qt::WindowMaximizeButtonHint);
    dialog.resize(800,620);
    dialog.setMinimumSize(480,360);
    auto *layout=new QVBoxLayout(&dialog);
    auto *dateLabel=new QLabel(entry.date.toString(QStringLiteral("yyyy 年 MM 月 dd 日")),&dialog);
    dateLabel->setObjectName(QStringLiteral("muted"));
    layout->addWidget(dateLabel);
    auto *title=addSummaryField(layout,&dialog,QStringLiteral("diaryTitle"));
    auto *editor=new HeadingTextEdit(&dialog);
    layout->addWidget(editor,1);
    if (!creating) {
        title->setText(summaryText(entry.title,entry.content));
        editor->setPlainText(entry.content);
    }
    connect(editor,&HeadingTextEdit::previewModeChanged,title,&QLineEdit::setReadOnly);
    editor->setPreviewMode(!creating);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);
    auto *save=buttons->button(QDialogButtonBox::Save);
    save->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    auto validate=[=] {
        const bool changed=title->text().trimmed()!=summaryText(entry.title,entry.content) || editor->toPlainText()!=entry.content;
        save->setEnabled(!title->text().trimmed().isEmpty() && !editor->toPlainText().trimmed().isEmpty() && (creating || changed));
    };
    connect(title,&QLineEdit::textChanged,&dialog,validate);
    connect(editor,&HeadingTextEdit::textChanged,&dialog,validate);
    validate();
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        QString error;
        const bool ok=creating
            ? database_->addDiaryEntry(entry.date,title->text().trimmed(),editor->toPlainText(),&error)
            : database_->updateDiaryEntry(entry.id,title->text().trimmed(),editor->toPlainText(),&error);
        if (!ok) { showDatabaseError(&dialog,error); return; }
        dialog.accept();
        refreshDiaryList();
        refreshDiaryCollection();
        updateCalendarAppearance();
    });
    if (creating) title->setFocus();
    else editor->setFocus();
    dialog.exec();
}

void CalendarPage::openDiaryCollection()
{
    if (!diaryCollection_) {
        diaryCollection_=new QDialog(this,Qt::Window);
        diaryCollection_->setObjectName(QStringLiteral("diaryCollection"));
        diaryCollection_->setWindowTitle(QStringLiteral("日记集合"));
        diaryCollection_->setWindowFlag(Qt::WindowMaximizeButtonHint);
        diaryCollection_->resize(860,680);
        diaryCollection_->setMinimumSize(480,360);
        auto *layout=new QVBoxLayout(diaryCollection_);
        layout->setContentsMargins(24,24,24,24);
        collectionSearch_=new QLineEdit(diaryCollection_);
        collectionSearch_->setObjectName(QStringLiteral("diaryCollectionSearch"));
        collectionSearch_->setPlaceholderText(QStringLiteral("搜索简介或日期"));
        collectionSearch_->setClearButtonEnabled(true);
        layout->addWidget(collectionSearch_);
        collectionList_=new QListWidget(diaryCollection_);
        collectionList_->setObjectName(QStringLiteral("diaryCollectionList"));
        collectionList_->setSpacing(4);
        collectionList_->setTextElideMode(Qt::ElideRight);
        collectionList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        collectionList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        layout->addWidget(collectionList_,1);
        collectionCount_=new QLabel(diaryCollection_);
        collectionCount_->setObjectName(QStringLiteral("muted"));
        layout->addWidget(collectionCount_);
        connect(collectionSearch_,&QLineEdit::textChanged,this,&CalendarPage::filterDiaryCollection);
        auto openCurrent=[this] {
            auto *item=collectionList_->currentItem();
            if (!item || item->isHidden()) return;
            QString error;
            const auto entry=database_->diaryEntry(item->data(Qt::UserRole).toInt(),&error);
            if (!error.isEmpty()) { showDatabaseError(diaryCollection_,error); return; }
            if (entry.id>0) diaryDialog(entry,false,diaryCollection_);
        };
        connect(collectionList_,&QListWidget::itemDoubleClicked,this,[openCurrent]{ openCurrent(); });
        auto *open=new QPushButton(QStringLiteral("查看日记"),diaryCollection_);
        layout->addWidget(open,0,Qt::AlignRight);
        connect(open,&QPushButton::clicked,this,openCurrent);
    }
    refreshDiaryCollection();
    if (diaryCollection_->isMinimized()) diaryCollection_->showNormal();
    else diaryCollection_->show();
    diaryCollection_->raise();
    diaryCollection_->activateWindow();
}

void CalendarPage::refreshDiaryCollection()
{
    if (!diaryCollection_) return;
    const int selected=collectionList_->currentItem() ? collectionList_->currentItem()->data(Qt::UserRole).toInt() : 0;
    QString error;
    const auto entries=database_->allDiaryEntries(&error);
    if (!error.isEmpty()) { showDatabaseError(diaryCollection_,error); return; }
    collectionList_->clear();
    for (const auto &entry:entries) {
        const QString text=entry.date.toString(QStringLiteral("yyyy-MM-dd"))+QStringLiteral("  ")
            +entry.createdAt.toLocalTime().toString(QStringLiteral("HH:mm"))+QStringLiteral("   ·   ")
            +summaryText(entry.title,entry.content);
        auto *item=new QListWidgetItem(text,collectionList_);
        item->setSizeHint(QSize(0,48));
        item->setData(Qt::UserRole,entry.id);
        item->setToolTip(text);
        if (entry.id==selected) collectionList_->setCurrentItem(item);
    }
    filterDiaryCollection();
}

void CalendarPage::filterDiaryCollection()
{
    if (!collectionList_) return;
    int visible=0;
    for (int i=0;i<collectionList_->count();++i) {
        auto *item=collectionList_->item(i);
        const bool match=item->text().contains(collectionSearch_->text().trimmed(),Qt::CaseInsensitive);
        item->setHidden(!match);
        if (match) ++visible;
    }
    collectionCount_->setText(visible==0 ? (collectionList_->count()==0 ? QStringLiteral("暂无日记") : QStringLiteral("没有匹配的日记"))
                                        : QStringLiteral("%1 篇日记 · 最近日期在前").arg(visible));
}

void CalendarPage::deleteDiaryEntry()
{
    const AppDatabase::DiaryEntry entry = selectedEntry();
    if (entry.id <= 0) {
        return;
    }

    if (QMessageBox::question(
            this,
            QStringLiteral("删除日记"),
            QStringLiteral("确定删除选中的日记吗？"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!database_->deleteDiaryEntry(entry.id, &error)) {
        showDatabaseError(this, error);
        return;
    }
    refreshDiaryList();
    refreshDiaryCollection();
    updateCalendarAppearance();
}
