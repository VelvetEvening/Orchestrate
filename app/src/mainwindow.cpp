#include "ui/theme.h"
#include "mainwindow.h"

#include "pages/calendarpage.h"
#include "pages/automationpage.h"
#include "pages/projectspage.h"
#include "pages/clipboardpage.h"
#include "platform/globalshortcut.h"
#include "update/updatewidget.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QVBoxLayout>
#include <QUrl>
#include <QWidget>
#include <QWindow>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif


namespace {

QFrame *makeCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("card"));
    card->setFrameShape(QFrame::StyledPanel);
    card->setFrameShadow(QFrame::Plain);
    return card;
}

} // namespace


MainWindow::MainWindow(QWidget *parent, std::unique_ptr<AutoStart> autoStart)
    : QMainWindow(parent), autoStart_(autoStart ? std::move(autoStart) : std::make_unique<AutoStart>())
{
    setWindowTitle(QStringLiteral("Orchestrate"));
    UiTheme::install(*qApp);
    setMinimumSize(980, 640);
    resize(1180, 760);

    QString databaseError;
    if (!database_.open(&databaseError)) {
        QMessageBox::critical(this, QStringLiteral("数据库初始化失败"), databaseError);
        auto *message = new QLabel(QStringLiteral("数据库初始化失败，编辑功能不可用。\n请恢复数据目录的访问权限后重新启动。\n\n") + databaseError, this);
        message->setObjectName(QStringLiteral("databaseUnavailable"));
        message->setTextFormat(Qt::PlainText);
        message->setWordWrap(true);
        message->setMargin(32);
        setCentralWidget(message);
        closeToTray_ = false;
        return;
    }

    globalShortcut_ = new GlobalShortcut(this);
    connect(globalShortcut_, &GlobalShortcut::activated, this, &MainWindow::showMainWindow);
    buildUi();
#ifdef Q_OS_WIN
    if (QApplication::platformName() == QStringLiteral("windows")) {
        // Extend the page header into the caption while Qt keeps the window
        // buttons, resize borders, system dragging and maximize behavior.
        setWindowFlags(windowFlags() | Qt::CustomizeWindowHint
                       | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint);
        setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
        winId();
        // QWidget adds WindowTitleHint when window buttons are requested.
        // Clear it on QWindow so the caption does not paint an app name/icon.
        // Keep windowTitle() for taskbar / Alt+Tab identification.
        windowHandle()->setFlag(Qt::WindowTitleHint, false);
        const int captionHeight = qMax(32, windowHandle()->safeAreaMargins().top());
        auto *header = findChild<QFrame *>(QStringLiteral("windowHeader"));
        header->setFixedHeight(captionHeight);
        // Qt's Windows caption buttons are each 1.5 times the caption height.
        header->layout()->setContentsMargins(18, 0, captionHeight * 9 / 2 + 12, 0);
    }
#endif
    buildTray();
    switchToPage(0);
}

void MainWindow::buildUi()
{
    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("root"));

    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *header = new QFrame(root);
    header->setObjectName(QStringLiteral("windowHeader"));
    header->setFixedHeight(32);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18, 0, 24, 0);
    headerLayout->setSpacing(12);
    sidebarToggleButton_ = new QPushButton(header);
    sidebarToggleButton_->setObjectName(QStringLiteral("sidebarToggle"));
    sidebarToggleButton_->setIcon(UiTheme::icon(QStringLiteral("panel-left")));
    sidebarToggleButton_->setIconSize(QSize(18, 18));
    sidebarToggleButton_->setToolTip(QStringLiteral("收起侧边栏"));
    sidebarToggleButton_->setAccessibleName(QStringLiteral("收起侧边栏"));
    sidebarToggleButton_->setCursor(Qt::PointingHandCursor);
    headerLayout->addWidget(sidebarToggleButton_);
    headerLayout->addStretch(1);
    rootLayout->addWidget(header);
    connect(sidebarToggleButton_, &QPushButton::clicked, this, &MainWindow::toggleSidebar);

    auto *bodyLayout = new QHBoxLayout;
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    sidebar_ = new QFrame(root);
    sidebar_->setObjectName(QStringLiteral("sidebar"));
    sidebar_->setMinimumWidth(72);
    sidebar_->setMaximumWidth(260);
    sidebar_->setFixedWidth(216);
    buildNavigation(sidebar_);
    bodyLayout->addWidget(sidebar_);

    pageStack_ = new QStackedWidget(root);
    pageStack_->setObjectName(QStringLiteral("pageStack"));
    bodyLayout->addWidget(pageStack_, 1);
    rootLayout->addLayout(bodyLayout, 1);

    setCentralWidget(root);
}

void MainWindow::buildNavigation(QWidget *parent)
{
    sidebarLayout_ = new QVBoxLayout(parent);
    sidebarLayout_->setContentsMargins(14, 16, 14, 16);
    sidebarLayout_->setSpacing(6);

    const QStringList labels {
        QStringLiteral("时序记录"),
        QStringLiteral("项目记录"),
        QStringLiteral("自动化工具"),
        QStringLiteral("剪贴板"),
        QStringLiteral("设置")
    };

    const QStringList icons {
        QStringLiteral("calendar"), QStringLiteral("folder"), QStringLiteral("workflow"),
        QStringLiteral("clipboard"), QStringLiteral("settings")
    };

    for (int i = 0; i < labels.size(); ++i) {
        auto *button = new QPushButton(labels.at(i), parent);
        button->setObjectName(QStringLiteral("navButton"));
        button->setProperty("expandedText", labels.at(i));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setIcon(UiTheme::icon(icons.at(i)));
        button->setIconSize(QSize(19, 19));
        button->setToolTip(labels.at(i));
        button->setCursor(Qt::PointingHandCursor);
        button->setAccessibleName(labels.at(i));
        if (i == labels.size() - 1) sidebarLayout_->addStretch(1);
        navigationButtons_.append(button);
        sidebarLayout_->addWidget(button);

        connect(button, &QPushButton::clicked, this, [this, i] {
            switchToPage(i);
        });
    }

}

void MainWindow::toggleSidebar()
{
    sidebarCollapsed_ = !sidebarCollapsed_;
    if (sidebar_ == nullptr) {
        return;
    }

    sidebar_->setFixedWidth(sidebarCollapsed_ ? 72 : 216);

    for (QPushButton *button : navigationButtons_) {
        button->setText(sidebarCollapsed_ ? QString() : button->property("expandedText").toString());
        if (button->property("updateTag").toString().isEmpty())
            button->setToolTip(button->property("expandedText").toString());
        button->setStyleSheet(sidebarCollapsed_
                                  ? QStringLiteral("text-align: center; padding: 10px 0;")
                                  : QString());
    }

    sidebarToggleButton_->setAccessibleName(sidebarCollapsed_ ? QStringLiteral("展开侧边栏") : QStringLiteral("收起侧边栏"));
    sidebarToggleButton_->setToolTip(sidebarCollapsed_ ? QStringLiteral("展开侧边栏")
                                                       : QStringLiteral("收起侧边栏"));
}

void MainWindow::buildPages()
{
    // This method is kept separate so the page shell can grow without changing the
    // navigation/tray wiring.
}

void MainWindow::buildTray()
{
    trayIcon_ = new QSystemTrayIcon(QApplication::windowIcon(), this);
    trayIcon_->setToolTip(QStringLiteral("Orchestrate"));

    trayMenu_ = new QMenu(this);
    showAction_ = trayMenu_->addAction(QStringLiteral("打开 Orchestrate"));
    updateAction_ = trayMenu_->addAction(QStringLiteral("查看更新"));
    updateAction_->setObjectName(QStringLiteral("viewUpdateAction"));
    updateAction_->setVisible(false);
    connect(updateAction_, &QAction::triggered, this, &MainWindow::openUpdatesPage);
    trayMenu_->addSeparator();
    quitAction_ = trayMenu_->addAction(QStringLiteral("退出 Orchestrate"));
    trayIcon_->setContextMenu(trayMenu_);

    connect(showAction_, &QAction::triggered, this, &MainWindow::showMainWindow);
    connect(trayIcon_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                    showMainWindow();
                }
            });
    connect(quitAction_, &QAction::triggered, this, &MainWindow::quitApplication);

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        trayIcon_->show();
    } else {
        trayIcon_->setVisible(false);
        trayIcon_->setToolTip(QStringLiteral("系统托盘不可用，关闭窗口将直接退出。"));
        closeToTray_ = false;
    }
}

void MainWindow::switchToPage(int index)
{
    if (!pageStack_) {
        return;
    }

    if (pageStack_->count() == 0) {
        auto *calendarPage = new CalendarPage(&database_, trayIcon_, pageStack_);
        pageStack_->addWidget(calendarPage);

        auto *projectsPage = new ProjectsPage(&database_, pageStack_);
        pageStack_->addWidget(projectsPage);

        auto *automationPage = new AutomationPage(&database_, pageStack_);
        pageStack_->addWidget(automationPage);

        auto *clipboardPage = new ClipboardPage(&database_, pageStack_);
        pageStack_->addWidget(clipboardPage);
        connect(clipboardPage, &ClipboardPage::hideRequested, this, [this] { hide(); });

        auto *settingsPage = new QWidget(pageStack_);
        settingsPage->setObjectName(QStringLiteral("settingsPage"));
        auto *settingsLayout = new QVBoxLayout(settingsPage);
        settingsLayout->setContentsMargins(24, 16, 24, 24);
        settingsLayout->setSpacing(18);

        auto *settingsCard = makeCard(settingsPage);
        auto *settingsCardLayout = new QVBoxLayout(settingsCard);
        settingsCardLayout->setContentsMargins(22, 20, 22, 20);
        settingsCardLayout->setSpacing(12);

        auto *runtimeTitle = new QLabel(QStringLiteral("运行与托盘"), settingsCard);
        runtimeTitle->setObjectName(QStringLiteral("cardTitle"));
        settingsCardLayout->addWidget(runtimeTitle);

        autoStartCheck_ = new QCheckBox(QStringLiteral("开机自动启动 Orchestrate"), settingsCard);
        autoStartCheck_->setObjectName(QStringLiteral("autoStartCheck"));
        settingsCardLayout->addWidget(autoStartCheck_);
        autoStartStatus_ = new QLabel(settingsCard);
        autoStartStatus_->setObjectName(QStringLiteral("autoStartStatus"));
        autoStartStatus_->setTextFormat(Qt::PlainText);
        autoStartStatus_->setWordWrap(true);
        settingsCardLayout->addWidget(autoStartStatus_);
        systemStartupButton_ = new QPushButton(QStringLiteral("打开 Windows 启动应用设置"), settingsCard);
        settingsCardLayout->addWidget(systemStartupButton_);
        connect(autoStartCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
            QString error;
            autoStart_->setEnabled(enabled, &error);
            refreshAutoStartSettings(error);
        });
        connect(systemStartupButton_, &QPushButton::clicked, this, [this] {
            if (!QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:startupapps"))))
                autoStartStatus_->setText(QStringLiteral("无法打开系统设置，请手动进入 Windows 设置 → 应用 → 启动。"));
        });
        refreshAutoStartSettings();

        auto *trayCheck = new QCheckBox(QStringLiteral("关闭主窗口时继续驻留系统托盘"), settingsCard);
        trayCheck->setObjectName(QStringLiteral("closeToTrayCheck"));
        trayCheck->setChecked(closeToTray_);
        settingsCardLayout->addWidget(trayCheck);
        connect(trayCheck, &QCheckBox::toggled, this, [this](bool enabled) {
            closeToTray_ = enabled;
        });

        auto *shortcutCheck = new QCheckBox(QStringLiteral("启用全局快捷键 Alt+X，唤起 Orchestrate"), settingsCard);
        shortcutCheck->setObjectName(QStringLiteral("globalShortcutCheck"));
        shortcutCheck->setChecked(database_.setting(QStringLiteral("global_shortcut_enabled"), QStringLiteral("true")) == QStringLiteral("true"));
        settingsCardLayout->addWidget(shortcutCheck);
        auto *shortcutLabel = new QLabel(settingsCard);
        shortcutLabel->setObjectName(QStringLiteral("globalShortcutStatus"));
        shortcutLabel->setWordWrap(true);
        settingsCardLayout->addWidget(shortcutLabel);
        connect(globalShortcut_, &GlobalShortcut::statusChanged, shortcutLabel, &QLabel::setText);
        connect(shortcutCheck, &QCheckBox::toggled, this, [this, shortcutLabel](bool enabled) {
            globalShortcut_->configure(enabled);
            QString error;
            if (!database_.setSetting(QStringLiteral("global_shortcut_enabled"), enabled ? QStringLiteral("true") : QStringLiteral("false"), &error)) {
                shortcutLabel->setText(globalShortcut_->status() + QStringLiteral("\n设置保存失败：") + error);
            }
        });
        auto *retryShortcut = new QPushButton(QStringLiteral("重试注册 Alt+X"), settingsCard);
        retryShortcut->setEnabled(shortcutCheck->isChecked());
        settingsCardLayout->addWidget(retryShortcut);
        connect(shortcutCheck, &QCheckBox::toggled, retryShortcut, &QPushButton::setEnabled);
        connect(retryShortcut, &QPushButton::clicked, this, [this, shortcutCheck] {
            globalShortcut_->configure(shortcutCheck->isChecked());
        });
        globalShortcut_->configure(shortcutCheck->isChecked());


        settingsLayout->addWidget(settingsCard);
        auto *update = new UpdateWidget(settingsPage);
        connect(update, &UpdateWidget::installRequested, this, &MainWindow::installUpdate);
        connect(update, &UpdateWidget::updateAvailableChanged, this, &MainWindow::setAvailableUpdate);
        connect(update, &UpdateWidget::updateNotificationRequested, this, [this](const QString &tag) {
            if (trayIcon_ && trayIcon_->isVisible() && QSystemTrayIcon::supportsMessages())
                trayIcon_->showMessage(QStringLiteral("Orchestrate 有新版本"),
                    QStringLiteral("%1 已发布。打开“设置 → 版本与更新”查看说明并下载。").arg(tag),
                    QSystemTrayIcon::Information, 8000);
        });
        settingsLayout->addWidget(update);
        settingsLayout->addStretch(1);
        auto *settingsScroll = new QScrollArea(pageStack_);
        settingsScroll->setWidgetResizable(true);
        settingsScroll->setFrameShape(QFrame::NoFrame);
        settingsScroll->setWidget(settingsPage);
        pageStack_->addWidget(settingsScroll);
        QWidget::setTabOrder(sidebarToggleButton_, navigationButtons_.first());
        update->startAutomaticChecks();
    }

    if (index < 0 || index >= pageStack_->count()) {
        return;
    }

    pageStack_->setCurrentIndex(index);
    if (index == 4) refreshAutoStartSettings();
    for (int i = 0; i < navigationButtons_.size(); ++i) {
        navigationButtons_.at(i)->setChecked(i == index);
    }
}

void MainWindow::setAvailableUpdate(const QString &tag)
{
    if (navigationButtons_.size() < 5) return;
    auto *settings = navigationButtons_.at(4);
    const auto text = tag.isEmpty() ? QStringLiteral("设置") : QStringLiteral("设置 · 有更新");
    settings->setProperty("expandedText", text);
    settings->setProperty("updateTag", tag);
    settings->setText(sidebarCollapsed_ ? QString() : text);
    settings->setToolTip(tag.isEmpty() ? text : QStringLiteral("设置：有新版本 %1").arg(tag));
    settings->setAccessibleName(settings->toolTip());
    settings->setIcon(tag.isEmpty() ? UiTheme::icon(QStringLiteral("settings"))
                                   : QIcon(QStringLiteral(":/icons/update-available.svg")));
    if (updateAction_) {
        updateAction_->setText(QStringLiteral("查看更新（%1）").arg(tag));
        updateAction_->setVisible(!tag.isEmpty());
    }
}

void MainWindow::openUpdatesPage()
{
    showMainWindow();
    switchToPage(4);
    auto *scroll = qobject_cast<QScrollArea *>(pageStack_->currentWidget());
    if (scroll) {
        auto *update = scroll->findChild<UpdateWidget *>();
        if (update) scroll->ensureWidgetVisible(update);
    }
}

void MainWindow::refreshAutoStartSettings(const QString &error)
{
    if (!autoStartCheck_) return;
    const auto state = autoStart_->state();
    const QSignalBlocker blocker(autoStartCheck_);
    autoStartCheck_->setChecked(state.enabled);
    autoStartCheck_->setEnabled(state.error.isEmpty());
    systemStartupButton_->setVisible(state.disabledByWindows);
    autoStartStatus_->setText(!error.isEmpty() ? error : !state.error.isEmpty() ? state.error : state.message);
}

void MainWindow::showMainWindow()
{
    show();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
#ifdef Q_OS_WIN
    if (QApplication::platformName() == QStringLiteral("windows")) {
        // STARTUPINFO/SW_HIDE from an older updater can hide the native window
        // while QWidget already considers it visible, making show() a no-op.
        const auto hwnd = reinterpret_cast<HWND>(winId());
        ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
    }
#endif
    raise();
    activateWindow();
}

bool MainWindow::mainWindowVisible() const
{
    if (!isVisible() || isMinimized()) return false;
#ifdef Q_OS_WIN
    if (QApplication::platformName() == QStringLiteral("windows")) {
        const auto hwnd = reinterpret_cast<HWND>(effectiveWinId());
        return hwnd && IsWindowVisible(hwnd) && !IsIconic(hwnd);
    }
#endif
    return true;
}

void MainWindow::quitApplication()
{
    if (!savePendingChanges()) return;
    allowQuit_ = true;
    if (trayIcon_) {
        trayIcon_->hide();
    }
    qApp->quit();
}

void MainWindow::installUpdate()
{
    for (auto *page : findChildren<AutomationPage *>()) {
        if (page->hasRunningCommands()) {
            QMessageBox::warning(this, QStringLiteral("暂时无法更新"), QStringLiteral("请等待运行中的工具命令结束后再安装更新。"));
            return;
        }
    }
    if (QMessageBox::question(this, QStringLiteral("安装更新"),
            QStringLiteral("安装更新将退出并重启 Orchestrate。引用本目录的计划任务会临时暂停，结束后恢复原状态。个人数据会保留；新版启动确认且任务恢复后，自动清理本次旧版备份及临时文件。失败或恢复未完成则保留恢复材料。\n\n是否继续？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    if (!savePendingChanges()) return;
    auto *update = findChild<UpdateWidget *>();
    QString error;
    if (!update || !update->startInstaller(&error)) {
        QMessageBox::warning(this, QStringLiteral("无法安装更新"), error);
        return;
    }
    allowQuit_ = true;
    if (trayIcon_) trayIcon_->hide();
    qApp->quit();
}

bool MainWindow::savePendingChanges()
{
    for (auto *page : findChildren<ProjectsPage *>()) {
        if (!page->savePendingChanges()) return false;
    }
    for (auto *page : findChildren<ClipboardPage *>()) {
        if (!page->savePendingChanges()) return false;
    }
    return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (closeToTray_ && trayIcon_ && trayIcon_->isVisible() && !allowQuit_) {
        hide();
        event->ignore();
        // Closing to the tray is always silent; scheduled reminder alerts are
        // independent and remain enabled.
        return;
    }

    if (!allowQuit_ && !savePendingChanges()) {
        event->ignore();
        return;
    }
    event->accept();
}
