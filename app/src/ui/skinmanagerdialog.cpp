#include "skinmanagerdialog.h"

#include "skinlibrary.h"
#include "theme.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <utility>

namespace {

constexpr int SkinIdRole = Qt::UserRole;
constexpr int InstalledRole = Qt::UserRole + 1;

QString tooltip(const QString &text)
{
    return QStringLiteral("<qt>%1</qt>").arg(text.toHtmlEscaped());
}

} // namespace

SkinManagerDialog::SkinManagerDialog(SkinLibrary &library, std::function<bool(const QString &, QString *)> removeTheme,
                                     QWidget *parent)
    : QDialog(parent), library_(library), removeTheme_(std::move(removeTheme)),
      content_(new QStackedWidget(this)), list_(new QTreeWidget(content_)),
      empty_(new QLabel(content_)), status_(new QLabel(this)),
      refresh_(new QPushButton(this)), action_(new QPushButton(this))
{
    setObjectName(QStringLiteral("skinManagerDialog"));
    setWindowTitle(QStringLiteral("管理皮肤"));
    setMinimumSize(420, 280);
    resize(520, 360);

    auto *body = new QVBoxLayout(this);
    UiTheme::setLayoutMetrics(body, {20, 18, 20, 18}, 12);
    body->addWidget(content_, 1);

    list_->setObjectName(QStringLiteral("skinManagerList"));
    list_->setAccessibleName(QStringLiteral("皮肤列表"));
    list_->setHeaderLabels({QStringLiteral("皮肤"), QStringLiteral("状态")});
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list_->setRootIsDecorated(false);
    list_->setAllColumnsShowFocus(true);
    list_->setIconSize(QSize(22, 16));
    content_->addWidget(list_);

    empty_->setAlignment(Qt::AlignCenter);
    empty_->setWordWrap(true);
    UiTheme::setRole(empty_, "muted");
    content_->addWidget(empty_);

    status_->setObjectName(QStringLiteral("skinManagerStatus"));
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    body->addWidget(status_);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(8);
    refresh_->setObjectName(QStringLiteral("skinCatalogRefresh"));
    action_->setObjectName(QStringLiteral("skinActionButton"));
    auto *close = new QPushButton(QStringLiteral("关闭"), this);
    close->setObjectName(QStringLiteral("skinManagerClose"));
    for (auto *button : {refresh_, action_, close}) button->setAutoDefault(false);
    footer->addWidget(refresh_);
    footer->addStretch(1);
    footer->addWidget(action_);
    footer->addWidget(close);
    body->addLayout(footer);

    connect(list_, &QTreeWidget::currentItemChanged, this, &SkinManagerDialog::updateActions);
    connect(refresh_, &QPushButton::clicked, this, &SkinManagerDialog::refreshCatalog);
    connect(action_, &QPushButton::clicked, this, &SkinManagerDialog::actOnSelection);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(this, &QDialog::finished, &library_, &SkinLibrary::cancel);
    connect(&library_, &SkinLibrary::installedChanged, this, &SkinManagerDialog::refreshRows);
    connect(&library_, &SkinLibrary::catalogFinished, this, [this](const QString &error) {
        catalogLoading_ = false;
        catalogFailed_ = !error.isEmpty();
        showStatus(catalogFailed_ ? QStringLiteral("无法加载皮肤目录。请重试。") : QString(), error);
        refreshRows();
    });
    connect(&library_, &SkinLibrary::installFinished, this, [this](const QString &id, const QString &error) {
        downloadingId_.clear();
        failedDownloadId_ = error.isEmpty() ? QString() : id;
        showStatus(error.isEmpty() ? QStringLiteral("皮肤已下载，可在设置的配色中选择。")
                                   : QStringLiteral("皮肤下载失败。请重试。"), error);
        updateActions();
    });
    refreshRows();
}

void SkinManagerDialog::refreshCatalog()
{
    if (library_.isBusy()) return;
    catalogLoading_ = true;
    showStatus(list_->topLevelItemCount() ? QStringLiteral("正在加载皮肤目录…") : QString());
    updateActions();
    library_.refreshCatalog();
}

void SkinManagerDialog::refreshRows()
{
    const auto *previous = list_->currentItem();
    const QString previousId = previous ? previous->data(0, SkinIdRole).toString() : QString();
    const QSignalBlocker blocker(list_);
    list_->clear();

    const auto append = [this, &previousId](const QString &id, const QString &name, const QString &status, bool installed) {
        auto *item = new QTreeWidgetItem(list_, {name, status});
        item->setData(0, SkinIdRole, id);
        item->setData(0, InstalledRole, installed);
        item->setToolTip(0, tooltip(name));
        if (installed) item->setIcon(0, UiTheme::paletteSwatch(id));
        if (id == previousId) list_->setCurrentItem(item);
    };

    QSet<QString> installedIds;
    for (const SkinTheme &skin : UiTheme::installedThemes()) {
        installedIds.insert(skin.id);
        append(skin.id, skin.name, skin.id == UiTheme::paletteId() ? QStringLiteral("正在使用")
                                                                : QStringLiteral("已安装"), true);
    }
    for (const SkinCatalogEntry &skin : library_.catalog()) {
        if (!installedIds.contains(skin.id)) append(skin.id, skin.name, QStringLiteral("可下载"), false);
    }

    if (!list_->currentItem() && list_->topLevelItemCount()) list_->setCurrentItem(list_->topLevelItem(0));
    content_->setCurrentWidget(list_->topLevelItemCount() ? static_cast<QWidget *>(list_) : empty_);
    updateActions();
}

void SkinManagerDialog::updateActions()
{
    const auto *item = list_->currentItem();
    const bool installed = item && item->data(0, InstalledRole).toBool();
    const QString id = item ? item->data(0, SkinIdRole).toString() : QString();
    const bool retryDownload = !id.isEmpty() && id == failedDownloadId_;

    empty_->setText(catalogLoading_ ? QStringLiteral("正在加载皮肤目录…")
                    : catalogFailed_ ? QStringLiteral("暂无已安装的皮肤") : QStringLiteral("暂无可用的皮肤"));
    refresh_->setText(catalogFailed_ ? QStringLiteral("重试目录") : QStringLiteral("刷新目录"));
    refresh_->setEnabled(!catalogLoading_ && downloadingId_.isEmpty());
    action_->setText(installed ? QStringLiteral("删除")
                     : retryDownload ? QStringLiteral("重试下载") : QStringLiteral("下载"));
    action_->setEnabled(item && downloadingId_.isEmpty() && (installed || !catalogLoading_));
    UiTheme::setRole(action_, installed ? "danger" : "primary");
}

void SkinManagerDialog::actOnSelection()
{
    const auto *item = list_->currentItem();
    if (!item) return;
    const QString id = item->data(0, SkinIdRole).toString();
    if (item->data(0, InstalledRole).toBool()) {
        const bool active = id == UiTheme::paletteId();
        QString error;
        if (!removeTheme_(id, &error)) {
            showStatus(QStringLiteral("无法删除皮肤。请重试。"), error);
            return;
        }
        showStatus(active ? QStringLiteral("皮肤已删除，配色已切回云白 × 雾蓝。") : QStringLiteral("皮肤已删除。"));
    } else {
        downloadingId_ = id;
        failedDownloadId_.clear();
        showStatus(QStringLiteral("正在下载皮肤…"));
        updateActions();
        library_.installTheme(id);
    }
}

void SkinManagerDialog::showStatus(const QString &message, const QString &error)
{
    status_->setText(error.isEmpty() ? message : message + QLatin1Char('\n') + error);
    UiTheme::setRole(status_, error.isEmpty() ? "muted" : "error");
    status_->setVisible(!message.isEmpty());
}
