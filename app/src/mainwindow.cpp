#include "ui/theme.h"
#include "mainwindow.h"
#include "version.h"

#include "pages/calendarpage.h"
#include "pages/automationpage.h"
#include "pages/projectspage.h"
#include "pages/clipboardpage.h"
#include "platform/globalshortcut.h"
#include "platform/clipboardpaste.h"
#include "ui/contentworkspace.h"
#include "ui/motion.h"
#include "ui/navigationsidebar.h"
#include "ui/pagetransition.h"
#include "ui/settingsform.h"
#include "ui/skinlibrary.h"
#include "ui/skinmanagerdialog.h"
#include "widgets/shortcutsettings.h"
#include "update/updatewidget.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
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
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif


namespace {

constexpr int MaxSkinLabelWidth = 220;
constexpr int SettingsPageIndex = 4;
const QString PaletteSetting = QStringLiteral("appearance_palette");
const QString LayoutSetting = QStringLiteral("appearance_layout");
const QString MotionSetting = QStringLiteral("appearance_motion");
const QString CloseToTraySetting = QStringLiteral("close_to_tray");
const QString DefaultPalette = QStringLiteral("mist");
const QString SkinManagerKey = QStringLiteral("skin-library");
const QString SystemMotion = QStringLiteral("system");
const QString DisabledMotion = QStringLiteral("off");

UiMotion::Preference motionPreference(const QString &value)
{
    return value == DisabledMotion ? UiMotion::Preference::Off : UiMotion::Preference::FollowSystem;
}

QString currentMotionValue()
{
    return UiMotion::instance().preference() == UiMotion::Preference::Off ? DisabledMotion : SystemMotion;
}

QFrame *makeSection(QWidget *parent)
{
    auto *card = new QFrame(parent);
    UiTheme::setSurface(card, "section");
    return card;
}

void syncPaletteChoice(QComboBox *choice)
{
    const QSignalBlocker blocker(choice);
    choice->setCurrentIndex(choice->findData(UiTheme::paletteId()));
    choice->setToolTip(choice->currentData(Qt::ToolTipRole).toString());
}

void fillPaletteChoices(QComboBox *choice, const QList<SkinTheme> &installed)
{
    const QSignalBlocker blocker(choice);
    choice->clear();
    for (const auto &palette : UiTheme::builtInPalettes())
        choice->addItem(UiTheme::paletteSwatch(palette.id), palette.name, palette.id);
    for (const SkinTheme &skin : installed) {
        const QString label = choice->fontMetrics().elidedText(skin.name, Qt::ElideRight, MaxSkinLabelWidth);
        choice->addItem(UiTheme::paletteSwatch(skin.id), label, skin.id);
        const int row = choice->count() - 1;
        choice->setItemData(row, QStringLiteral("<qt>%1</qt>").arg(skin.name.toHtmlEscaped()), Qt::ToolTipRole);
        choice->setItemData(row, skin.name, Qt::AccessibleTextRole);
    }
    syncPaletteChoice(choice);
}
} // namespace

MainWindow::MainWindow(QWidget *parent, std::unique_ptr<AutoStart> autoStart, QNetworkAccessManager *skinNetwork)
    : QMainWindow(parent), autoStart_(autoStart ? std::move(autoStart) : std::make_unique<AutoStart>())
{
    setWindowTitle(QApplication::applicationDisplayName());
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
    UiMotion::instance().setPreference(motionPreference(database_.setting(MotionSetting, SystemMotion)));
    closeToTray_ = database_.setting(CloseToTraySetting, QStringLiteral("true")) == QStringLiteral("true");

    globalShortcut_ = new GlobalShortcut(database_.setting("global_shortcut_enabled", "true") == "true",
                                         database_.setting("global_shortcut_binding"), this);
    connect(globalShortcut_, &GlobalShortcut::activated, this, &MainWindow::toggleMainWindow);
    buildUi();
    updateSidebarAppearance(database_.setting(QStringLiteral("sidebar_collapsed"), QStringLiteral("false")) == QStringLiteral("true"));
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
#if ORCHESTRATE_DEV_BUILD
    auto *buildName = new QLabel(windowTitle(), header);
    buildName->setObjectName(QStringLiteral("devBuildName"));
    buildName->setAttribute(Qt::WA_TransparentForMouseEvents);
    UiTheme::setRole(buildName, "muted");
    headerLayout->addWidget(buildName);
#endif
    rootLayout->addWidget(header);
    connect(sidebarToggleButton_, &QPushButton::clicked, this, &MainWindow::toggleSidebar);

    auto *bodyLayout = new QHBoxLayout;
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    sidebar_ = new NavigationSidebar(root);
    buildNavigation();
    sidebar_->setArtworkPath(UiTheme::sidebarImagePath());
    bodyLayout->addWidget(sidebar_);

    pageStack_ = new QStackedWidget(root);
    pageStack_->setObjectName(QStringLiteral("pageStack"));
    bodyLayout->addWidget(pageStack_, 1);
    rootLayout->addLayout(bodyLayout, 1);

    setCentralWidget(root);
}

void MainWindow::buildNavigation()
{
    const std::pair<QString, QString> entries[] = {
        {QStringLiteral("日历"), QStringLiteral("calendar")},
        {QStringLiteral("项目"), QStringLiteral("folder")},
        {QStringLiteral("工具"), QStringLiteral("toolbox")},
        {QStringLiteral("剪贴板"), QStringLiteral("clipboard")},
        {QStringLiteral("设置"), QStringLiteral("settings")},
    };

    int index = 0;
    for (const auto &[label, icon] : entries) {
        auto *button = sidebar_->addButton(label, icon, index == SettingsPageIndex);
        navigationButtons_.append(button);
        connect(button, &QPushButton::clicked, this, [this, index] { switchToPage(index); });
        ++index;
    }
}

void MainWindow::toggleSidebar()
{
    const bool collapsed = !sidebar_->isCollapsed();
    QString error;
    if (!database_.setSetting(QStringLiteral("sidebar_collapsed"), collapsed ? QStringLiteral("true") : QStringLiteral("false"), &error)) {
        QMessageBox::warning(this, QStringLiteral("无法保存侧边栏设置"), error);
        return;
    }
    updateSidebarAppearance(collapsed, true);
}

void MainWindow::updateSidebarAppearance(bool collapsed, bool animated)
{
    sidebar_->setCollapsed(collapsed, animated);
    sidebarToggleButton_->setIcon(UiTheme::icon(collapsed ? QStringLiteral("panel-left-expand") : QStringLiteral("panel-left-collapse")));
    const QString action = collapsed ? QStringLiteral("展开侧边栏") : QStringLiteral("收起侧边栏");
    sidebarToggleButton_->setAccessibleName(action);
    sidebarToggleButton_->setToolTip(action);
}

QFrame *MainWindow::makeAppearanceSection(QWidget *parent)
{
    auto *section = makeSection(parent);
    section->setObjectName(QStringLiteral("settingsAppearanceSection"));
    auto *body = new QVBoxLayout(section);
    UiTheme::setLayoutMetrics(body, {0, 0, 0, 20}, 12);
    auto *heading = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("风格与布局"), section);
    title->setObjectName(QStringLiteral("cardTitle"));
    heading->addWidget(title, 1);
    body->addLayout(heading);
    auto *samplesAndChoices = new QHBoxLayout;
    UiTheme::setLayoutMetrics(samplesAndChoices, {0, 0, 0, 0}, 28);
    samplesAndChoices->addWidget(UiTheme::createAppearancePreview(section), 3);
    auto *choices = new QWidget(section);
    choices->setMaximumWidth(400);
    auto *options = new QGridLayout(choices);
    SettingsForm::configure(*options, SettingsForm::ControlColumn);
    options->setVerticalSpacing(12);
    samplesAndChoices->addWidget(choices, 2, Qt::AlignVCenter);
    body->addLayout(samplesAndChoices);

    auto *palette = new QComboBox(choices);
    paletteChoice_ = palette;
    palette->setObjectName(QStringLiteral("appearancePalette"));
    auto *layout = new QComboBox(choices);
    layout->setObjectName(QStringLiteral("appearanceLayout"));
    layout->addItem(UiTheme::icon("layout-standard"), QStringLiteral("标准"), QStringLiteral("standard"));
    layout->addItem(UiTheme::icon("layout-compact"), QStringLiteral("紧凑"), QStringLiteral("compact"));
    auto *motion = new QComboBox(choices);
    motion->setObjectName(QStringLiteral("appearanceMotion"));
    motion->addItem(QStringLiteral("跟随系统"), SystemMotion);
    motion->addItem(QStringLiteral("关闭"), DisabledMotion);
    motion->setCurrentIndex(motion->findData(currentMotionValue()));

    for (auto *choice : {palette, layout, motion}) {
        UiTheme::styleAppearanceChoice(choice);
        choice->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        choice->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    }
    layout->setCurrentIndex(layout->findData(UiTheme::layoutId()));
    SettingsForm::addRow(*options, 0, QStringLiteral("风格"), palette);
    SettingsForm::addRow(*options, 1, QStringLiteral("布局"), layout);
    connect(palette, &QComboBox::currentIndexChanged, this, [this, palette](int index) {
        if (index < 0) return;
        QString error;
        if (!selectPalette(palette->currentData().toString(), &error)) {
            syncPaletteChoice(palette);
            QMessageBox::warning(this, QStringLiteral("无法保存外观设置"), error);
        }
    });
    connect(layout, &QComboBox::currentIndexChanged, this, [this, layout](int index) {
        if (index < 0) return;
        const QString id = layout->currentData().toString();
        QString error;
        if (!database_.setSetting(LayoutSetting, id, &error)) {
            const QSignalBlocker blocker(layout);
            layout->setCurrentIndex(layout->findData(UiTheme::layoutId()));
            QMessageBox::warning(this, QStringLiteral("无法保存外观设置"), error);
            return;
        }
        applyAppearance(UiTheme::paletteId(), id);
    });
    SettingsForm::addRow(*options, 2, QStringLiteral("界面动效"), motion);
    auto *motionStatus = SettingsForm::addStatus(*options, 3, QStringLiteral("appearanceMotionStatus"), choices);
    connect(motion, &QComboBox::currentIndexChanged, this, [this, motion, motionStatus](int index) {
        if (index < 0) return;
        const QString value = motion->currentData().toString();
        QString error;
        if (!database_.setSetting(MotionSetting, value, &error)) {
            const QSignalBlocker blocker(motion);
            motion->setCurrentIndex(motion->findData(currentMotionValue()));
            motionStatus->setText(QStringLiteral("无法保存界面动效设置，请检查数据目录的写入权限后重试。"));
            motionStatus->show();
            qWarning().noquote() << QStringLiteral("保存界面动效设置失败") << error;
            return;
        }
        UiMotion::instance().setPreference(motionPreference(value));
        motionStatus->clear();
        motionStatus->hide();
    });
    palette->ensurePolished();
    fillPaletteChoices(palette, UiTheme::installedThemes());

    auto *manage = new QPushButton(UiTheme::icon(QStringLiteral("chevron-right")), QStringLiteral("主题库"), section);
    manage->setObjectName(QStringLiteral("manageSkinsButton"));
    manage->setFlat(true);
    manage->setLayoutDirection(Qt::RightToLeft);
    manage->setIconSize(QSize(12, 12));
    manage->setCursor(Qt::PointingHandCursor);
    heading->addWidget(manage);
    QWidget::setTabOrder(manage, palette);
    QWidget::setTabOrder(palette, layout);
    QWidget::setTabOrder(layout, motion);
    connect(manage, &QPushButton::clicked, this, [this, manage] { openSkinManager(manage); });

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
    const auto activate = [this](const QString &id, QString *error) { return selectPalette(id, error); };
    const auto remove = [this](const QString &id, QString *error) {
        if (id != UiTheme::paletteId()) return skinLibrary_->removeTheme(id, error);
        return skinLibrary_->removeTheme(id, error, [this](QString *saveError) {
            return database_.setSetting(PaletteSetting, DefaultPalette, saveError);
        });
    };
    auto *dialog = new SkinManagerDialog(*skinLibrary_, activate, remove, settingsWorkspace_);
    settingsWorkspace_->present(SkinManagerKey, dialog, source);
    dialog->refreshCatalog();
}

bool MainWindow::selectPalette(const QString &id, QString *error)
{
    if (id == UiTheme::paletteId()) return true;
    if (!database_.setSetting(PaletteSetting, id, error)) return false;
    applyAppearance(id, UiTheme::layoutId());
    syncPaletteChoice(paletteChoice_);
    return true;
}

QFrame *MainWindow::makeRuntimeSection(QWidget *parent)
{
    auto *section = makeSection(parent);
    section->setObjectName(QStringLiteral("settingsRuntimeSection"));
    auto *body = new QVBoxLayout(section);
    UiTheme::setLayoutMetrics(body, {0, 0, 0, 20}, 12);
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
    auto *pageLayout = new QVBoxLayout(page);
    UiTheme::setLayoutMetrics(pageLayout, {30, 24, 30, 22}, 18);
    pageLayout->addWidget(makeAppearanceSection(page));
    pageLayout->addWidget(makeRuntimeSection(page));

    updateWidget_ = new UpdateWidget(page);
    updateWidget_->setAccessibleName(QStringLiteral("版本与更新"));
    updateWidget_->setFocusPolicy(Qt::ClickFocus);
    UiTheme::setSurface(updateWidget_, "content");
    UiTheme::setLayoutMetrics(updateWidget_->layout(), {0, 0, 0, 0}, 12);
    connect(updateWidget_, &UpdateWidget::installRequested, this, &MainWindow::installUpdate);
    connect(updateWidget_, &UpdateWidget::updateAvailableChanged, this, &MainWindow::setAvailableUpdate);
    connect(updateWidget_, &UpdateWidget::updateNotificationRequested, this, [this](const QString &tag) {
        if (trayIcon_ && trayIcon_->isVisible() && QSystemTrayIcon::supportsMessages())
            trayIcon_->showMessage(QStringLiteral("%1 有新版本").arg(QApplication::applicationDisplayName()),
                QStringLiteral("%1 已发布。打开“设置 → 版本与更新”查看说明并下载。").arg(tag),
                QSystemTrayIcon::Information, 8000);
    });
    pageLayout->addWidget(updateWidget_);
    pageLayout->addStretch();

    auto *scroll = new QScrollArea(pageStack_);
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    return scroll;
}

void MainWindow::buildTray()
{
    const QString appName = QApplication::applicationDisplayName();
    trayIcon_ = new QSystemTrayIcon(QApplication::windowIcon(), this);
    trayIcon_->setToolTip(appName);

    auto *trayMenu = new QMenu(this);
    auto *showAction = trayMenu->addAction(QStringLiteral("打开 %1").arg(appName));
    updateAction_ = trayMenu->addAction(QStringLiteral("查看更新"));
    updateAction_->setObjectName(QStringLiteral("viewUpdateAction"));
    updateAction_->setVisible(false);
    connect(updateAction_, &QAction::triggered, this, &MainWindow::openUpdatesPage);
    trayMenu->addSeparator();
    auto *quitAction = trayMenu->addAction(QStringLiteral("退出 %1").arg(appName));
    trayIcon_->setContextMenu(trayMenu);

    connect(showAction, &QAction::triggered, this, &MainWindow::showMainWindow);
    connect(trayIcon_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                    showMainWindow();
                }
            });
    connect(quitAction, &QAction::triggered, this, &MainWindow::quitApplication);

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
        if (workspace && !workspace->confirmLeaveCurrent()) return;
    }
    PageTransition::setCurrent(pageStack_, pageStack_->widget(index),
                               confirmLeave ? PageTransition::Direction::Module : PageTransition::Direction::Instant);
    if (index == SettingsPageIndex) refreshAutoStartSettings();
    sidebar_->setActiveButton(navigationButtons_.at(index), confirmLeave);
}

void MainWindow::setAvailableUpdate(const QString &tag)
{
    if (navigationButtons_.size() <= SettingsPageIndex) return;
    auto *settings = navigationButtons_.at(SettingsPageIndex);
    const auto text = tag.isEmpty() ? QStringLiteral("设置") : QStringLiteral("设置 · 有更新");
    sidebar_->setButtonLabel(settings, text);
    settings->setToolTip(tag.isEmpty() ? text : QStringLiteral("设置：有新版本 %1").arg(tag));
    settings->setAccessibleName(settings->toolTip());
    settings->setIcon(UiTheme::navigationIcon(tag.isEmpty() ? QStringLiteral("settings")
                                                           : QStringLiteral("update-available")));
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
            QStringLiteral("安装更新将退出并重启 %1。引用本目录的计划任务会临时暂停，结束后恢复原状态。个人数据会保留；新版启动确认且任务恢复后，自动清理本次旧版备份及临时文件。失败或恢复未完成则保留恢复材料。\n\n是否继续？").arg(QApplication::applicationDisplayName()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    if (!savePendingChanges()) return;
    QString error;
    if (!updateWidget_->startInstaller(&error)) {
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
