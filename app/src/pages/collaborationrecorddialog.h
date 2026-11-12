#pragma once

#include "data/appdatabase.h"
#include <QDialog>

class HeadingTextEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTabBar;

class CollaborationRecordDialog final : public QDialog {
    Q_OBJECT
public:
    CollaborationRecordDialog(AppDatabase *database, AppDatabase::WorkRecord record,
                              const QString &location, QWidget *parent = nullptr);
    bool hasSaved() const { return saved_; }
    const AppDatabase::WorkRecord &record() const { return record_; }
    void reject() override;
signals:
    void savedRecord(int id);

private:
    bool dirty() const;
    bool save();
    void setEditing(bool editing);
    void showSection();
    void updateActions();
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
    bool editing_ = false;
    bool saved_ = false;
    QLineEdit *title_;
    QTabBar *sections_;
    QStackedWidget *pages_;
    HeadingTextEdit *summary_;
    HeadingTextEdit *body_;
    QWidget *emptySummary_;
    QPushButton *edit_;
    QPushButton *copy_;
    QPushButton *save_;
    QPushButton *cancel_;
    QLabel *status_;
};
