#pragma once

#include <QDate>
#include <QList>
#include <QWidget>
#include <QPointer>

#include <QDateTime>

#include "../data/appdatabase.h"

class QCalendarWidget;
class QComboBox;
class QDateTimeEdit;
class QDialog;
class QLineEdit;
class QSystemTrayIcon;
class QTimer;
class QTextEdit;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

class CalendarPage final : public QWidget
{
    Q_OBJECT

public:
    explicit CalendarPage(AppDatabase *database,
                           QSystemTrayIcon *trayIcon = nullptr,
                           QWidget *parent = nullptr);

private:
    void addReminder();
    void editReminder();
    void completeReminder();
    void deleteReminder();
    void refreshReminderList();
    void checkDueReminders();
    AppDatabase::Reminder selectedReminder() const;
    bool showReminderDialog(AppDatabase::Reminder *reminder, bool editing);
    void updateSelectedDate(const QDate &date);
    void updateCalendarAppearance();
    void addDiaryEntry();
    void editDiaryEntry();
    void deleteDiaryEntry();
    void refreshDiaryList();
    void diaryDialog(AppDatabase::DiaryEntry entry, bool creating, QWidget *owner = nullptr);
    void openDiaryCollection();
    void refreshDiaryCollection();
    void filterDiaryCollection();
    AppDatabase::DiaryEntry selectedEntry() const;

    AppDatabase *database_ = nullptr;
    QCalendarWidget *calendar_ = nullptr;
    QLabel *selectedDateLabel_ = nullptr;
    QLabel *summaryLabel_ = nullptr;
    QListWidget *reminderList_ = nullptr;
    QListWidget *diaryList_ = nullptr;
    QPushButton *editReminderButton_ = nullptr;
    QPushButton *completeReminderButton_ = nullptr;
    QPushButton *deleteReminderButton_ = nullptr;
    QPushButton *editButton_ = nullptr;
    QPushButton *deleteButton_ = nullptr;
    QSystemTrayIcon *trayIcon_ = nullptr;
    QTimer *reminderTimer_ = nullptr;
    QPointer<QDialog> diaryCollection_;
    QListWidget *collectionList_ = nullptr;
    QLineEdit *collectionSearch_ = nullptr;
    QLabel *collectionCount_ = nullptr;
};
