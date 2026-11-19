#include "skinmanagerdialog.h"

#include "skinlibrary.h"
#include "theme.h"
#include "widgets/emptyviewhint.h"

#include <QApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <utility>

namespace {

constexpr int NameColumn = 0;
constexpr int StatusColumn = 1;
constexpr int ActionColumn = 2;
constexpr int SkinIdRole = Qt::UserRole;
constexpr int InstalledRole = Qt::UserRole + 1;
constexpr int ActionEnabledRole = Qt::UserRole + 2;

QString tooltip(const QString &text)
{
    return QStringLiteral("<qt>%1</qt>").arg(text.toHtmlEscaped());
}

class SkinRowDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (index.column() == NameColumn) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem cell(option);
        initStyleOption(&cell, index);
        if (cell.icon.isNull()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        // Use the list style for selection and focus, then center the icon and its optional label.
        const auto icon = cell.icon;
        const auto text = cell.text;
        const auto enabled = index.data(ActionEnabledRole);
        cell.icon = {};
        cell.text.clear();
        cell.features &= ~(QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration);
        const auto *style = cell.widget ? cell.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &cell, painter, cell.widget);

        const QSize iconSize(16, 16);
        const int labelWidth = text.isEmpty() ? 0 : cell.fontMetrics.horizontalAdvance(text) + 6;
        const auto group = QStyle::alignedRect(cell.direction, Qt::AlignCenter,
                                               QSize(iconSize.width() + labelWidth, iconSize.height()), cell.rect);
        const auto image = QStyle::visualRect(cell.direction, group, QRect(group.topLeft(), iconSize));
        const bool selected = cell.state.testFlag(QStyle::State_Selected);
        const bool inactive = enabled.isValid() && !enabled.toBool();
        icon.paint(painter, image, Qt::AlignCenter,
                   inactive ? QIcon::Disabled : selected ? QIcon::Selected : QIcon::Normal);
        if (!text.isEmpty()) {
            const QRect label = group.adjusted(iconSize.width() + 6, 0, 0, 0);
            auto palette = cell.palette;
            if (inactive) palette.setCurrentColorGroup(QPalette::Disabled);
            painter->save();
            painter->setFont(cell.font);
            style->drawItemText(painter, QStyle::visualRect(cell.direction, group, label), Qt::AlignCenter,
                                palette, !inactive, text, selected ? QPalette::HighlightedText : QPalette::Text);
            painter->restore();
        }
    }
};

} // namespace

SkinManagerDialog::SkinManagerDialog(SkinLibrary &library, std::function<bool(const QString &, QString *)> removeTheme,
                                     QWidget *parent)
    : QDialog(parent), library_(library), removeTheme_(std::move(removeTheme)),
      list_(new QTreeWidget(this)), search_(new QLineEdit(this)), status_(new QLabel(this)),
      refresh_(new QPushButton(UiTheme::icon(QStringLiteral("refresh")), QString(), this))
{
    setObjectName(QStringLiteral("skinManagerDialog"));
    setWindowTitle(QStringLiteral("皮肤库"));
    UiTheme::setSurface(this, "content");

    auto *body = new QVBoxLayout(this);
    UiTheme::setLayoutMetrics(body, {20, 18, 20, 18}, 10);

    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);
    search_->setObjectName(QStringLiteral("skinSearch"));
    search_->setAccessibleName(QStringLiteral("搜索皮肤"));
    search_->setPlaceholderText(QStringLiteral("搜索皮肤"));
    search_->setClearButtonEnabled(true);
    search_->setMaximumWidth(260);
    status_->setObjectName(QStringLiteral("skinManagerStatus"));
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->hide();
    refresh_->setObjectName(QStringLiteral("skinCatalogRefresh"));
    refresh_->setAutoDefault(false);
    refresh_->setIconSize(QSize(16, 16));
    UiTheme::setRole(refresh_, "quiet");
    toolbar->addWidget(search_, 2, Qt::AlignTop);
    toolbar->addWidget(status_, 3, Qt::AlignVCenter);
    toolbar->addStretch(3);
    toolbar->addWidget(refresh_, 0, Qt::AlignTop);
    body->addLayout(toolbar);

    list_->setObjectName(QStringLiteral("skinManagerList"));
    list_->setAccessibleName(QStringLiteral("皮肤列表"));
    list_->setAccessibleDescription(QStringLiteral("用方向键选中皮肤，按回车或空格下载，按 Delete 删除已下载的皮肤。"));
    list_->setHeaderLabels({QStringLiteral("皮肤"), QStringLiteral("状态"), QStringLiteral("操作")});
    list_->headerItem()->setTextAlignment(StatusColumn, Qt::AlignCenter);
    list_->headerItem()->setTextAlignment(ActionColumn, Qt::AlignCenter);
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionsMovable(false);
    list_->header()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(StatusColumn, QHeaderView::Fixed);
    list_->header()->setSectionResizeMode(ActionColumn, QHeaderView::Fixed);
    list_->setColumnWidth(StatusColumn, 108);
    list_->setColumnWidth(ActionColumn, 68);
    list_->setRootIsDecorated(false);
    list_->setAllColumnsShowFocus(true);
    list_->setUniformRowHeights(true);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setIconSize(QSize(22, 16));
    list_->setItemDelegate(new SkinRowDelegate(list_));
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->installEventFilter(this);
    list_->viewport()->setMouseTracking(true);
    list_->viewport()->installEventFilter(this);
    body->addWidget(list_, 1);
    setFocusProxy(list_);

    connect(refresh_, &QPushButton::clicked, this, &SkinManagerDialog::refreshCatalog);
    connect(search_, &QLineEdit::textChanged, this, &SkinManagerDialog::filterRows);
    connect(list_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int column) {
        if (column == StatusColumn) download(item);
        else if (column == ActionColumn) remove(item);
    });
    connect(list_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        showItemMenu(list_->itemAt(point), list_->viewport()->mapToGlobal(point));
    });
    connect(this, &QDialog::finished, &library_, &SkinLibrary::cancel);
    connect(&library_, &SkinLibrary::installedChanged, this, &SkinManagerDialog::refreshRows);
    connect(&library_, &SkinLibrary::catalogFinished, this, [this](const QString &error) {
        catalogLoading_ = false;
        catalogFailed_ = !error.isEmpty();
        showStatus(catalogFailed_ ? QStringLiteral("无法加载皮肤目录。点击刷新图标重试。") : QString(), error);
        refreshRows();
    });
    connect(&library_, &SkinLibrary::installFinished, this, [this](const QString &id, const QString &error) {
        downloadingId_.clear();
        if (error.isEmpty()) {
            downloadErrors_.remove(id);
            showStatus(QStringLiteral("皮肤已下载，可返回设置，在「风格」中选择。"), {}, true);
        } else {
            downloadErrors_.insert(id, error);
        }
        updateActions();
    });
    refreshRows();
}

void SkinManagerDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    updateActiveSkin();
}

bool SkinManagerDialog::eventFilter(QObject *object, QEvent *event)
{
    if (object == list_->viewport() && event->type() == QEvent::MouseMove) {
        const auto position = static_cast<QMouseEvent *>(event)->position().toPoint();
        const bool actionable = list_->indexAt(position).data(ActionEnabledRole).toBool();
        list_->viewport()->setCursor(actionable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    if (object == list_ && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        auto *item = list_->currentItem();
        switch (key->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Space:
            if (!key->isAutoRepeat()) download(item);
            return true;
        case Qt::Key_Delete:
            if (!key->isAutoRepeat()) remove(item);
            return true;
        case Qt::Key_Menu:
            if (item) {
                const auto position = list_->visualItemRect(item).center();
                showItemMenu(item, list_->viewport()->mapToGlobal(position));
            }
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(object, event);
}

void SkinManagerDialog::refreshCatalog()
{
    if (library_.isBusy()) return;
    catalogLoading_ = true;
    showStatus(list_->topLevelItemCount() ? QStringLiteral("正在刷新皮肤目录…") : QString());
    updateActions();
    library_.refreshCatalog();
}

void SkinManagerDialog::refreshRows()
{
    const int scrollPosition = list_->verticalScrollBar()->value();
    QHash<QString, QTreeWidgetItem *> previous;
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        auto *item = list_->topLevelItem(i);
        previous.insert(item->data(NameColumn, SkinIdRole).toString(), item);
    }

    const auto updateOrAdd = [this, &previous](const QString &id, const QString &name, bool installed) {
        auto *item = previous.take(id);
        if (!item) {
            item = new QTreeWidgetItem(list_);
            item->setData(NameColumn, SkinIdRole, id);
            item->setTextAlignment(StatusColumn, Qt::AlignCenter);
            item->setTextAlignment(ActionColumn, Qt::AlignCenter);
        }
        item->setText(NameColumn, name);
        item->setToolTip(NameColumn, tooltip(name));
        item->setData(NameColumn, InstalledRole, installed);
        item->setIcon(NameColumn, installed ? UiTheme::paletteSwatch(id) : QIcon());
    };

    QSet<QString> installedIds;
    for (const SkinTheme &skin : UiTheme::installedThemes()) {
        installedIds.insert(skin.id);
        updateOrAdd(skin.id, skin.name, true);
    }
    for (const SkinCatalogEntry &skin : library_.catalog()) {
        if (!installedIds.contains(skin.id)) updateOrAdd(skin.id, skin.name, false);
    }
    for (auto old = previous.cbegin(); old != previous.cend(); ++old) {
        downloadErrors_.remove(old.key());
        delete old.value();
    }

    filterRows();
    list_->verticalScrollBar()->setValue(scrollPosition);
}

void SkinManagerDialog::filterRows()
{
    const auto filter = search_->text().trimmed();
    QTreeWidgetItem *firstMatch = nullptr;
    bool hasInstalledMatch = false;
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        auto *item = list_->topLevelItem(i);
        const bool matches = item->text(NameColumn).contains(filter, Qt::CaseInsensitive);
        item->setHidden(!matches);
        if (matches && !firstMatch) firstMatch = item;
        if (matches && item->data(NameColumn, InstalledRole).toBool()) hasInstalledMatch = true;
    }
    list_->setColumnHidden(ActionColumn, !hasInstalledMatch);
    const auto *current = list_->currentItem();
    if (!current || current->isHidden()) list_->setCurrentItem(firstMatch);
    updateActions();
}

void SkinManagerDialog::updateActions()
{
    const bool busy = catalogLoading_ || !downloadingId_.isEmpty();
    refresh_->setToolTip(catalogFailed_ ? QStringLiteral("重试目录") : QStringLiteral("刷新目录"));
    refresh_->setAccessibleName(catalogFailed_ ? QStringLiteral("重试皮肤目录") : QStringLiteral("刷新皮肤目录"));
    refresh_->setEnabled(!busy);

    QString hint;
    if (!list_->topLevelItemCount()) {
        if (catalogLoading_) hint = QStringLiteral("正在加载皮肤目录…");
        else if (catalogFailed_) hint = QStringLiteral("暂无已下载的皮肤");
        else hint = QStringLiteral("暂无可用的皮肤");
    } else if (!list_->currentItem() || list_->currentItem()->isHidden()) {
        hint = QStringLiteral("没有匹配的皮肤");
    }
    EmptyViewHint::update(list_, hint);

    for (int i = 0; i < list_->topLevelItemCount(); ++i) updateRowActions(list_->topLevelItem(i));
}

void SkinManagerDialog::updateActiveSkin()
{
    if (clearStatusOnReturn_) showStatus({});
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        auto *item = list_->topLevelItem(i);
        if (item->data(NameColumn, InstalledRole).toBool()) updateRowActions(item);
    }
}

void SkinManagerDialog::updateRowActions(QTreeWidgetItem *item)
{
    static const auto downloadIcon = UiTheme::icon(QStringLiteral("download"));
    static const auto retryIcon = UiTheme::icon(QStringLiteral("refresh"));
    static const auto installedIcon = UiTheme::icon(QStringLiteral("check"));
    static const auto removeIcon = UiTheme::icon(QStringLiteral("trash"));
    const auto id = item->data(NameColumn, SkinIdRole).toString();
    const bool installed = item->data(NameColumn, InstalledRole).toBool();
    const bool canRemove = installed && downloadingId_.isEmpty();

    QIcon icon;
    QString text;
    QString label;
    QString detail;
    QVariant downloadEnabled;
    if (installed) {
        icon = installedIcon;
        const bool active = id == UiTheme::paletteId();
        if (active) text = QStringLiteral("使用中");
        label = active ? QStringLiteral("已下载，正在使用") : QStringLiteral("已下载");
        detail = label;
    } else if (id == downloadingId_) {
        text = QStringLiteral("下载中…");
        label = QStringLiteral("正在下载");
        detail = label;
    } else {
        downloadEnabled = !catalogLoading_ && downloadingId_.isEmpty();
        const auto error = downloadErrors_.value(id);
        if (error.isEmpty()) {
            icon = downloadIcon;
            label = QStringLiteral("可下载");
            detail = QStringLiteral("下载此皮肤（回车或空格）");
        } else {
            icon = retryIcon;
            text = QStringLiteral("重试");
            label = QStringLiteral("下载失败，重试");
            detail = QStringLiteral("下载失败。点击重试（回车或空格）。\n%1").arg(error);
        }
    }
    item->setIcon(StatusColumn, icon);
    item->setText(StatusColumn, text);
    item->setData(StatusColumn, ActionEnabledRole, downloadEnabled);
    item->setData(StatusColumn, Qt::AccessibleTextRole, label);
    item->setData(StatusColumn, Qt::AccessibleDescriptionRole, detail);
    item->setToolTip(StatusColumn, tooltip(detail));

    const QString removeLabel = installed ? QStringLiteral("删除皮肤") : QString();
    const QString removeHint = installed ? QStringLiteral("删除此皮肤（Delete）") : QString();
    item->setIcon(ActionColumn, installed ? removeIcon : QIcon());
    item->setData(ActionColumn, ActionEnabledRole, canRemove);
    item->setData(ActionColumn, Qt::AccessibleTextRole, removeLabel);
    item->setData(ActionColumn, Qt::AccessibleDescriptionRole, removeHint);
    item->setToolTip(ActionColumn, installed ? tooltip(removeHint) : QString());
}

void SkinManagerDialog::download(QTreeWidgetItem *item)
{
    if (!item || item->isHidden() || !item->data(StatusColumn, ActionEnabledRole).toBool() || library_.isBusy()) return;
    const auto id = item->data(NameColumn, SkinIdRole).toString();
    downloadingId_ = id;
    showStatus({});
    updateActions();
    library_.installTheme(id);
}

void SkinManagerDialog::remove(QTreeWidgetItem *item)
{
    if (!item || item->isHidden() || !item->data(ActionColumn, ActionEnabledRole).toBool()) return;
    const auto id = item->data(NameColumn, SkinIdRole).toString();
    const bool active = id == UiTheme::paletteId();
    QString message = QStringLiteral("删除「%1」？").arg(item->text(NameColumn));
    if (active) message += QStringLiteral("\n当前风格将切回云白 × 雾蓝。");
    QMessageBox prompt(QMessageBox::Question, QStringLiteral("删除皮肤"), message,
                       QMessageBox::Yes | QMessageBox::Cancel, this);
    prompt.setObjectName(QStringLiteral("skinRemoveConfirmation"));
    prompt.setTextFormat(Qt::PlainText);
    auto *confirm = prompt.button(QMessageBox::Yes);
    confirm->setText(QStringLiteral("删除"));
    confirm->setObjectName(QStringLiteral("skinRemoveConfirm"));
    UiTheme::setRole(confirm, "danger");
    auto *cancel = prompt.button(QMessageBox::Cancel);
    cancel->setText(QStringLiteral("取消"));
    cancel->setObjectName(QStringLiteral("skinRemoveCancel"));
    prompt.setDefaultButton(QMessageBox::Cancel);
    prompt.setEscapeButton(QMessageBox::Cancel);
    if (prompt.exec() != QMessageBox::Yes) return;

    QString error;
    if (!removeTheme_(id, &error)) {
        showStatus(QStringLiteral("无法删除皮肤。请重试。"), error);
        return;
    }
    showStatus(active ? QStringLiteral("皮肤已删除，风格已切回云白 × 雾蓝。") : QStringLiteral("皮肤已删除。"), {}, true);
}

void SkinManagerDialog::showItemMenu(QTreeWidgetItem *item, const QPoint &globalPosition)
{
    if (!item || !item->data(ActionColumn, ActionEnabledRole).toBool()) return;
    QMenu menu(this);
    auto *removeAction = menu.addAction(UiTheme::icon(QStringLiteral("trash")), QStringLiteral("删除皮肤"));
    if (menu.exec(globalPosition) == removeAction) remove(item);
}

void SkinManagerDialog::showStatus(const QString &message, const QString &error, bool clearOnReturn)
{
    clearStatusOnReturn_ = clearOnReturn;
    status_->setText(message);
    status_->setToolTip(error.isEmpty() ? QString() : tooltip(error));
    status_->setAccessibleDescription(error);
    UiTheme::setRole(status_, error.isEmpty() ? "muted" : "error");
    status_->setVisible(!message.isEmpty());
}
