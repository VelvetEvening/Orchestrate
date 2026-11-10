#pragma once
#include "data/appdatabase.h"
#include "widgets/siblingorderview.h"
#include <QWidget>
#include <QPointer>

class ClipboardMonitor;
class QCheckBox;
class QComboBox;
class QDialog;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabBar;
class QTimer;

class ClipboardPage final : public QWidget {
    Q_OBJECT
public:
    explicit ClipboardPage(AppDatabase *database, QWidget *parent=nullptr);
    bool savePendingChanges();
signals:
    void hideRequested();
protected:
    void showEvent(QShowEvent *event) override;
private:
    int tabId() const;
    int folderId() const;
    QList<int> selectedIds() const;
    void refreshTabs(int selected=0);
    void refreshFolders(int selected=0);
    void refreshItems();
    void updatePreview();
    void updateActions();
    void editItem(bool creating);
    void copySelected();
    void moveSelected();
    void deleteSelected();
    void addTab();
    void addFolder();
    void tabMenu(const QPoint &position);
    void folderMenu(const QPoint &position);
    void itemMenu(const QPoint &position);
    void historySettings();
    void report(const QString &message);
    AppDatabase *database_;
    ClipboardMonitor *monitor_;
    QTabBar *tabs_;
    SiblingOrderList *folders_;
    SiblingOrderList *items_;
    QLineEdit *search_;
    QComboBox *searchScope_;
    QCheckBox *listening_;
    QLabel *status_;
    QLabel *count_;
    QLabel *empty_;
    QLabel *previewTitle_;
    QLabel *previewImage_;
    QPlainTextEdit *previewText_;
    QPushButton *copy_;
    QPushButton *edit_;
    QPushButton *move_;
    QPushButton *remove_;
    QTimer *refreshTimer_;
    bool dirty_=true;
    int displayedTabId_=0;
    QPointer<QDialog> editor_;
};
