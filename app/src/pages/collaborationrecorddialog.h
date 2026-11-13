#pragma once

#include "data/appdatabase.h"
#include "ui/contentworkspace.h"

class HeadingTextEdit;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QTabBar;

class CollaborationRecordDialog final : public QDialog, public ContentLeaveGuard {
    Q_OBJECT
public:
    CollaborationRecordDialog(AppDatabase *database, AppDatabase::WorkRecord record,
                              QWidget *parent = nullptr);
    bool hasSaved() const { return saved_; }
    const AppDatabase::WorkRecord &record() const { return record_; }
    void reject() override;
    bool confirmLeave() override;
signals:
    void savedRecord(int id);

private:
    bool dirty() const;
    bool save();
    void showSection();
    void updateStatus();
    void resetDraft();
    QString bodyValue() const;
    QString summaryValue() const;
    void rememberDraft();

    AppDatabase *database_;
    AppDatabase::WorkRecord record_;
    QString savedTitle_;
    QString savedBodyText_;
    QString savedSummaryText_;
    QString draftBodyText_;
    QString draftSummaryText_;
    QString error_;
    bool saved_ = false;
    QLineEdit *title_;
    QTabBar *sections_;
    QStackedWidget *pages_;
    HeadingTextEdit *summary_;
    HeadingTextEdit *body_;
    QLabel *status_;
};
