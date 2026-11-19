#pragma once
#include "data/appdatabase.h"
#include "widgets/siblingorderview.h"
#include <QWidget>

class ClipboardMonitor;
class QAction;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabBar;
class QTimer;
class WorkspaceSplitter;

class ClipboardPage final : public QWidget {
    Q_OBJECT
public:
    explicit ClipboardPage(AppDatabase *database, QWidget *parent=nullptr);
    bool pasteOnUse() const;
    void report(const QString &message);
signals:
    void pasteOnUseChanged(bool enabled);
    void hideRequested(bool pasteToPreviousWindow, quint32 clipboardSequence);
protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
private:
    int tabId() const;
    QList<int> selectedIds() const;
    void refreshTabs(int selected=0);
    void refreshItems();
    void saveExpandedFolders();
    void selectFolder(int id);
    void updatePreview();
    void arrangePreview(bool opening = false);
    void editItem(bool creating);
    void copySelected();
    void deleteSelected();
    void addTab();
    void addFolder();
    void tabMenu(const QPoint &position);
    void folderMenu(const QPoint &position);
    void itemMenu(const QPoint &position);
    void historySettings();
    AppDatabase *database_;
    ClipboardMonitor *monitor_;
    QAction *pasteOnUse_;
    QTabBar *tabs_;
    SiblingOrderTree *items_;
    QLineEdit *search_;
    QLabel *status_;
    WorkspaceSplitter *contentSplit_;
    QList<int> horizontalSizes_{560, 400};
    QList<int> verticalSizes_{330, 170};
    QWidget *previewPane_;
    QLabel *previewMeta_;
    QLabel *previewImage_;
    QPlainTextEdit *previewText_;
    QPushButton *edit_;
    QPushButton *remove_;
    QTimer *refreshTimer_;
    bool dirty_=true;
    int displayedTabId_=0;
    QString displayedSearch_;
};
