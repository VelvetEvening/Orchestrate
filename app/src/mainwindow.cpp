#include "ui/theme.h"
#include "mainwindow.h"

#include "pages/calendarpage.h"
#include "pages/automationpage.h"
#include "pages/projectspage.h"
#include "pages/clipboardpage.h"
#include "platform/globalshortcut.h"
#include "platform/clipboardpaste.h"
#include "widgets/shortcutsettings.h"
#include "update/updatewidget.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
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
#include "ui/contentworkspace.h"
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

constexpr int ExpandedSidebarWidth = 168;
constexpr int CollapsedSidebarWidth = 72;
const QString PaletteSetting = QStringLiteral("appearance_palette");
const QString LayoutSetting = QStringLiteral("appearance_layout");

QFrame *makeSection(QWidget *parent)
{
    auto *card = new QFrame(parent);
    UiTheme::setSurface(card, "section");
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

    // Read each new preference independently so changing one preserves the other legacy choice.
    const QString legacyPreset = database_.setting(QStringLiteral("appearance_preset"), QStringLiteral("mist"));
    const QString legacyLayout = legacyPreset == QStringLiteral("precision") ? QStringLiteral("compact") : QStringLiteral("standard");
    UiTheme::apply(*qApp, database_.setting(PaletteSetting, legacyPreset), database_.setting(LayoutSetting, legacyLayout));

    globalShortcut_ = new GlobalShortcut(database_.setting("global_shortcut_enabled", "true") == "true",
                                         database_.setting("global_shortcut_binding"), this);
    connect(globalShortcut_, &GlobalShortcut::activated, this, &MainWindow::toggleMainWindow);
    buildUi();
    sidebarCollapsed_ = database_.setting(QStringLiteral("sidebar_collapsed"), QStringLiteral("false")) == QStringLiteral("true");
    updateSidebarAppearance();
#ifdef Q_OS_WIN
    if (QApplication::platformName() == QStringLiteral("windows")) {
        // Dedicate the caption to window controls while Qt keeps the window
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
    sidebarToggleButton_->setIconSize(QSize(18, 18));
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
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(14, 16, 14, 16);
    layout->setSpacing(6);

    const QStringList labels {
        QStringLiteral("日历"),
        QStringLiteral("项目"),
        QStringLiteral("工具"),
        QStringLiteral("剪贴板"),
        QStringLiteral("设置")
    };

    const QStringList icons {
        QStringLiteral("calendar"), QStringLiteral("folder"), QStringLiteral("toolbox"),
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
        if (i == labels.size() - 1) layout->addStretch(1);
        navigationButtons_.append(button);
        layout->addWidget(button);

        connect(button, &QPushButton::clicked, this, [this, i] {
            switchToPage(i);
        });
    }

}

void MainWindow::toggleSidebar()
{
    const bool collapsed = !sidebarCollapsed_;
    QString error;
    if (!database_.setSetting(QStringLiteral("sidebar_collapsed"), collapsed ? QStringLiteral("true") : QStringLiteral("false"), &error)) {
        QMessageBox::warning(this, QStringLiteral("无法保存侧边栏设置"), error);
        return;
    }
    sidebarCollapsed_ = collapsed;
    updateSidebarAppearance();
}

void MainWindow::updateSidebarAppearance()
{
    if (sidebar_ == nullptr) {
        return;
    }

    sidebar_->setFixedWidth(sidebarCollapsed_ ? CollapsedSidebarWidth : ExpandedSidebarWidth);

    for (QPushButton *button : navigationButtons_) {
        button->setText(sidebarCollapsed_ ? QString() : button->property("expandedText").toString());
        UiTheme::setRole(button, sidebarCollapsed_ ? "collapsedNav" : "");
    }

    sidebarToggleButton_->setIcon(UiTheme::icon(sidebarCollapsed_ ? QStringLiteral("panel-left-expand") : QStringLiteral("panel-left-collapse")));
    const QString action = sidebarCollapsed_ ? QStringLiteral("展开侧边栏") : QStringLiteral("收起侧边栏");
    sidebarToggleButton_->setAccessibleName(action);
    sidebarToggleButton_->setToolTip(action);
}

QFrame *MainWindow::makeAppearanceSection(QWidget *parent)
{
    auto *section = makeSection(parent);
    auto *row = new QHBoxLayout(section);
    UiTheme::setLayoutMetrics(row, {24, 22, 24, 24}, 8);
    auto *title = new QLabel(QStringLiteral("风格与布局"), section);
    title->setObjectName(QStringLiteral("cardTitle"));
    row->addWidget(title);
    row->addStretch(1);

    auto *palette = new QComboBox(section);
    palette->setObjectName(QStringLiteral("appearancePalette"));
    palette->addItem(UiTheme::paletteSwatch("mist"), QStringLiteral("云白 × 雾蓝"), QStringLiteral("mist"));
    palette->addItem(UiTheme::paletteSwatch("paper"), QStringLiteral("暖白 × 陶褐"), QStringLiteral("paper"));
    palette->addItem(UiTheme::paletteSwatch("precision"), QStringLiteral("冷灰 × 墨蓝"), QStringLiteral("precision"));

    auto *layout = new QComboBox(section);
    layout->setObjectName(QStringLiteral("appearanceLayout"));
    layout->addItem(UiTheme::icon("layout-standard"), QStringLiteral("标准"), QStringLiteral("standard"));
    layout->addItem(UiTheme::icon("layout-compact"), QStringLiteral("紧凑"), QStringLiteral("compact"));

    const auto addChoice = [&](QComboBox *choice, const QString &label, const QString &key, QString (*currentId)()) {
        auto *caption = new QLabel(label, section);
        caption->setBuddy(choice);
        choice->setAccessibleName(label);
        UiTheme::styleAppearanceChoice(choice);
        choice->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        choice->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        choice->setCurrentIndex(choice->findData(currentId()));
        row->addWidget(caption);
        row->addWidget(choice);
        connect(choice, &QComboBox::currentIndexChanged, this, [this, choice, key, currentId, palette, layout] {
            QString error;
            if (!database_.setSetting(key, choice->currentData().toString(), &error)) {
                const QSignalBlocker blocker(choice);
                choice->setCurrentIndex(choice->findData(currentId()));
                QMessageBox::warning(this, QStringLiteral("无法保存外观设置"), error);
                return;
            }
            UiTheme::apply(*qApp, palette->currentData().toString(), layout->currentData().toString());
        });
    };
    addChoice(palette, QStringLiteral("配色"), PaletteSetting, UiTheme::paletteId);
    row->addSpacing(12);
    addChoice(layout, QStringLiteral("布局"), LayoutSetting, UiTheme::layoutId);
    return section;
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

void MainWindow::switchToPage(int index, bool confirmLeave)
{
    if (!pageStack_) {
        return;
    }

    if (pageStack_->count() == 0) {
        const auto addWorkspace = [this](QWidget *root, int index) {
            auto *workspace = new ContentWorkspace(root, pageStack_);
            pageStack_->addWidget(workspace);
            workspace->setActivationGuard([this,workspace] {
                auto *current = qobject_cast<ContentWorkspace *>(pageStack_->currentWidget());
                return !current || current == workspace || current->confirmLeaveCurrent();
            });
            connect(workspace, &ContentWorkspace::activationRequested, this, [this,index] { switchToPage(index, false); });
        };
        auto *calendarPage = new CalendarPage(&database_, trayIcon_, pageStack_);
        addWorkspace(calendarPage,0);

        auto *projectsPage = new ProjectsPage(&database_, pageStack_);
        addWorkspace(projectsPage,1);

        auto *automationPage = new AutomationPage(&database_, pageStack_);
        pageStack_->addWidget(automationPage);

        auto *clipboardPage = new ClipboardPage(&database_, pageStack_);
        addWorkspace(clipboardPage,3);
        ClipboardPaste::setTrackingEnabled(clipboardPage->pasteOnUse());
        connect(clipboardPage, &ClipboardPage::pasteOnUseChanged, this, &ClipboardPaste::setTrackingEnabled);
        connect(clipboardPage, &ClipboardPage::hideRequested, this, [this,clipboardPage](bool paste, quint32 sequence) {
            if (paste) {
                const auto reportManualPaste = [this, clipboardPage](const QString &message) {
                    clipboardPage->report(message);
                    if (trayIcon_ && trayIcon_->isVisible() && QSystemTrayIcon::supportsMessages())
                        trayIcon_->showMessage(QStringLiteral("剪贴板"),message,QSystemTrayIcon::Information,4000);
                };
                if (globalShortcut_->isRegistered() && globalShortcut_->binding() == QKeyCombination(Qt::ControlModifier, Qt::Key_V))
                    reportManualPaste(QStringLiteral("已复制。Ctrl+V 用于唤起窗口，请在目标应用的右键菜单中选择粘贴。"));
                else
                    ClipboardPaste::request(this, sequence, [reportManualPaste] {
                        reportManualPaste(QStringLiteral("已复制。请在目标窗口手动粘贴。"));
                    });
            }
            hide();
        });

        auto *settingsPage = new QWidget(pageStack_);
        settingsPage->setObjectName(QStringLiteral("settingsPage"));
        auto *settingsLayout = new QVBoxLayout(settingsPage);
        settingsLayout->setContentsMargins(0, 0, 0, 0);
        settingsLayout->setSpacing(0);

        auto *settingsCard = makeSection(settingsPage);
        auto *settingsCardLayout = new QVBoxLayout(settingsCard);
        UiTheme::setLayoutMetrics(settingsCardLayout, {24, 22, 24, 24}, 12);

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
        settingsCardLayout->addWidget(systemStartupButton_, 0, Qt::AlignLeft);
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

        settingsCardLayout->addWidget(new ShortcutSettings(*globalShortcut_, database_, settingsCard));

        settingsLayout->addWidget(settingsCard);
        settingsLayout->addWidget(makeAppearanceSection(settingsPage));
        auto *update = new UpdateWidget(settingsPage);
        UiTheme::setSurface(update, "section");
        UiTheme::setLayoutMetrics(update->layout(), {24, 22, 24, 24}, update->layout()->spacing());
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

    if (confirmLeave && index != pageStack_->currentIndex()) {
        auto *workspace = qobject_cast<ContentWorkspace *>(pageStack_->currentWidget());
        if (workspace && !workspace->confirmLeaveCurrent()) {
            for (int i = 0; i < navigationButtons_.size(); ++i)
                navigationButtons_.at(i)->setChecked(i == pageStack_->currentIndex());
            return;
        }
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

void MainWindow::toggleMainWindow()
{
    if (auto *dialog = QApplication::activeModalWidget()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }

    auto *popup = QApplication::activePopupWidget();
    const bool ownsPopup = popup && popup->parentWidget() && popup->parentWidget()->window() == this;
    bool inForeground = isActiveWindow() || ownsPopup;
#ifdef Q_OS_WIN
    if (QApplication::platformName() == QStringLiteral("windows")) {
        if (const auto foreground = GetForegroundWindow()) {
            inForeground = foreground == reinterpret_cast<HWND>(effectiveWinId())
                || (ownsPopup && foreground == reinterpret_cast<HWND>(popup->effectiveWinId()));
        }
    }
#endif
    if (mainWindowVisible() && inForeground) {
        if (ownsPopup) popup->hide();
        hide();
    } else {
        showMainWindow();
    }
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
        SetForegroundWindow(hwnd);
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

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    const auto *nativeMessage = static_cast<const MSG *>(message);
    if (eventType == "windows_generic_MSG" && nativeMessage->message == WM_NCACTIVATE
        && windowHandle() && windowHandle()->flags().testFlag(Qt::ExpandedClientAreaHint)) {
        // Qt already draws the expanded caption. DefWindowProc would paint a
        // second, classic caption over it when the window's activation changes.
        // Only suppress non-client painting; WM_ACTIVATE still reaches Qt.
        *result = TRUE;
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

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
    for (auto *workspace : findChildren<ContentWorkspace *>()) {
        if (!workspace->closeAll()) return false;
    }
    return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (closeToTray_ && trayIcon_ && trayIcon_->isVisible() && !allowQuit_) {
        event->ignore();
        auto *workspace = qobject_cast<ContentWorkspace *>(pageStack_->currentWidget());
        if (workspace && !workspace->confirmLeaveCurrent()) return;
        hide();
        return;
    }

    if (!allowQuit_ && !savePendingChanges()) {
        event->ignore();
        return;
    }
    event->accept();
}
