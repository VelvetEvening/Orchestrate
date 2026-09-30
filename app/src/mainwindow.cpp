#include "mainwindow.h"

#include "pages/calendarpage.h"
#include "pages/automationpage.h"
#include "pages/projectspage.h"
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

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif


namespace {

// Paint the native title bar in the sidebar navy so the default white caption
// doesn't sit on top of the app. Windows 11 honours caption/text colours; older
// builds silently ignore them and fall back to the dark-mode flag or the default.
void applyTitleBarColors(QWidget *window)
{
#ifdef Q_OS_WIN
    constexpr DWORD kUseImmersiveDarkMode = 20;
    constexpr DWORD kBorderColor = 34;
    constexpr DWORD kCaptionColor = 35;
    constexpr DWORD kTextColor = 36;

    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL darkMode = TRUE;
    const COLORREF caption = RGB(0x15, 0x22, 0x38);
    const COLORREF text = RGB(0xff, 0xff, 0xff);
    DwmSetWindowAttribute(hwnd, kUseImmersiveDarkMode, &darkMode, sizeof(darkMode));
    DwmSetWindowAttribute(hwnd, kCaptionColor, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, kBorderColor, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, kTextColor, &text, sizeof(text));
#else
    Q_UNUSED(window);
#endif
}

QLabel *makeSectionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("sectionLabel"));
    return label;
}

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
    applyTitleBarColors(this);
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
    buildTray();
    switchToPage(0);
}

void MainWindow::buildUi()
{
    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("root"));

    auto *rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    sidebar_ = new QFrame(root);
    sidebar_->setObjectName(QStringLiteral("sidebar"));
    sidebar_->setMinimumWidth(72);
    sidebar_->setMaximumWidth(260);
    sidebar_->setFixedWidth(240);
    buildNavigation(sidebar_);
    rootLayout->addWidget(sidebar_);

    pageStack_ = new QStackedWidget(root);
    pageStack_->setObjectName(QStringLiteral("pageStack"));
    rootLayout->addWidget(pageStack_, 1);

    setCentralWidget(root);

    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget#root {
            background: #f5f7fb;
            color: #1f2937;
        }
        QFrame#sidebar {
            background: #152238;
            border: none;
        }
        QLabel#brand {
            color: #ffffff;
            font-size: 24px;
            font-weight: 700;
        }
        QLabel#brandSubtitle {
            color: #9fb1c9;
            font-size: 12px;
        }
        QLabel#sectionLabel {
            color: #8ea2bd;
            font-size: 11px;
            font-weight: 700;
            letter-spacing: 1px;
            padding-top: 14px;
            padding-bottom: 4px;
        }
        QPushButton#sidebarToggle {
            color: #c9d5e6;
            background: #223451;
            border: 1px solid #385071;
            border-radius: 8px;
            min-width: 34px;
            min-height: 34px;
            max-width: 34px;
            max-height: 34px;
            padding: 0;
        }
        QPushButton#sidebarToggle:hover {
            color: #ffffff;
            background: #2f6fed;
            border-color: #2f6fed;
        }
        QPushButton#navButton {
            color: #c9d5e6;
            background: transparent;
            border: 1px solid transparent;
            border-radius: 9px;
            text-align: left;
            padding: 10px 14px;
            min-height: 22px;
            font-size: 14px;
        }
        QPushButton#navButton:hover {
            color: #ffffff;
            background: #223451;
            border-color: #385071;
        }
        QPushButton#navButton:checked {
            color: #ffffff;
            background: #2f6fed;
            border-color: #4d83f4;
            font-weight: 600;
        }
        QLabel#pageTitle {
            color: #111827;
            font-size: 26px;
            font-weight: 700;
        }
        QLabel#pageSubtitle {
            color: #6b7280;
            font-size: 13px;
        }
        QFrame#card {
            background: #ffffff;
            border: 1px solid #e3e8f0;
            border-radius: 10px;
        }
        QLabel#cardTitle {
            color: #1f2937;
            font-size: 16px;
            font-weight: 600;
        }
        QLabel#muted {
            color: #6b7280;
            font-size: 13px;
        }
        QLabel#emptyStateTitle {
            color: #334155;
            font-size: 18px;
            font-weight: 600;
        }
        QPushButton {
            color: #334155;
            background: #ffffff;
            border: 1px solid #d5dce8;
            border-radius: 6px;
            padding: 7px 14px;
        }
        QPushButton:hover {
            color: #1f2937;
            background: #f3f6fb;
            border-color: #b9c6da;
        }
        QPushButton:pressed {
            background: #e7edf6;
        }
        QPushButton:disabled {
            color: #a3adbb;
            background: #f7f9fc;
            border-color: #e5e9f0;
        }
        QPushButton#primaryButton {
            color: #ffffff;
            background: #2f6fed;
            border: 1px solid #2f6fed;
            border-radius: 6px;
            padding: 7px 14px;
            font-weight: 600;
        }
        QPushButton#primaryButton:hover {
            background: #255dcc;
            border-color: #255dcc;
        }
        QPushButton#primaryButton:disabled {
            background: #a9c1f5;
            border-color: #a9c1f5;
        }
        QPushButton#dangerButton {
            color: #dc2626;
            border-color: #f1c7c7;
        }
        QPushButton#dangerButton:hover {
            background: #fef2f2;
            border-color: #e79c9c;
        }
        QLineEdit, QPlainTextEdit, QTextEdit, QAbstractSpinBox, QComboBox {
            color: #1f2937;
            background: #ffffff;
            border: 1px solid #d5dce8;
            border-radius: 6px;
            padding: 5px 8px;
            selection-background-color: #bfd3fb;
            selection-color: #111827;
        }
        QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QAbstractSpinBox:focus, QComboBox:focus {
            border-color: #2f6fed;
        }
        QLineEdit:disabled, QAbstractSpinBox:disabled, QComboBox:disabled {
            color: #9aa4b2;
            background: #f5f7fa;
        }
        QComboBox {
            padding-right: 28px;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: center right;
            width: 26px;
            border: none;
        }
        QComboBox::down-arrow {
            image: url(:/icons/chevron-down.svg);
            width: 12px;
            height: 12px;
        }
        QComboBox QAbstractItemView {
            background: #ffffff;
            border: 1px solid #d5dce8;
            padding: 4px;
            outline: none;
            selection-background-color: #eaf1ff;
            selection-color: #1d4ed8;
        }
        QAbstractSpinBox {
            padding-right: 24px;
        }
        QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {
            subcontrol-origin: border;
            width: 22px;
            border: none;
            border-left: 1px solid #e5e9f0;
        }
        QAbstractSpinBox::up-button {
            subcontrol-position: top right;
            border-top-right-radius: 6px;
        }
        QAbstractSpinBox::down-button {
            subcontrol-position: bottom right;
            border-bottom-right-radius: 6px;
        }
        QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover {
            background: #eef4ff;
        }
        QAbstractSpinBox::up-arrow {
            image: url(:/icons/chevron-up.svg);
            width: 10px;
            height: 10px;
        }
        QAbstractSpinBox::down-arrow {
            image: url(:/icons/chevron-down.svg);
            width: 10px;
            height: 10px;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border: 1px solid #b9c6da;
            border-radius: 4px;
            background: #ffffff;
        }
        QCheckBox::indicator:hover {
            border-color: #2f6fed;
        }
        QCheckBox::indicator:checked {
            background: #2f6fed;
            border-color: #2f6fed;
            image: url(:/icons/check.svg);
        }
        QScrollBar:vertical {
            background: transparent;
            width: 10px;
            margin: 2px;
        }
        QScrollBar:horizontal {
            background: transparent;
            height: 10px;
            margin: 2px;
        }
        QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
            background: #cbd5e1;
            border-radius: 3px;
            min-height: 30px;
            min-width: 30px;
        }
        QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover {
            background: #94a3b8;
        }
        QScrollBar::add-line, QScrollBar::sub-line {
            width: 0;
            height: 0;
        }
        QScrollBar::add-page, QScrollBar::sub-page {
            background: none;
        }
        QMenu {
            background: #ffffff;
            border: 1px solid #dde3ec;
            padding: 5px;
        }
        QMenu::item {
            color: #1f2937;
            padding: 7px 26px 7px 14px;
            border-radius: 5px;
        }
        QMenu::item:selected {
            background: #eaf1ff;
            color: #1d4ed8;
        }
        QMenu::item:disabled {
            color: #a3adbb;
        }
        QMenu::separator {
            height: 1px;
            background: #e5e9f0;
            margin: 5px 8px;
        }
        QToolTip {
            color: #f8fafc;
            background: #1f2937;
            border: none;
            padding: 6px 8px;
        }
        QDialog {
            background: #ffffff;
        }
        QCalendarWidget {
            background: #ffffff;
        }
        QCalendarWidget QToolButton {
            color: #1f2937;
            font-weight: 600;
            background: transparent;
            padding: 5px;
        }
        QCalendarWidget QSpinBox {
            min-width: 70px;
        }
        QListWidget {
            border: 1px solid #e3e8f0;
            border-radius: 6px;
            background: #ffffff;
            padding: 4px;
        }
        QCheckBox {
            color: #334155;
            font-size: 13px;
        }
    )"));
}

void MainWindow::buildNavigation(QWidget *parent)
{
    sidebarLayout_ = new QVBoxLayout(parent);
    sidebarLayout_->setContentsMargins(14, 18, 14, 16);
    sidebarLayout_->setSpacing(6);

    auto *header = new QHBoxLayout;
    header->setSpacing(8);
    brandLabel_ = new QLabel(QStringLiteral("Orchestrate"), parent);
    brandLabel_->setObjectName(QStringLiteral("brand"));
    header->addWidget(brandLabel_);
    header->addStretch(1);
    sidebarToggleButton_ = new QPushButton(parent);
    sidebarToggleButton_->setObjectName(QStringLiteral("sidebarToggle"));
    sidebarToggleButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowLeft));
    sidebarToggleButton_->setIconSize(QSize(18, 18));
    sidebarToggleButton_->setToolTip(QStringLiteral("收起侧边栏"));
    sidebarToggleButton_->setCursor(Qt::PointingHandCursor);
    header->addWidget(sidebarToggleButton_);
    sidebarLayout_->addLayout(header);


    sidebarLayout_->addSpacing(22);
    sectionLabel_ = makeSectionLabel(QStringLiteral("工作台"), parent);
    sidebarLayout_->addWidget(sectionLabel_);

    const QStringList labels {
        QStringLiteral("时序记录"),
        QStringLiteral("项目记录"),
        QStringLiteral("自动化工具"),
        QStringLiteral("设置")
    };

    const QList<QStyle::StandardPixmap> icons {
        QStyle::SP_FileDialogDetailedView,
        QStyle::SP_DirHomeIcon,
        QStyle::SP_ComputerIcon,
        QStyle::SP_FileDialogContentsView
    };

    for (int i = 0; i < labels.size(); ++i) {
        auto *button = new QPushButton(labels.at(i), parent);
        button->setObjectName(QStringLiteral("navButton"));
        button->setProperty("expandedText", labels.at(i));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setIcon(style()->standardIcon(icons.at(i)));
        button->setIconSize(QSize(19, 19));
        button->setToolTip(labels.at(i));
        button->setCursor(Qt::PointingHandCursor);
        navigationButtons_.append(button);
        sidebarLayout_->addWidget(button);

        connect(button, &QPushButton::clicked, this, [this, i] {
            switchToPage(i);
        });
    }

    sidebarLayout_->addStretch(1);


    connect(sidebarToggleButton_, &QPushButton::clicked, this, &MainWindow::toggleSidebar);
}

void MainWindow::toggleSidebar()
{
    sidebarCollapsed_ = !sidebarCollapsed_;
    if (sidebar_ == nullptr) {
        return;
    }

    sidebar_->setFixedWidth(sidebarCollapsed_ ? 72 : 240);
    brandLabel_->setVisible(!sidebarCollapsed_);
    sectionLabel_->setVisible(!sidebarCollapsed_);

    for (QPushButton *button : navigationButtons_) {
        button->setText(sidebarCollapsed_ ? QString() : button->property("expandedText").toString());
        button->setToolTip(button->property("expandedText").toString());
        button->setStyleSheet(sidebarCollapsed_
                                  ? QStringLiteral("text-align: center; padding: 10px 0;")
                                  : QString());
    }

    sidebarToggleButton_->setIcon(style()->standardIcon(sidebarCollapsed_
                                                            ? QStyle::SP_ArrowRight
                                                            : QStyle::SP_ArrowLeft));
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

        auto *settingsPage = new QWidget(pageStack_);
        auto *settingsLayout = new QVBoxLayout(settingsPage);
        settingsLayout->setContentsMargins(34, 30, 34, 34);
        settingsLayout->setSpacing(18);

        auto *settingsTitle = new QLabel(QStringLiteral("设置"), settingsPage);
        settingsTitle->setObjectName(QStringLiteral("pageTitle"));
        settingsLayout->addWidget(settingsTitle);


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
        settingsLayout->addWidget(update);
        settingsLayout->addStretch(1);
        auto *settingsScroll = new QScrollArea(pageStack_);
        settingsScroll->setWidgetResizable(true);
        settingsScroll->setFrameShape(QFrame::NoFrame);
        settingsScroll->setWidget(settingsPage);
        pageStack_->addWidget(settingsScroll);
    }

    if (index < 0 || index >= pageStack_->count()) {
        return;
    }

    pageStack_->setCurrentIndex(index);
    if (index == 3) refreshAutoStartSettings();
    for (int i = 0; i < navigationButtons_.size(); ++i) {
        navigationButtons_.at(i)->setChecked(i == index);
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
    raise();
    activateWindow();
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
            QStringLiteral("安装更新将退出并重启 Orchestrate。引用本目录的计划任务会临时暂停，结束后恢复原状态。个人数据和完整旧版备份会保留；成功后自动清理更新临时文件。\n\n是否继续？"),
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
