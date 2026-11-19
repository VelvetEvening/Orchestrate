#pragma once

#include <QMainWindow>

#include "data/appdatabase.h"
#include "platform/autostart.h"

class QAction;
class QCheckBox;
class ContentWorkspace;
class GlobalShortcut;
class QCloseEvent;
class QFrame;
class QLabel;
class QMenu;
class QNetworkAccessManager;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QSystemTrayIcon;
class SkinLibrary;
class UpdateWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr, std::unique_ptr<AutoStart> autoStart = nullptr,
                        QNetworkAccessManager *skinNetwork = nullptr);
    ~MainWindow() override = default;
    bool ready() const { return database_.isOpen(); }
    bool mainWindowVisible() const;
    void showMainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif

private:
    class NavigationSidebar;
    void toggleMainWindow();
    bool savePendingChanges();
    void buildUi();
    void buildNavigation(QWidget *parent);
    QScrollArea *makeSettingsPage();
    QFrame *makeAppearanceSection(QWidget *parent);
    QFrame *makeRuntimeSection(QWidget *parent);
    void applyAppearance(const QString &palette, const QString &layout);
    void openSkinManager(QWidget *source);
    void buildTray();
    void switchToPage(int index, bool confirmLeave = true);
    void toggleSidebar();
    void updateSidebarAppearance();
    void quitApplication();
    void installUpdate();
    void refreshAutoStartSettings(const QString &error = QString());
    void setAvailableUpdate(const QString &tag);
    void openUpdatesPage();

    std::unique_ptr<AutoStart> autoStart_;
    QCheckBox *autoStartCheck_ = nullptr;
    QLabel *autoStartStatus_ = nullptr;
    QPushButton *systemStartupButton_ = nullptr;
    NavigationSidebar *sidebar_ = nullptr;
    SkinLibrary *skinLibrary_ = nullptr;
    GlobalShortcut *globalShortcut_ = nullptr;
    QPushButton *sidebarToggleButton_ = nullptr;
    QStackedWidget *pageStack_ = nullptr;
    ContentWorkspace *settingsWorkspace_ = nullptr;
    QScrollArea *settingsScroll_ = nullptr;
    UpdateWidget *updateWidget_ = nullptr;
    QSystemTrayIcon *trayIcon_ = nullptr;
    QMenu *trayMenu_ = nullptr;
    QAction *showAction_ = nullptr;
    QAction *quitAction_ = nullptr;
    QAction *updateAction_ = nullptr;
    QList<QPushButton *> navigationButtons_;
    bool sidebarCollapsed_ = false;
    bool closeToTray_ = true;
    bool allowQuit_ = false;
    AppDatabase database_;
};
