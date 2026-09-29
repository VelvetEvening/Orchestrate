#pragma once

#include <QMainWindow>

#include "data/appdatabase.h"
#include "platform/autostart.h"

class QAction;
class QCheckBox;
class GlobalShortcut;
class QCloseEvent;
class QFrame;
class QLabel;
class QMenu;
class QPushButton;
class QStackedWidget;
class QSystemTrayIcon;
class QVBoxLayout;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr, std::unique_ptr<AutoStart> autoStart = nullptr);
    ~MainWindow() override = default;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    bool savePendingChanges();
    void buildUi();
    void buildNavigation(QWidget *parent);
    void buildPages();
    void buildTray();
    void switchToPage(int index);
    void toggleSidebar();
    void showMainWindow();
    void quitApplication();
    void refreshAutoStartSettings(const QString &error = QString());

    std::unique_ptr<AutoStart> autoStart_;
    QCheckBox *autoStartCheck_ = nullptr;
    QLabel *autoStartStatus_ = nullptr;
    QPushButton *systemStartupButton_ = nullptr;
    QFrame *sidebar_ = nullptr;
    GlobalShortcut *globalShortcut_ = nullptr;
    QVBoxLayout *sidebarLayout_ = nullptr;
    QLabel *brandLabel_ = nullptr;
    QLabel *sectionLabel_ = nullptr;
    QPushButton *sidebarToggleButton_ = nullptr;
    QStackedWidget *pageStack_ = nullptr;
    QSystemTrayIcon *trayIcon_ = nullptr;
    QMenu *trayMenu_ = nullptr;
    QAction *showAction_ = nullptr;
    QAction *quitAction_ = nullptr;
    QList<QPushButton *> navigationButtons_;
    bool sidebarCollapsed_ = false;
    bool closeToTray_ = true;
    bool allowQuit_ = false;
    AppDatabase database_;
};
