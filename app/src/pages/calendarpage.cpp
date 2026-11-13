#include "ui/theme.h"
#include "ui/contentworkspace.h"
#include <QShortcut>
#include <QScrollBar>
#include "widgets/emptyviewhint.h"
#include "calendarpage.h"
#include "widgets/persistenttextzoom.h"
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

template<typename Open>
void bindListOpen(QListWidget *list, QObject *context, Open open)
{
    list->setToolTip(QStringLiteral("双击或按 Enter 打开"));
    list->setAccessibleDescription(QStringLiteral("双击或按 Enter 打开选中的内容。"));
    QObject::connect(list, &QListWidget::itemDoubleClicked, context, [open](QListWidgetItem *) { open(); });
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto *shortcut = new QShortcut(QKeySequence(key), list);
        shortcut->setContext(Qt::WidgetShortcut);
        QObject::connect(shortcut, &QShortcut::activated, context, open);
    }
}

QFrame *makeSurface(QWidget *parent, const char *surface)
{
    auto *card = new QFrame(parent);
    UiTheme::setSurface(card, surface);
    return card;
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
    UiTheme::setSurface(this, "content");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *contentLayout = new QHBoxLayout;
    contentLayout->setSpacing(0);

    auto *calendarCard = makeSurface(this, "index");
    calendarCard->setObjectName(QStringLiteral("workspaceIndex"));
    auto *calendarLayout = new QVBoxLayout(calendarCard);
    calendarLayout->setContentsMargins(24, 22, 24, 24);
    calendarLayout->setSpacing(10);
    auto *calendarHeader = new QHBoxLayout;
    calendarHeader->addWidget(makeCardTitle(QStringLiteral("日历"), calendarCard),1);
    auto *diaryLegend = new QLabel(QStringLiteral("■ 日记"),calendarCard);
    diaryLegend->setObjectName(QStringLiteral("diaryLegend"));
    auto *reminderLegend = new QLabel(QStringLiteral("▲ 提醒"),calendarCard);
    reminderLegend->setObjectName(QStringLiteral("reminderLegend"));
    calendarHeader->addWidget(diaryLegend);
    calendarHeader->addSpacing(10);
    calendarHeader->addWidget(reminderLegend);
    calendarLayout->addLayout(calendarHeader);

    calendar_ = new WorkspaceCalendar(calendarCard);
    QPalette calendarPalette = calendar_->palette();
    calendarPalette.setColor(QPalette::Highlight, QColor(UiTheme::Selection));
    calendarPalette.setColor(QPalette::HighlightedText, QColor(UiTheme::Accent));
    calendar_->setPalette(calendarPalette);
    calendar_->setGridVisible(false);
    calendar_->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    calendar_->setFirstDayOfWeek(Qt::Monday);
    calendar_->setSelectedDate(QDate::currentDate());
    calendarLayout->addWidget(calendar_);
    contentLayout->addWidget(calendarCard, 3);

    auto *detailsCard = makeSurface(this, "content");
    auto *detailsLayout = new QVBoxLayout(detailsCard);
    detailsLayout->setContentsMargins(24, 22, 24, 24);
    detailsLayout->setSpacing(10);

    detailsLayout->addWidget(makeCardTitle(QStringLiteral("提醒事项"), detailsCard));
    reminderList_ = new QListWidget(detailsCard);
    reminderList_->setObjectName(QStringLiteral("reminderList"));
    reminderList_->setAlternatingRowColors(false);
    reminderList_->setSelectionMode(QAbstractItemView::SingleSelection);
    reminderList_->setMinimumHeight(110);
    detailsLayout->addWidget(reminderList_, 2);

    auto *reminderButtonRow = new QHBoxLayout;
    auto *addReminderButton = new QPushButton(QStringLiteral("添加提醒"), detailsCard);
    addReminderButton->setObjectName(QStringLiteral("primaryButton"));
    reminderButtonRow->addWidget(addReminderButton);
    completeReminderButton_ = new QPushButton(QStringLiteral("完成"), detailsCard);
    UiTheme::setRole(completeReminderButton_, "quiet");
    completeReminderButton_->setEnabled(false);
    reminderButtonRow->addWidget(completeReminderButton_);
    deleteReminderButton_ = new QPushButton(QStringLiteral("删除"), detailsCard);
    deleteReminderButton_->setEnabled(false);
    UiTheme::setRole(deleteReminderButton_, "danger");
    reminderButtonRow->addWidget(deleteReminderButton_);
    reminderButtonRow->addStretch(1);
    detailsLayout->addLayout(reminderButtonRow);

    auto *diaryHeader = new QHBoxLayout;
    diaryHeader->addWidget(makeCardTitle(QStringLiteral("日记"),detailsCard),1);
    auto *collectionButton = new QPushButton(QStringLiteral("日记集合"),detailsCard);
    collectionButton->setObjectName(QStringLiteral("diaryCollectionButton"));
    UiTheme::setRole(collectionButton, "quiet");
    diaryHeader->addWidget(collectionButton);
    detailsLayout->addLayout(diaryHeader);
    connect(collectionButton,&QPushButton::clicked,this,&CalendarPage::openDiaryCollection);
    diaryList_ = new QListWidget(detailsCard);
    diaryList_->setObjectName(QStringLiteral("diaryList"));
    diaryList_->setSpacing(4);
    diaryList_->setTextElideMode(Qt::ElideRight);
    diaryList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    diaryList_->setAlternatingRowColors(false);
    diaryList_->setSelectionMode(QAbstractItemView::SingleSelection);
    diaryList_->setMinimumHeight(110);
    detailsLayout->addWidget(diaryList_, 3);

    auto *diaryButtonRow = new QHBoxLayout;
    auto *addDiaryButton = new QPushButton(QStringLiteral("添加日记"), detailsCard);
    addDiaryButton->setObjectName(QStringLiteral("primaryButton"));
    diaryButtonRow->addWidget(addDiaryButton);
    deleteButton_ = new QPushButton(QStringLiteral("删除"), detailsCard);
    deleteButton_->setEnabled(false);
    UiTheme::setRole(deleteButton_, "danger");
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
    connect(completeReminderButton_, &QPushButton::clicked, this, &CalendarPage::completeReminder);
    connect(deleteReminderButton_, &QPushButton::clicked, this, &CalendarPage::deleteReminder);
    connect(reminderList_, &QListWidget::itemSelectionChanged, this, [this] {
        const bool hasSelection = reminderList_->currentItem()
            && reminderList_->currentItem()->data(Qt::UserRole).isValid();
        completeReminderButton_->setEnabled(hasSelection
                                             && !reminderList_->currentItem()->data(Qt::UserRole + 1).toBool());
        deleteReminderButton_->setEnabled(hasSelection);
    });
    bindListOpen(reminderList_, this, [this] { editReminder(); });

    connect(addDiaryButton, &QPushButton::clicked, this, &CalendarPage::addDiaryEntry);
    connect(deleteButton_, &QPushButton::clicked, this, &CalendarPage::deleteDiaryEntry);
    connect(diaryList_, &QListWidget::itemSelectionChanged, this, [this] {
        const bool hasSelection = diaryList_->currentItem()
            && diaryList_->currentItem()->data(Qt::UserRole).isValid();
        deleteButton_->setEnabled(hasSelection);
    });
    bindListOpen(diaryList_, this, [this] { editDiaryEntry(); });

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
    const QString dateText = date.toString(QStringLiteral("yyyy 年 MM 月 dd 日"));
    reminderList_->setAccessibleName(dateText + QStringLiteral(" 提醒事项"));
    diaryList_->setAccessibleName(dateText + QStringLiteral(" 日记"));
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
    todayFormat.setBackground(QColor(UiTheme::Selection));
    calendar_->setDateTextFormat(QDate::currentDate(),todayFormat);
}

void CalendarPage::refreshReminderList()
{
    reminderList_->clear();
    completeReminderButton_->setEnabled(false);
    deleteReminderButton_->setEnabled(false);
    UiTheme::setRole(deleteReminderButton_, "danger");
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
            item->setForeground(QColor(UiTheme::Muted));
        }
    }
    EmptyViewHint::update(reminderList_, reminderList_->count() ? QString()
        : QStringLiteral("当天没有提醒\n点击“添加提醒”安排事项"));
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

void CalendarPage::showReminderDialog(AppDatabase::Reminder reminder, bool editing)
{
    auto *workspace=ContentWorkspace::find(this);
    const QString key=editing ? QStringLiteral("reminder/%1").arg(reminder.id)
        : QStringLiteral("reminder/new/%1").arg(calendar_->selectedDate().toString(Qt::ISODate));
    if (workspace && workspace->activate(key,this)) return;
    auto *dialog=new DraftDialog(this);
    dialog->setObjectName(QStringLiteral("reminderEditor"));
    dialog->setWindowTitle(editing ? QStringLiteral("编辑提醒") : QStringLiteral("添加提醒"));
    dialog->resize(640,560);
    dialog->setMinimumSize(560,420);
    auto *layout=new QVBoxLayout(dialog);
    layout->setContentsMargins(22,20,22,20);
    layout->setSpacing(12);
    auto *header=new QHBoxLayout;
    header->setSpacing(16);
    auto *titleLayout=new QVBoxLayout;
    titleLayout->setContentsMargins(0,0,0,0);
    auto *form = new QFormLayout;

    auto *titleEdit = addSummaryField(titleLayout,dialog,QStringLiteral("reminderTitle"),QStringLiteral("标题"));
    titleEdit->setMaximumWidth(520);
    titleEdit->setText(reminder.title);
    titleEdit->setPlaceholderText(QStringLiteral("例如：提交论文初稿"));
    header->addLayout(titleLayout,1);
    header->addWidget(dialog->createSaveStatus(QStringLiteral("reminderSaveStatus")),1);
    layout->addLayout(header);

    auto *contentEdit = new HeadingTextEdit(dialog);
    contentEdit->setModeControlsVisible(false);
    contentEdit->setContentLabel(QStringLiteral("说明"));
    contentEdit->setTextPointSize(20);
    bindPersistentTextZoom(contentEdit, database_, QStringLiteral("text_zoom.reminder.v2"));
    contentEdit->setObjectName(QStringLiteral("reminderContent"));
    contentEdit->setPlainText(reminder.content);
    contentEdit->setPlaceholderText(QStringLiteral("可选：补充说明"));

    auto *dateTimeEdit = new QDateTimeEdit(dialog);
    dateTimeEdit->setObjectName(QStringLiteral("reminderTime"));
    dateTimeEdit->setCalendarPopup(true);
    dateTimeEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    if (editing && reminder.remindAt.isValid()) {
        dateTimeEdit->setDateTime(reminder.remindAt);
    } else {
        dateTimeEdit->setDateTime(QDateTime(calendar_->selectedDate(), QTime(9, 0)));
    }
    form->addRow(QStringLiteral("提醒时间："), dateTimeEdit);

    auto *repeatCombo = new QComboBox(dialog);
    repeatCombo->setObjectName(QStringLiteral("reminderRepeat"));
    repeatCombo->addItem(QStringLiteral("不重复"), QStringLiteral("none"));
    repeatCombo->addItem(QStringLiteral("每天"), QStringLiteral("daily"));
    repeatCombo->addItem(QStringLiteral("每周"), QStringLiteral("weekly"));
    repeatCombo->addItem(QStringLiteral("每月"), QStringLiteral("monthly"));
    const int repeatIndex = repeatCombo->findData(reminder.repeatMode.isEmpty()
                                                       ? QStringLiteral("none")
                                                       : reminder.repeatMode);
    repeatCombo->setCurrentIndex(repeatIndex >= 0 ? repeatIndex : 0);
    form->addRow(QStringLiteral("重复："), repeatCombo);

    auto *projectCombo = new QComboBox(dialog);
    projectCombo->setObjectName(QStringLiteral("reminderProject"));
    projectCombo->addItem(QStringLiteral("不关联项目"), 0);
    QString projectError;
    const QList<AppDatabase::Project> projects = database_->projects(&projectError);
    if (!projectError.isEmpty()) {
        showDatabaseError(this, projectError);
        dialog->deleteLater(); return;
    }
    for (const AppDatabase::Project &project : projects) {
        projectCombo->addItem(project.name, project.id);
    }
    const int projectIndex = projectCombo->findData(reminder.projectId);
    projectCombo->setCurrentIndex(projectIndex >= 0 ? projectIndex : 0);
    form->addRow(QStringLiteral("关联项目："), projectCombo);

    layout->addLayout(form);
    layout->addWidget(contentEdit,1);
    QWidget::setTabOrder(titleEdit,dateTimeEdit);
    QWidget::setTabOrder(dateTimeEdit,repeatCombo);
    QWidget::setTabOrder(repeatCombo,projectCombo);
    QWidget::setTabOrder(projectCombo,contentEdit);
    auto saved=std::make_shared<AppDatabase::Reminder>(reminder);
    auto savedText=std::make_shared<QString>(contentEdit->toPlainText());
    saved->remindAt=dateTimeEdit->dateTime(); saved->repeatMode=repeatCombo->currentData().toString();
    saved->projectId=projectCombo->currentData().toInt();
    const auto value=[=] {
        auto value=*saved;
        value.title=titleEdit->text().trimmed();
        value.content=contentEdit->toPlainText()==*savedText ? saved->content : contentEdit->toPlainText();
        value.remindAt=dateTimeEdit->dateTime(); value.repeatMode=repeatCombo->currentData().toString();
        value.projectId=projectCombo->currentData().toInt();
        return value;
    };
    dialog->dirty=[=] {
        const auto now=value();
        return now.title!=saved->title || now.content!=saved->content || now.remindAt!=saved->remindAt
            || now.repeatMode!=saved->repeatMode || now.projectId!=saved->projectId;
    };
    const auto validate=[=] {
        dialog->updateSaveStatus(titleEdit->text().trimmed().isEmpty() ? QStringLiteral("待填写标题") : QString());
    };
    connect(titleEdit,&QLineEdit::textChanged,dialog,validate);
    connect(contentEdit,&HeadingTextEdit::textChanged,dialog,validate);
    connect(dateTimeEdit,&QDateTimeEdit::dateTimeChanged,dialog,validate);
    connect(repeatCombo,&QComboBox::currentIndexChanged,dialog,validate);
    connect(projectCombo,&QComboBox::currentIndexChanged,dialog,validate);
    dialog->resetDraft=[=] {
        titleEdit->setText(saved->title);
        if (contentEdit->toPlainText()!=*savedText) contentEdit->setPlainText(saved->content);
        dateTimeEdit->setDateTime(saved->remindAt);
        repeatCombo->setCurrentIndex(repeatCombo->findData(saved->repeatMode));
        projectCombo->setCurrentIndex(projectCombo->findData(saved->projectId));
        validate();
    };
    dialog->saveDraft=[=,this] {
        if (saved->id>0 && !dialog->dirty()) return true;
        auto updated=value();
        if (updated.title.isEmpty()) { validate(); titleEdit->setFocus(); return false; }
        updated.completed=false; updated.lastNotifiedAt=QDateTime();
        QString error;
        const bool ok=updated.id>0 ? database_->updateReminder(updated,&error)
            : database_->addReminder(updated,&updated.id,&error);
        if (!ok) { dialog->showSaveError(error); return false; }
        *saved=updated; *savedText=contentEdit->toPlainText();
        if (workspace) workspace->rekey(dialog->property("contentKey").toString(),QStringLiteral("reminder/%1").arg(updated.id));
        dialog->setWindowTitle(QStringLiteral("提醒")); dialog->showSavedStatus();
        refreshReminderList(); updateCalendarAppearance();
        return true;
    };
    auto *shortcut=new QShortcut(QKeySequence::Save,dialog); shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shortcut,&QShortcut::activated,dialog,[dialog]{ dialog->saveDraft(); });
    validate();
    if (workspace) workspace->present(key,dialog,this);
    else { dialog->exec(); delete dialog; }
}

void CalendarPage::addReminder() { showReminderDialog({},false); }
void CalendarPage::editReminder() {
    const auto reminder=selectedReminder(); if (reminder.id>0) showReminderDialog(reminder,true);
}

void CalendarPage::completeReminder()
{
    const AppDatabase::Reminder reminder = selectedReminder();
    if (reminder.id <= 0) {
        return;
    }
    if (auto *workspace=ContentWorkspace::find(this))
        if (!workspace->closePage(QStringLiteral("reminder/%1").arg(reminder.id))) return;
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
    if (auto *workspace=ContentWorkspace::find(this))
        if (!workspace->closePage(QStringLiteral("reminder/%1").arg(reminder.id))) return;
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
    deleteButton_->setEnabled(false);
    UiTheme::setRole(deleteButton_, "danger");
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
        item->setForeground(QColor(UiTheme::Muted));
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
    auto *workspace=ContentWorkspace::find(this);
    const QString key=creating ? QStringLiteral("diary/new/%1").arg(entry.date.toString(Qt::ISODate))
        : QStringLiteral("diary/%1").arg(entry.id);
    if (workspace && workspace->activate(key,owner ? owner : this)) return;
    auto *dialog=new DraftDialog(owner ? owner : this);
    dialog->setObjectName(QStringLiteral("diaryEditor"));
    dialog->setWindowTitle(creating ? QStringLiteral("添加日记") : QStringLiteral("日记"));
    dialog->resize(800,620);
    dialog->setMinimumSize(560,400);
    auto *layout=new QVBoxLayout(dialog);
    layout->setContentsMargins(22,20,22,20);
    layout->setSpacing(12);
    auto *header=new QHBoxLayout;
    header->setSpacing(16);
    auto *titleLayout=new QVBoxLayout;
    titleLayout->setContentsMargins(0,0,0,0);
    auto *title=addSummaryField(titleLayout,dialog,QStringLiteral("diaryTitle"));
    title->setMaximumWidth(520);
    header->addLayout(titleLayout,1);
    auto *dateLabel=new QLabel(entry.date.toString(Qt::ISODate),dialog);
    UiTheme::setRole(dateLabel,"muted"); header->addWidget(dateLabel);
    header->addWidget(dialog->createSaveStatus(QStringLiteral("diarySaveStatus")),1);
    layout->addLayout(header);
    auto *editor=new HeadingTextEdit(dialog); editor->setObjectName(QStringLiteral("diaryContent"));
    editor->setModeControlsVisible(false);
    editor->setTextPointSize(20);
    bindPersistentTextZoom(editor,database_,QStringLiteral("text_zoom.diary.v2")); layout->addWidget(editor,1);
    if (!creating) { title->setText(summaryText(entry.title,entry.content)); editor->setPlainText(entry.content); }
    auto saved=std::make_shared<AppDatabase::DiaryEntry>(entry);
    saved->title=title->text().trimmed();
    auto savedText=std::make_shared<QString>(editor->toPlainText());
    dialog->dirty=[=] { return title->text().trimmed()!=saved->title || editor->toPlainText()!=*savedText; };
    const auto validate=[=] { dialog->updateSaveStatus(title->text().trimmed().isEmpty()
        ? QStringLiteral("待填写简介") : editor->toPlainText().trimmed().isEmpty() ? QStringLiteral("待填写正文") : QString()); };
    connect(title,&QLineEdit::textChanged,dialog,validate); connect(editor,&HeadingTextEdit::textChanged,dialog,validate);
    dialog->resetDraft=[=] {
        title->setText(saved->title);
        if (editor->toPlainText()!=*savedText) editor->setPlainText(saved->content);
        validate();
    };
    dialog->saveDraft=[=,this] {
        if (saved->id>0 && !dialog->dirty()) return true;
        if (title->text().trimmed().isEmpty() || editor->toPlainText().trimmed().isEmpty()) {
            validate();
            if (title->text().trimmed().isEmpty()) title->setFocus(); else editor->setFocus();
            return false;
        }
        const QString content=editor->toPlainText()==*savedText ? saved->content : editor->toPlainText();
        QString error; int id=saved->id;
        const bool ok=id>0 ? database_->updateDiaryEntry(id,title->text().trimmed(),content,&error)
            : database_->addDiaryEntry(saved->date,title->text().trimmed(),content,&error,&id);
        if (!ok) { dialog->showSaveError(error); return false; }
        saved->id=id; saved->title=title->text().trimmed(); saved->content=content; *savedText=editor->toPlainText();
        if (workspace) workspace->rekey(dialog->property("contentKey").toString(),QStringLiteral("diary/%1").arg(id));
        dialog->setWindowTitle(QStringLiteral("日记")); dialog->showSavedStatus();
        refreshDiaryList(); refreshDiaryCollection(); updateCalendarAppearance();
        return true;
    };
    auto *shortcut=new QShortcut(QKeySequence::Save,dialog); shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shortcut,&QShortcut::activated,dialog,[dialog]{ dialog->saveDraft(); });
    validate();
    if (workspace) workspace->present(key,dialog,owner ? owner : this);
    else { dialog->exec(); delete dialog; }

}

void CalendarPage::openDiaryCollection()
{
    auto *workspace=ContentWorkspace::find(this);
    if (workspace && workspace->activate(QStringLiteral("diary/collection"))) return;
    if (!diaryCollection_) {
        diaryCollection_=new QDialog(this,Qt::Window);
        connect(diaryCollection_,&QDialog::finished,this,[this] {
            diaryCollection_=nullptr; collectionList_=nullptr; collectionSearch_=nullptr; collectionCount_=nullptr;
        });
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
        bindListOpen(collectionList_, diaryCollection_, openCurrent);
    }
    refreshDiaryCollection();
    if (workspace) { workspace->present(QStringLiteral("diary/collection"),diaryCollection_,this); return; }
    if (diaryCollection_->isMinimized()) diaryCollection_->showNormal();
    else diaryCollection_->show();
    diaryCollection_->raise();
    diaryCollection_->activateWindow();
}

void CalendarPage::refreshDiaryCollection()
{
    if (!diaryCollection_) return;
    const int selected=collectionList_->currentItem() ? collectionList_->currentItem()->data(Qt::UserRole).toInt() : 0;
    const int scroll=collectionList_->verticalScrollBar()->value();
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
    collectionList_->doItemsLayout(); collectionList_->verticalScrollBar()->setValue(scroll);
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
    if (auto *workspace=ContentWorkspace::find(this))
        if (!workspace->closePage(QStringLiteral("diary/%1").arg(entry.id))) return;
    if (!database_->deleteDiaryEntry(entry.id, &error)) {
        showDatabaseError(this, error);
        return;
    }
    refreshDiaryList();
    refreshDiaryCollection();
    updateCalendarAppearance();
}
