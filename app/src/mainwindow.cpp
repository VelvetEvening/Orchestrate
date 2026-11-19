#include "ui/theme.h"
#include "mainwindow.h"

#include "pages/calendarpage.h"
#include "pages/automationpage.h"
#include "pages/projectspage.h"
#include "pages/clipboardpage.h"
#include "platform/globalshortcut.h"
#include "platform/clipboardpaste.h"
#include "ui/contentworkspace.h"
#include "ui/settingsform.h"
#include "ui/skinlibrary.h"
#include "ui/skinmanagerdialog.h"
#include "widgets/shortcutsettings.h"
#include "update/updatewidget.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
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
constexpr int MaxSkinLabelWidth = 220;
constexpr int SettingsPageIndex = 4;
constexpr int SettingsColumnWidth = 624;
const QString PaletteSetting = QStringLiteral("appearance_palette");
const QString LayoutSetting = QStringLiteral("appearance_layout");
const QString CloseToTraySetting = QStringLiteral("close_to_tray");
const QString DefaultPalette = QStringLiteral("mist");
const QString SkinManagerKey = QStringLiteral("skin-library");

QFrame *makeSection(QWidget *parent)
{
    auto *card = new QFrame(parent);
    UiTheme::setSurface(card, "section");
    return card;
}

void fillPaletteChoices(QComboBox *choice, const QList<SkinTheme> &installed)
{
    const QSignalBlocker blocker(choice);
    choice->clear();
    choice->addItem(UiTheme::paletteSwatch(DefaultPalette), QStringLiteral("云白 × 雾蓝"), DefaultPalette);
    choice->addItem(UiTheme::paletteSwatch("paper"), QStringLiteral("暖白 × 陶褐"), QStringLiteral("paper"));
    for (const SkinTheme &skin : installed) {
        const QString label = choice->fontMetrics().elidedText(skin.name, Qt::ElideRight, MaxSkinLabelWidth);
        choice->addItem(UiTheme::paletteSwatch(skin.id), label, skin.id);
        const int row = choice->count() - 1;
        choice->setItemData(row, QStringLiteral("<qt>%1</qt>").arg(skin.name.toHtmlEscaped()), Qt::ToolTipRole);
        choice->setItemData(row, skin.name, Qt::AccessibleTextRole);
    }
    choice->setCurrentIndex(choice->findData(UiTheme::paletteId()));
    choice->setToolTip(choice->currentData(Qt::ToolTipRole).toString());
}

} // namespace

class MainWindow::NavigationSidebar final : public QFrame
{
public:
    using QFrame::QFrame;

    void setArtworkPath(const QString &path)
    {
        if (path == artworkPath_) return;
        artworkPath_ = path;
        artwork_ = path.isEmpty() ? QPixmap() : QPixmap(path);
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QFrame::paintEvent(event);
        if (artwork_.isNull() || width() <= CollapsedSidebarWidth) return;
        // Keep artwork in the stretch between the navigation entries and Settings.
        for (int i = 0; i < layout()->count(); ++i) {
            const auto *spacer = layout()->itemAt(i)->spacerItem();
            if (!spacer) continue;
            const QRect gap = spacer->geometry();
            if (gap.height() <= 0) return;
            const QSize size = artwork_.size().scaled(width(), gap.height(), Qt::KeepAspectRatio);
            const int top = qBound(gap.top(), qRound(height() * 0.72 - size.height() * 0.52),
                                   gap.bottom() + 1 - size.height());
            QPainter painter(this);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.setOpacity(0.76);
            painter.drawPixmap(QRect((width() - size.width()) / 2, top, size.width(), size.height()), artwork_);
            return;
        }
    }

private:
    QString artworkPath_;
    QPixmap artwork_;
};

MainWindow::MainWindow(QWidget *parent, std::unique_ptr<AutoStart> autoStart, QNetworkAccessManager *skinNetwork)
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

    const QString skinDirectory = QFileInfo(database_.databasePath()).dir().filePath(QStringLiteral("skins"));
    skinLibrary_ = new SkinLibrary(skinDirectory, this, SkinLibrary::defaultCatalogUrl(), skinNetwork);
    UiTheme::setInstalledThemes(skinLibrary_->installedThemes());

    // Read each preference independently so a retired color retains its saved layout density.
    const QString legacyPreset = database_.setting(QStringLiteral("appearance_preset"), DefaultPalette);
    const QString legacyLayout = legacyPreset == QStringLiteral("precision") ? QStringLiteral("compact") : QStringLiteral("standard");
    const QString savedPalette = database_.setting(PaletteSetting, legacyPreset);
    applyAppearance(savedPalette, database_.setting(LayoutSetting, legacyLayout));
    if (savedPalette != UiTheme::paletteId()) database_.setSetting(PaletteSetting, UiTheme::paletteId());
    closeToTray_ = database_.setting(CloseToTraySetting, QStringLiteral("true")) == QStringLiteral("true");

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
    sidebar_ = new NavigationSidebar(root);
    sidebar_->setObjectName(QStringLiteral("sidebar"));
    buildNavigation(sidebar_);
    sidebar_->setArtworkPath(UiTheme::sidebarImagePath());
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
    section->setObjectName(QStringLiteral("settingsAppearanceSection"));
    auto *body = new QVBoxLayout(section);
    UiTheme::setLayoutMetrics(body, {0, 20, 0, 22}, 10);
    auto *title = new QLabel(QStringLiteral("风格与布局"), section);
    title->setObjectName(QStringLiteral("cardTitle"));
    body->addWidget(title);
    auto *options = new QGridLayout;
    SettingsForm::configure(*options);
    body->addLayout(options);

    auto *palette = new QComboBox(section);
    palette->setObjectName(QStringLiteral("appearancePalette"));
    auto *layout = new QComboBox(section);
    layout->setObjectName(QStringLiteral("appearanceLayout"));
    layout->addItem(UiTheme::icon("layout-standard"), QStringLiteral("标准"), QStringLiteral("standard"));
    layout->addItem(UiTheme::icon("layout-compact"), QStringLiteral("紧凑"), QStringLiteral("compact"));

    const auto addChoice = [&](int row, QComboBox *choice, const QString &label, const QString &key, QString (*currentId)()) {
        UiTheme::styleAppearanceChoice(choice);
        choice->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        choice->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        choice->setCurrentIndex(choice->findData(currentId()));
        SettingsForm::addRow(*options, row, label, choice);
        connect(choice, &QComboBox::currentIndexChanged, this, [this, choice, key, currentId, palette, layout] {
            QString error;
            if (!database_.setSetting(key, choice->currentData().toString(), &error)) {
                const QSignalBlocker blocker(choice);
                choice->setCurrentIndex(choice->findData(currentId()));
                QMessageBox::warning(this, QStringLiteral("无法保存外观设置"), error);
                return;
            }
            applyAppearance(palette->currentData().toString(), layout->currentData().toString());
        });
    };
    addChoice(0, palette, QStringLiteral("风格"), PaletteSetting, UiTheme::paletteId);
    addChoice(1, layout, QStringLiteral("布局"), LayoutSetting, UiTheme::layoutId);
    palette->ensurePolished();
    fillPaletteChoices(palette, UiTheme::installedThemes());

    auto *manage = new QPushButton(UiTheme::icon(QStringLiteral("chevron-right")), QStringLiteral("管理皮肤"), section);
    manage->setObjectName(QStringLiteral("manageSkinsButton"));
    manage->setFlat(true);
    manage->setLayoutDirection(Qt::RightToLeft);
    manage->setIconSize(QSize(12, 12));
    manage->setCursor(Qt::PointingHandCursor);
    options->addWidget(manage, 0, SettingsForm::AccessoryColumn, Qt::AlignLeft | Qt::AlignAbsolute | Qt::AlignVCenter);
    QWidget::setTabOrder(manage, palette);
    QWidget::setTabOrder(palette, layout);
    connect(manage, &QPushButton::clicked, this, [this, manage] { openSkinManager(manage); });

    connect(palette, &QComboBox::currentIndexChanged, palette, [palette] {
        palette->setToolTip(palette->currentData(Qt::ToolTipRole).toString());
    });
    connect(skinLibrary_, &SkinLibrary::installedChanged, section, [this, palette] {
        const auto installed = skinLibrary_->installedThemes();
        UiTheme::setInstalledThemes(installed);
        applyAppearance(UiTheme::paletteId(), UiTheme::layoutId());
        fillPaletteChoices(palette, installed);
    });
    return section;
}

void MainWindow::applyAppearance(const QString &palette, const QString &layout)
{
    UiTheme::apply(*qApp, palette, layout);
    if (sidebar_) sidebar_->setArtworkPath(UiTheme::sidebarImagePath());
    if (settingsWorkspace_) {
        if (auto *dialog = static_cast<SkinManagerDialog *>(settingsWorkspace_->content(SkinManagerKey)))
            dialog->updateActiveSkin();
    }
}

void MainWindow::openSkinManager(QWidget *source)
{
    if (settingsWorkspace_->activate(SkinManagerKey, source)) return;
    auto *dialog = new SkinManagerDialog(*skinLibrary_, [this](const QString &id, QString *error) {
        if (id != UiTheme::paletteId()) return skinLibrary_->removeTheme(id, error);
        return skinLibrary_->removeTheme(id, error, [this](QString *saveError) {
            return database_.setSetting(PaletteSetting, DefaultPalette, saveError);
        });
    }, settingsWorkspace_);
    settingsWorkspace_->present(SkinManagerKey, dialog, source);
    dialog->refreshCatalog();
}

QFrame *MainWindow::makeRuntimeSection(QWidget *parent)
{
    auto *section = makeSection(parent);
    auto *body = new QVBoxLayout(section);
    UiTheme::setLayoutMetrics(body, {0, 20, 0, 22}, 10);
    auto *title = new QLabel(QStringLiteral("运行与托盘"), section);
    title->setObjectName(QStringLiteral("cardTitle"));
    body->addWidget(title);

    auto *options = new QGridLayout;
    SettingsForm::configure(*options);
    body->addLayout(options);
    autoStartToggle_ = SettingsForm::makeToggle(section, QStringLiteral("autoStartEnabled"), false);
    SettingsForm::addRow(*options, 0, QStringLiteral("开机自动启动"), autoStartToggle_);
    autoStartStatus_ = SettingsForm::addStatus(*options, 1, QStringLiteral("autoStartStatus"), section);
    systemStartupButton_ = new QPushButton(QStringLiteral("打开 Windows 启动应用设置"), section);
    systemStartupButton_->setObjectName(QStringLiteral("openWindowsStartupSettings"));
    options->addWidget(systemStartupButton_, 2, SettingsForm::LabelColumn, 1, SettingsForm::ColumnCount, Qt::AlignLeft);
    connect(autoStartToggle_, &QPushButton::toggled, this, [this](bool enabled) {
        QString error;
        autoStart_->setEnabled(enabled, &error);
        refreshAutoStartSettings(error);
    });
    connect(systemStartupButton_, &QPushButton::clicked, this, [this] {
        if (!QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:startupapps"))))
            refreshAutoStartSettings(QStringLiteral("无法打开系统设置，请手动进入 Windows 设置 → 应用 → 启动。"));
    });
    refreshAutoStartSettings();

    auto *trayToggle = SettingsForm::makeToggle(section, QStringLiteral("closeToTrayEnabled"), closeToTray_);
    SettingsForm::addRow(*options, 3, QStringLiteral("关闭窗口时驻留托盘"), trayToggle);
    auto *trayStatus = SettingsForm::addStatus(*options, 4, QStringLiteral("closeToTrayStatus"), section);
    connect(trayToggle, &QPushButton::toggled, this, [this, trayToggle, trayStatus](bool enabled) {
        QString error;
        if (!database_.setSetting(CloseToTraySetting, enabled ? QStringLiteral("true") : QStringLiteral("false"), &error)) {
            SettingsForm::setToggleValue(*trayToggle, closeToTray_);
            trayStatus->setText(QStringLiteral("无法保存托盘设置。\n") + error);
            trayStatus->show();
            return;
        }
        closeToTray_ = enabled;
        trayStatus->clear();
        trayStatus->hide();
    });
    body->addWidget(new ShortcutSettings(*globalShortcut_, database_, section));
    return section;
}

QScrollArea *MainWindow::makeSettingsPage()
{
    auto *page = new QWidget(pageStack_);
    page->setObjectName(QStringLiteral("settingsPage"));
    auto *pageLayout = new QHBoxLayout(page);
    UiTheme::setLayoutMetrics(pageLayout, {32, 4, 32, 24}, 0);
    auto *column = new QWidget(page);
    column->setMaximumWidth(SettingsColumnWidth);
    pageLayout->addWidget(column, 1);
    pageLayout->addStretch();

    auto *body = new QVBoxLayout(column);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(makeAppearanceSection(column));
    body->addWidget(makeRuntimeSection(column));

    updateWidget_ = new UpdateWidget(column);
    updateWidget_->setAccessibleName(QStringLiteral("版本与更新"));
    updateWidget_->setFocusPolicy(Qt::ClickFocus);
    UiTheme::setSurface(updateWidget_, "content");
    UiTheme::setLayoutMetrics(updateWidget_->layout(), {0, 20, 0, 0}, 9);
    connect(updateWidget_, &UpdateWidget::installRequested, this, &MainWindow::installUpdate);
    connect(updateWidget_, &UpdateWidget::updateAvailableChanged, this, &MainWindow::setAvailableUpdate);
    connect(updateWidget_, &UpdateWidget::updateNotificationRequested, this, [this](const QString &tag) {
        if (trayIcon_ && trayIcon_->isVisible() && QSystemTrayIcon::supportsMessages())
            trayIcon_->showMessage(QStringLiteral("Orchestrate 有新版本"),
                QStringLiteral("%1 已发布。打开“设置 → 版本与更新”查看说明并下载。").arg(tag),
                QSystemTrayIcon::Information, 8000);
    });
    body->addWidget(updateWidget_);
    body->addStretch();

    auto *scroll = new QScrollArea(pageStack_);
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    return scroll;
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
            return workspace;
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

        settingsScroll_ = makeSettingsPage();
        settingsWorkspace_ = addWorkspace(settingsScroll_, SettingsPageIndex);
        settingsWorkspace_->setObjectName(QStringLiteral("settingsWorkspace"));
        QWidget::setTabOrder(sidebarToggleButton_, navigationButtons_.first());
        updateWidget_->startAutomaticChecks();
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
    if (index == SettingsPageIndex) refreshAutoStartSettings();
    for (int i = 0; i < navigationButtons_.size(); ++i) {
        navigationButtons_.at(i)->setChecked(i == index);
    }
}

void MainWindow::setAvailableUpdate(const QString &tag)
{
    if (navigationButtons_.size() <= SettingsPageIndex) return;
    auto *settings = navigationButtons_.at(SettingsPageIndex);
    const auto text = tag.isEmpty() ? QStringLiteral("设置") : QStringLiteral("设置 · 有更新");
    settings->setProperty("expandedText", text);
    settings->setText(sidebarCollapsed_ ? QString() : text);
    settings->setToolTip(tag.isEmpty() ? text : QStringLiteral("设置：有新版本 %1").arg(tag));
    settings->setAccessibleName(settings->toolTip());
    settings->setIcon(tag.isEmpty() ? UiTheme::icon(QStringLiteral("settings"))
                                   : UiTheme::icon(QStringLiteral("update-available")));
    if (updateAction_) {
        updateAction_->setText(QStringLiteral("查看更新（%1）").arg(tag));
        updateAction_->setVisible(!tag.isEmpty());
    }
}

void MainWindow::openUpdatesPage()
{
    showMainWindow();
    switchToPage(SettingsPageIndex);
    if (!settingsWorkspace_ || pageStack_->currentWidget() != settingsWorkspace_) return;
    settingsWorkspace_->back();
    QTimer::singleShot(0, this, [this] {
        if (pageStack_->currentWidget() != settingsWorkspace_ || !settingsWorkspace_->currentKey().isEmpty()) return;
        updateWidget_->setFocus();
        const int top = updateWidget_->mapTo(settingsScroll_->widget(), QPoint()).y();
        settingsScroll_->verticalScrollBar()->setValue(top);
    });
}

void MainWindow::refreshAutoStartSettings(const QString &error)
{
    if (!autoStartToggle_) return;
    const auto state = autoStart_->state();
    SettingsForm::setToggleValue(*autoStartToggle_, state.enabled);
    autoStartToggle_->setEnabled(state.error.isEmpty());
    systemStartupButton_->setVisible(state.disabledByWindows);
    QStringList notices;
    if (!error.isEmpty()) notices.append(error);
    if (!state.error.isEmpty() && state.error != error) notices.append(state.error);
    if (!state.message.isEmpty()) notices.append(state.message);
    UiTheme::setRole(autoStartStatus_, error.isEmpty() && state.error.isEmpty() ? "warning" : "error");
    autoStartStatus_->setText(notices.join(QLatin1Char('\n')));
    autoStartStatus_->setVisible(!notices.isEmpty());
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
