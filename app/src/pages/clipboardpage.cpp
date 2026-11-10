#include "clipboardpage.h"
#include "clipboard/clipboardcontent.h"
#include "clipboard/clipboardmonitor.h"
#include "widgets/textlayoutguard.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QHash>
#include <QSet>
#include <algorithm>

namespace {
constexpr int FolderRole=Qt::UserRole+1;
constexpr int TabRole=Qt::UserRole+2;
constexpr int FolderIdRole=Qt::UserRole+3;
constexpr int TabNodeRole=Qt::UserRole+4;
QString expandedKey(int tabId) { return QStringLiteral("clipboard.%1.expanded_folders").arg(tabId); }
bool isEntry(const QTreeWidgetItem *item)
{
    if (!item || item->data(0,FolderRole).toBool() || item->data(0,TabNodeRole).toBool()) return false;
    for (auto *node=item; node; node=node->parent()) {
        if (node->isHidden() || (node->parent() && !node->parent()->isExpanded())) return false;
    }
    return true;
}
class ClipboardEditorDialog final : public QDialog {
public:
    using QDialog::QDialog;
    std::function<bool()> dirty;
    std::function<void()> save;
    void reject() override {
        if (dirty && dirty()) {
            const auto choice=QMessageBox::warning(this,QStringLiteral("保存修改"),QStringLiteral("条目有未保存的修改。"),
                QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
            if (choice==QMessageBox::Cancel) return;
            if (choice==QMessageBox::Save) { if (save) save(); return; }
        }
        QDialog::reject();
    }
};
class ClipboardTree final : public SiblingOrderTree {
public:
    using SiblingOrderTree::SiblingOrderTree;
protected:
    void startDrag(Qt::DropActions actions) override {
        if (selectedItems().size()==1) SiblingOrderTree::startDrag(actions);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (selectedItems().size()>1 && event->modifiers()==Qt::ControlModifier
            && (event->key()==Qt::Key_Up || event->key()==Qt::Key_Down)) { event->accept(); return; }
        SiblingOrderTree::keyPressEvent(event);
    }
};
QLabel *label(const QString &text, const QString &name, QWidget *parent)
{
    auto *result=new QLabel(text,parent); result->setTextFormat(Qt::PlainText); result->setObjectName(name); return result;
}
QString itemTitle(const AppDatabase::ClipboardItem &item)
{
    if (!item.title.isEmpty()) return item.title;
    const auto text=item.text.simplified();
    return text.isEmpty() ? ClipboardContent::kindName(item.kind) : text.left(100);
}
void readablePlaceholder(QWidget *widget)
{
    // The parent's stylesheet supplies the input palette, so set this in the
    // stylesheet too; a widget palette alone is overwritten during polishing.
    widget->setStyleSheet(QStringLiteral("QLineEdit, QPlainTextEdit { placeholder-text-color: #52627a; }"));
}
}

ClipboardPage::ClipboardPage(AppDatabase *database, QWidget *parent) : QWidget(parent),database_(database)
{
    setObjectName("clipboardPage");
    monitor_=new ClipboardMonitor(database_,this);
    auto *layout=new QVBoxLayout(this); layout->setContentsMargins(34,30,34,24); layout->setSpacing(14);
    auto *header=new QHBoxLayout;
    header->addWidget(label(QStringLiteral("剪贴板"),"pageTitle",this)); header->addStretch();
    listening_=new QCheckBox(QStringLiteral("监听剪贴板"),this); listening_->setObjectName("clipboardListening");
    listening_->setToolTip(QStringLiteral("开启后自动记录新复制的内容；关闭后仍可使用已保存的条目。"));
    listening_->setChecked(monitor_->enabled()); header->addWidget(listening_);
    auto *settings=new QPushButton(QStringLiteral("历史设置"),this); header->addWidget(settings);
    layout->addLayout(header);

    auto *tabRow=new QHBoxLayout;
    tabs_=new QTabBar(this); tabs_->setObjectName("clipboardTabs"); tabs_->setMovable(true); tabs_->setExpanding(false);
    tabs_->setUsesScrollButtons(true); tabs_->setElideMode(Qt::ElideRight); tabs_->setContextMenuPolicy(Qt::CustomContextMenu);
    tabs_->setAccessibleName(QStringLiteral("剪贴板标签页"));
    tabs_->setStyleSheet(QStringLiteral("QTabBar::tab { padding: 9px 16px; margin-right: 3px; border-bottom: 2px solid transparent; color: #52627a; } QTabBar::tab:selected { color: #1d4ed8; border-bottom: 2px solid #2563eb; background: #eaf0ff; } QTabBar::tab:hover { background: #edf2fa; }"));
    tabRow->addWidget(tabs_,1);
    auto *newTab=new QPushButton(QStringLiteral("新建标签页"),this); tabRow->addWidget(newTab); layout->addLayout(tabRow);
    auto *searchRow=new QHBoxLayout;
    search_=new QLineEdit(this); search_->setObjectName("clipboardSearch"); search_->setPlaceholderText(QStringLiteral("搜索内容、标题或文件夹"));
    readablePlaceholder(search_);
    search_->setClearButtonEnabled(true); search_->setAccessibleName(QStringLiteral("搜索剪贴板内容")); searchRow->addWidget(search_,1);
    searchScope_=new QComboBox(this); searchScope_->setObjectName("clipboardSearchScope");
    searchScope_->addItems({QStringLiteral("当前标签页"),QStringLiteral("全部标签页")}); searchScope_->setAccessibleName(QStringLiteral("搜索范围"));
    searchRow->addWidget(searchScope_);
    auto *newFolder=new QPushButton(QStringLiteral("新建文件夹"),this); newFolder->setObjectName("clipboardNewFolder");
    searchRow->addWidget(newFolder); layout->addLayout(searchRow);

    auto *contentSplit=new QSplitter(Qt::Vertical,this); contentSplit->setChildrenCollapsible(false);
    items_=new ClipboardTree(contentSplit); items_->setObjectName("clipboardItems");
    synchronizeTreeAccessibility(items_);
    items_->setHeaderHidden(true); items_->header()->setStretchLastSection(true);
    items_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    items_->setStyleSheet(QStringLiteral("QTreeWidget::item { padding: 8px 10px; color: #334155; } QTreeWidget::item:hover { background: #f1f5fb; } QTreeWidget::item:selected { color: #1d4ed8; background: #e0ebff; }"));
    items_->setContextMenuPolicy(Qt::CustomContextMenu); items_->setMinimumHeight(120); items_->setWordWrap(false);
    items_->setTextElideMode(Qt::ElideRight); items_->setUniformRowHeights(true);
    items_->setIndentation(20); items_->setIconSize(QSize(16,16));
    items_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); items_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    items_->setAccessibleName(QStringLiteral("剪贴板内容与文件夹"));

    previewPane_=new QWidget(contentSplit); auto *previewLayout=new QVBoxLayout(previewPane_); previewLayout->setContentsMargins(0,4,0,0);
    previewImage_=label(QString(),"clipboardImage",previewPane_); previewImage_->setAlignment(Qt::AlignCenter); previewImage_->setMinimumSize(1,1);
    previewImage_->setMaximumHeight(220); previewImage_->hide(); previewLayout->addWidget(previewImage_);
    previewText_=new TextLayoutGuard::Edit(previewPane_); previewText_->setObjectName("clipboardPreview"); previewText_->setReadOnly(true);
    previewText_->setMinimumHeight(65); previewText_->setAccessibleName(QStringLiteral("条目内容预览")); previewLayout->addWidget(previewText_,1);
    previewPane_->hide();
    contentSplit->setSizes({330,170}); contentSplit->setStretchFactor(0,3); contentSplit->setStretchFactor(1,2); layout->addWidget(contentSplit,1);

    auto *actions=new QHBoxLayout; actions->setSpacing(6);
    auto *create=new QPushButton(QStringLiteral("新建"),this); create->setObjectName("clipboardNewItem");
    edit_=new QPushButton(QStringLiteral("编辑"),this); edit_->setObjectName("clipboardEditItem");
    move_=new QPushButton(QStringLiteral("移动到…"),this); move_->setObjectName("clipboardMoveItems");
    remove_=new QPushButton(QStringLiteral("删除"),this); remove_->setObjectName("clipboardDeleteItems");
    copy_=new QPushButton(QStringLiteral("复制并收起"),this); copy_->setObjectName("primaryButton");
    copy_->setToolTip(QStringLiteral("复制选中条目并隐藏 Orchestrate，随后可手动 Ctrl+V。"));
    actions->addWidget(create); actions->addWidget(edit_); actions->addWidget(move_); actions->addWidget(remove_); actions->addStretch(); actions->addWidget(copy_);
    layout->addLayout(actions);
    status_=label(QString(),"clipboardStatus",this); status_->setWordWrap(true); status_->setStyleSheet(QStringLiteral("color: #b42318;")); status_->hide(); layout->addWidget(status_);

    refreshTimer_=new QTimer(this); refreshTimer_->setSingleShot(true); refreshTimer_->setInterval(100);
    connect(refreshTimer_,&QTimer::timeout,this,&ClipboardPage::refreshItems);
    connect(monitor_,&ClipboardMonitor::recorded,this,[this] {
        dirty_=true;
        status_->hide();
        if (isVisible()) refreshTimer_->start();
    });
    connect(monitor_,&ClipboardMonitor::captureError,this,&ClipboardPage::report);
    connect(listening_,&QCheckBox::toggled,this,[this](bool enabled) {
        QString error;
        if (!monitor_->setEnabled(enabled,&error)) { QSignalBlocker blocker(listening_); listening_->setChecked(monitor_->enabled()); report(error); return; }
        status_->hide();
    });
    connect(settings,&QPushButton::clicked,this,&ClipboardPage::historySettings);
    connect(newTab,&QPushButton::clicked,this,&ClipboardPage::addTab);
    connect(newFolder,&QPushButton::clicked,this,&ClipboardPage::addFolder);
    connect(tabs_,&QTabBar::currentChanged,this,[this] {
        database_->setSetting("clipboard.tab",QString::number(tabId()));
        if (tabId()!=displayedTabId_) refreshItems();
    });
    connect(tabs_,&QTabBar::tabMoved,this,[this] {
        QList<int> ids; for (int i=0; i<tabs_->count(); ++i) ids.append(tabs_->tabData(i).toInt());
        QString error;
        if (!database_->reorderClipboardTabs(ids,&error)) { const int selected=tabId(); refreshTabs(selected); report(error); }
    });
    connect(tabs_,&QTabBar::customContextMenuRequested,this,&ClipboardPage::tabMenu);
    items_->saveOrder=[this](QTreeWidgetItem *item,const QList<int> &ids) {
        QString error;
        const bool ok=item->data(0,FolderRole).toBool()
            ? database_->reorderClipboardFolders(item->data(0,TabRole).toInt(),ids,&error)
            : database_->reorderClipboardItems(item->data(0,TabRole).toInt(),item->data(0,FolderIdRole).toInt(),ids,&error);
        if (!ok) report(error); return ok;
    };
    connect(items_,&QTreeWidget::itemExpanded,this,[this] { saveExpandedFolders(); updatePreview(); });
    connect(items_,&QTreeWidget::itemCollapsed,this,[this] { saveExpandedFolders(); updatePreview(); });
    connect(search_,&QLineEdit::textChanged,this,[this] { refreshTimer_->start(); });
    connect(searchScope_,&QComboBox::currentIndexChanged,this,[this] { refreshItems(); });
    connect(items_,&QTreeWidget::itemSelectionChanged,this,&ClipboardPage::updatePreview);
    connect(items_,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem *item) {
        if (!isEntry(item)) return;
        items_->clearSelection(); items_->setCurrentItem(item); item->setSelected(true); copySelected();
    });
    connect(items_,&QTreeWidget::customContextMenuRequested,this,&ClipboardPage::itemMenu);
    connect(create,&QPushButton::clicked,this,[this] { editItem(true); });
    connect(edit_,&QPushButton::clicked,this,[this] { editItem(false); });
    connect(copy_,&QPushButton::clicked,this,&ClipboardPage::copySelected);
    connect(move_,&QPushButton::clicked,this,&ClipboardPage::moveSelected);
    connect(remove_,&QPushButton::clicked,this,&ClipboardPage::deleteSelected);
    auto *enter=new QShortcut(QKeySequence(Qt::Key_Return),items_); enter->setContext(Qt::WidgetShortcut);
    connect(enter,&QShortcut::activated,this,[this] {
        auto *item=items_->currentItem();
        if (item && !isEntry(item)) item->setExpanded(!item->isExpanded());
        else copySelected();
    });
    auto *find=new QShortcut(QKeySequence::Find,this); find->setContext(Qt::WidgetWithChildrenShortcut); connect(find,&QShortcut::activated,search_,qOverload<>(&QWidget::setFocus));
    refreshTabs(database_->setting("clipboard.tab","1").toInt());
}

int ClipboardPage::tabId() const { return tabs_->tabData(tabs_->currentIndex()).toInt(); }
QList<int> ClipboardPage::selectedIds() const
{
    QList<int> ids;
    // Preserve visual order for bulk moves.
    for (QTreeWidgetItemIterator it(items_); *it; ++it)
        if ((*it)->isSelected() && isEntry(*it)) ids.append((*it)->data(0,Qt::UserRole).toInt());
    return ids;
}
void ClipboardPage::showEvent(QShowEvent *event) { QWidget::showEvent(event); if (dirty_) refreshItems(); }
void ClipboardPage::report(const QString &message) { status_->setText(message); status_->setVisible(!message.isEmpty()); }

void ClipboardPage::refreshTabs(int selected)
{
    if (!selected) selected=tabId();
    QString error; const auto tabs=database_->clipboardTabs(&error);
    if (!error.isEmpty()) { report(error); return; }
    {
        const QSignalBlocker blocker(tabs_);
        while (tabs_->count()) tabs_->removeTab(0);
        int current=0;
        for (const auto &tab : tabs) {
            const int index=tabs_->addTab(tab.name); tabs_->setTabData(index,tab.id);
            tabs_->setTabToolTip(index,tab.history ? QStringLiteral("自动收集新复制的内容；拖动可调位。") : tab.name+QStringLiteral(" · 拖动调位，右键管理"));
            if (tab.id==selected) current=index;
        }
        tabs_->setCurrentIndex(current);
    }
    refreshItems();
}

void ClipboardPage::saveExpandedFolders()
{
    if (searchScope_->currentIndex()!=0 || !search_->text().trimmed().isEmpty()) return;
    QStringList expanded;
    for (int i=0; i<items_->topLevelItemCount(); ++i) {
        const auto *item=items_->topLevelItem(i);
        if (item->data(0,FolderRole).toBool() && item->isExpanded()) expanded.append(QString::number(item->data(0,Qt::UserRole).toInt()));
    }
    QString error;
    if (!database_->setSetting(expandedKey(tabId()),expanded.join(','),&error)) report(error);
}

void ClipboardPage::selectFolder(int id)
{
    for (QTreeWidgetItemIterator it(items_); *it; ++it) {
        if ((*it)->data(0,FolderRole).toBool() && (*it)->data(0,Qt::UserRole).toInt()==id) {
            items_->clearSelection(); items_->setCurrentItem(*it); (*it)->setExpanded(true); items_->scrollToItem(*it); return;
        }
    }
}

void ClipboardPage::refreshItems()
{
    refreshTimer_->stop(); dirty_=false;
    const bool sameTab=displayedTabId_==tabId();
    const auto selected=sameTab ? selectedIds() : QList<int>();
    const auto *previous=items_->currentItem();
    const int previousFolder=sameTab && previous && previous->data(0,FolderRole).toBool() ? previous->data(0,Qt::UserRole).toInt() : 0;
    const int scroll=sameTab ? items_->verticalScrollBar()->value() : 0;
    const auto search=search_->text().trimmed(); const bool global=searchScope_->currentIndex()==1;
    QString error; auto entries=database_->clipboardItems(global ? 0 : tabId(),-1,search,&error);
    if (!error.isEmpty()) { report(error); return; }
    auto tabs=database_->clipboardTabs(&error);
    if (!global) tabs.erase(std::remove_if(tabs.begin(),tabs.end(),[this](const auto &tab) { return tab.id!=tabId(); }),tabs.end());
    QHash<int,QList<AppDatabase::ClipboardFolder>> folders;
    QSet<int> entryIds;
    for (const auto &entry : entries) entryIds.insert(entry.id);
    for (const auto &tab : tabs) {
        folders.insert(tab.id,database_->clipboardFolders(tab.id,&error));
        if (!error.isEmpty()) { report(error); return; }
        if (!search.isEmpty()) for (const auto &folder : folders.value(tab.id)) {
            if (!folder.name.contains(search,Qt::CaseInsensitive)) continue;
            for (const auto &entry : database_->clipboardItems(tab.id,folder.id,{},&error)) {
                if (!entryIds.contains(entry.id)) { entries.append(entry); entryIds.insert(entry.id); }
            }
        }
    }
    if (!error.isEmpty()) { report(error); return; }
    displayedTabId_=tabId();
    {
        const QSignalBlocker blocker(items_); items_->clear();
        items_->orderingEnabled=!global && search.isEmpty();
        items_->setDragEnabled(items_->orderingEnabled);
        QHash<int,QTreeWidgetItem *> tabNodes,folderNodes;
        QSet<int> matchedFolders,matchedTabs;
        for (const auto &entry : entries) { matchedFolders.insert(entry.folderId); matchedTabs.insert(entry.tabId); }
        QIcon folderIcon=style()->standardIcon(QStyle::SP_DirClosedIcon);
        folderIcon.addPixmap(style()->standardPixmap(QStyle::SP_DirOpenIcon),QIcon::Normal,QIcon::On);
        for (const auto &tab : tabs) {
            bool matchingFolder=false;
            for (const auto &folder : folders.value(tab.id)) matchingFolder|=folder.name.contains(search,Qt::CaseInsensitive);
            if (!search.isEmpty() && !matchedTabs.contains(tab.id) && !matchingFolder) continue;
            QTreeWidgetItem *tabNode=nullptr;
            if (global) {
                tabNode=new QTreeWidgetItem(items_,{tab.name}); tabNode->setData(0,TabNodeRole,true); tabNode->setData(0,TabRole,tab.id);
                tabNode->setExpanded(true); tabNodes.insert(tab.id,tabNode);
            }
            const auto savedExpanded=database_->setting(expandedKey(tab.id),QStringLiteral("*")).split(',');
            for (const auto &folder : folders.value(tab.id)) {
                if (!search.isEmpty() && !matchedFolders.contains(folder.id) && !folder.name.contains(search,Qt::CaseInsensitive)) continue;
                auto *item=tabNode ? new QTreeWidgetItem(tabNode) : new QTreeWidgetItem(items_);
                item->setText(0,folder.name); item->setIcon(0,folderIcon);
                item->setData(0,Qt::UserRole,folder.id); item->setData(0,FolderRole,true);
                item->setData(0,TabRole,tab.id); item->setData(0,FolderIdRole,folder.id);
                item->setData(0,SiblingOrderScopeRole,QStringLiteral("folders.%1").arg(tab.id));
                item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
                item->setSizeHint(0,QSize(0,40)); item->setToolTip(0,folder.name);
                item->setExpanded(global || !search.isEmpty() || savedExpanded.contains("*") || savedExpanded.contains(QString::number(folder.id)));
                folderNodes.insert(folder.id,item);
                if (previousFolder==folder.id) items_->setCurrentItem(item);
            }
        }
        for (const auto &entry : entries) {
            const auto caption=itemTitle(entry);
            auto *parent=folderNodes.value(entry.folderId,tabNodes.value(entry.tabId));
            auto *item=parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(items_);
            item->setText(0,caption); item->setData(0,Qt::UserRole,entry.id);
            item->setData(0,TabRole,entry.tabId); item->setData(0,FolderIdRole,entry.folderId);
            item->setData(0,SiblingOrderScopeRole,QStringLiteral("items.%1.%2").arg(entry.tabId).arg(entry.folderId));
            item->setSizeHint(0,QSize(0,40)); item->setToolTip(0,caption);
            if (selected.contains(entry.id)) { items_->setCurrentItem(item,QItemSelectionModel::NoUpdate); item->setSelected(true); }
        }
        items_->verticalScrollBar()->setValue(scroll);
    }
    items_->doItemsLayout(); resetTreeAccessibility(items_); updatePreview();
}

void ClipboardPage::updateActions()
{
    const auto ids=selectedIds(); const bool single=ids.size()==1;
    copy_->setEnabled(single); edit_->setEnabled(single); move_->setEnabled(!ids.isEmpty()); remove_->setEnabled(!ids.isEmpty());
}

void ClipboardPage::updatePreview()
{
    updateActions(); previewImage_->hide(); previewText_->show();
    const auto ids=selectedIds();
    previewPane_->setVisible(ids.size()==1);
    if (ids.size()!=1) {
        previewText_->clear(); return;
    }
    QString error; const auto item=database_->clipboardItem(ids.first(),&error);
    if (!item.id) { report(error); previewText_->clear(); previewPane_->hide(); return; }
    previewText_->setToolTip(item.text.size()>200000 ? QStringLiteral("预览前 20 万字，复制保留完整内容。") : QString());
    TextLayoutGuard::setPlainText(previewText_,item.text.left(200000));
    if (item.kind=="image") {
        const auto image=ClipboardContent::previewImage(item.payload,QSize(qMax(100,previewText_->width()-20),210));
        previewImage_->setPixmap(QPixmap::fromImage(image)); previewImage_->setVisible(!image.isNull());
        previewText_->setVisible(!item.text.isEmpty());
    }
}

void ClipboardPage::copySelected()
{
    const auto ids=selectedIds(); if (ids.size()!=1) return;
    QString error;
    if (!monitor_->copyItem(ids.first(),&error)) { report(error); return; }
    status_->hide();
    emit hideRequested();
}

void ClipboardPage::addTab()
{
    bool ok=false; const auto name=QInputDialog::getText(this,QStringLiteral("新建标签页"),QStringLiteral("标签页名称"),QLineEdit::Normal,{},&ok);
    if (!ok) return;
    int id=0; QString error;
    if (!database_->addClipboardTab(name,&id,&error)) { report(error); return; }
    refreshTabs(id);
}

void ClipboardPage::addFolder()
{
    bool ok=false; const auto name=QInputDialog::getText(this,QStringLiteral("新建文件夹"),QStringLiteral("文件夹名称"),QLineEdit::Normal,{},&ok);
    if (!ok) return;
    int id=0; QString error;
    if (!database_->addClipboardFolder(tabId(),name,&id,&error)) { report(error); return; }
    search_->clear(); searchScope_->setCurrentIndex(0); refreshItems(); selectFolder(id);
}

void ClipboardPage::tabMenu(const QPoint &position)
{
    const int index=tabs_->tabAt(position); if (index<0) return;
    const int id=tabs_->tabData(index).toInt();
    QMenu menu(this); auto *create=menu.addAction(QStringLiteral("新建标签页"));
    auto *rename=menu.addAction(QStringLiteral("重命名")); auto *remove=menu.addAction(QStringLiteral("删除标签页…"));
    rename->setEnabled(id!=1); remove->setEnabled(id!=1);
    menu.addSeparator(); auto *left=menu.addAction(QStringLiteral("向左移动")); auto *right=menu.addAction(QStringLiteral("向右移动"));
    left->setEnabled(index>0); right->setEnabled(index<tabs_->count()-1);
    const auto *action=menu.exec(tabs_->mapToGlobal(position)); QString error;
    if (action==create) addTab();
    else if (action==left) tabs_->moveTab(index,index-1);
    else if (action==right) tabs_->moveTab(index,index+1);
    else if (action==rename) {
        bool ok=false; const auto name=QInputDialog::getText(this,QStringLiteral("重命名标签页"),QStringLiteral("标签页名称"),QLineEdit::Normal,tabs_->tabText(index),&ok);
        if (ok) { if (!database_->renameClipboardTab(id,name,&error)) report(error); else refreshTabs(tabId()); }
    } else if (action==remove) {
        if (QMessageBox::question(this,QStringLiteral("删除标签页"),QStringLiteral("删除“%1”？其中的内容会保留并移回“历史”，文件夹分类会移除。").arg(tabs_->tabText(index)),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return;
        if (!database_->deleteClipboardTab(id,&error)) report(error); else refreshTabs(1);
    }
}

void ClipboardPage::folderMenu(const QPoint &position)
{
    const auto *item=items_->itemAt(position); if (!item || !item->data(0,FolderRole).toBool()) return;
    const int id=item->data(0,Qt::UserRole).toInt();
    const QString folderName=item->text(0);
    QMenu menu(this); auto *create=menu.addAction(QStringLiteral("新建文件夹"));
    auto *rename=menu.addAction(QStringLiteral("重命名")); auto *remove=menu.addAction(QStringLiteral("删除文件夹…"));
    rename->setEnabled(id>0); remove->setEnabled(id>0);
    const auto *action=menu.exec(items_->viewport()->mapToGlobal(position)); QString error;
    if (action==create) addFolder();
    else if (action==rename) {
        bool ok=false; const auto name=QInputDialog::getText(this,QStringLiteral("重命名文件夹"),QStringLiteral("文件夹名称"),QLineEdit::Normal,folderName,&ok);
        if (ok) { if (!database_->renameClipboardFolder(id,name,&error)) report(error); else { refreshItems(); selectFolder(id); } }
    } else if (action==remove) {
        if (QMessageBox::question(this,QStringLiteral("删除文件夹"),QStringLiteral("删除“%1”？内容会保留并移到所在标签页顶层。").arg(folderName),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return;
        if (!database_->deleteClipboardFolder(id,&error)) report(error); else refreshItems();
    }
}

void ClipboardPage::itemMenu(const QPoint &position)
{
    if (const auto *item=items_->itemAt(position); item && item->data(0,FolderRole).toBool()) { folderMenu(position); return; }
    QMenu menu(this); auto *copy=menu.addAction(QStringLiteral("复制并收起")); auto *edit=menu.addAction(QStringLiteral("编辑"));
    auto *move=menu.addAction(QStringLiteral("移动到…")); auto *remove=menu.addAction(QStringLiteral("删除…"));
    const auto ids=selectedIds(); copy->setEnabled(ids.size()==1); edit->setEnabled(ids.size()==1); move->setEnabled(!ids.isEmpty()); remove->setEnabled(!ids.isEmpty());
    const auto *action=menu.exec(items_->viewport()->mapToGlobal(position));
    if (action==copy) copySelected(); else if (action==edit) editItem(false); else if (action==move) moveSelected(); else if (action==remove) deleteSelected();
}

void ClipboardPage::editItem(bool creating)
{
    AppDatabase::ClipboardItem item; QString error;
    if (creating) {
        const auto *selected=items_->currentItem();
        item.tabId=selected ? selected->data(0,TabRole).toInt() : tabId();
        item.folderId=selected ? selected->data(0,FolderIdRole).toInt() : 0;
    }
    else {
        const auto ids=selectedIds(); if (ids.size()!=1) return;
        item=database_->clipboardItem(ids.first(),&error); if (!item.id) { report(error); return; }
    }
    ClipboardEditorDialog dialog(this); editor_=&dialog;
    dialog.setObjectName("clipboardEditor"); dialog.setWindowTitle(creating ? QStringLiteral("新建剪贴板条目") : QStringLiteral("编辑剪贴板条目")); dialog.resize(640,470);
    auto *layout=new QVBoxLayout(&dialog); auto *form=new QFormLayout;
    auto *title=new QLineEdit(item.title,&dialog); title->setObjectName("clipboardItemTitle"); title->setMaxLength(200); title->setPlaceholderText(QStringLiteral("可选，留空时显示内容开头"));
    readablePlaceholder(title);
    form->addRow(QStringLiteral("标题"),title); layout->addLayout(form);
    const bool editable=creating || item.kind=="text";
    auto *body=new TextLayoutGuard::Edit(&dialog); body->setObjectName("clipboardItemBody"); body->setReadOnly(!editable); TextLayoutGuard::setPlainText(body,editable ? item.text : item.text.left(200000));
    const QString initialBody=body->toPlainText();
    body->setPlaceholderText(QStringLiteral("填写要保存的文本…")); layout->addWidget(body,1);
    readablePlaceholder(body);
    if (!editable) {
        auto *hint=label(QStringLiteral("此条目保留原始格式，可修改标题。需要修改正文时，可另存为纯文本条目。"),"muted",&dialog); hint->setWordWrap(true); layout->addWidget(hint);
        if (!item.text.isEmpty()) {
            auto *asText=new QPushButton(QStringLiteral("另存为纯文本"),&dialog); layout->addWidget(asText,0,Qt::AlignLeft);
            connect(asText,&QPushButton::clicked,&dialog,[&] {
                AppDatabase::ClipboardItem copy; copy.tabId=item.tabId; copy.folderId=item.folderId; copy.title=title->text().trimmed(); QString e;
                if (!ClipboardContent::plainText(item.text,&copy,&e) || !database_->saveClipboardItem(&copy,&e)) { QMessageBox::warning(&dialog,QStringLiteral("保存失败"),e); return; }
                dialog.accept();
            });
        }
    }
    auto *errorLabel=label(QString(),"clipboardEditorError",&dialog); errorLabel->setWordWrap(true); layout->addWidget(errorLabel);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog); buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存")); buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消")); layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        QString e; auto saved=item; saved.title=title->text().trimmed();
        if (editable && (creating || body->toPlainText()!=initialBody)
            && !ClipboardContent::plainText(body->toPlainText(),&saved,&e)) { errorLabel->setText(e); return; }
        if (!database_->saveClipboardItem(&saved,&e)) { errorLabel->setText(e); return; }
        dialog.accept();
    });
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    dialog.dirty=[&] { return title->text()!=item.title || (editable && body->toPlainText()!=initialBody); };
    dialog.save=[&] { buttons->button(QDialogButtonBox::Save)->click(); };
    auto *save=new QShortcut(QKeySequence::Save,&dialog); connect(save,&QShortcut::activated,buttons->button(QDialogButtonBox::Save),&QPushButton::click);
    if (dialog.exec()==QDialog::Accepted) refreshItems();
    editor_.clear();
}

bool ClipboardPage::savePendingChanges()
{
    if (editor_ && editor_->isVisible()) editor_->reject();
    return !editor_ || !editor_->isVisible();
}

void ClipboardPage::moveSelected()
{
    const auto ids=selectedIds(); if (ids.isEmpty()) return;
    QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("移动 %1 条内容").arg(ids.size()));
    auto *layout=new QFormLayout(&dialog); auto *tabs=new QComboBox(&dialog); tabs->setObjectName("clipboardDestinationTab");
    auto *folders=new QComboBox(&dialog); folders->setObjectName("clipboardDestinationFolder");
    for (const auto &tab : database_->clipboardTabs()) tabs->addItem(tab.name,tab.id);
    const auto load=[&] { folders->clear(); folders->addItem(QStringLiteral("标签页顶层"),0); for (const auto &folder : database_->clipboardFolders(tabs->currentData().toInt())) folders->addItem(folder.name,folder.id); };
    connect(tabs,&QComboBox::currentIndexChanged,&dialog,load); tabs->setCurrentIndex(qMax(0,tabs->findData(tabId()))); load();
    layout->addRow(QStringLiteral("标签页"),tabs); layout->addRow(QStringLiteral("文件夹"),folders);
    auto *errorLabel=label(QString(),"clipboardMoveError",&dialog); errorLabel->setWordWrap(true); layout->addRow(errorLabel);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog); buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("移动")); buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消")); layout->addRow(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        QString error;
        if (!database_->moveClipboardItems(ids,tabs->currentData().toInt(),folders->currentData().toInt(),&error)) { errorLabel->setText(error); return; }
        dialog.accept();
    });
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if (dialog.exec()==QDialog::Accepted) { refreshItems(); status_->hide(); }
}

void ClipboardPage::deleteSelected()
{
    const auto ids=selectedIds(); if (ids.isEmpty()) return;
    if (QMessageBox::question(this,QStringLiteral("删除剪贴板内容"),QStringLiteral("删除选中的 %1 条内容？此操作无法撤销。").arg(ids.size()),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return;
    QString error; if (!database_->deleteClipboardItems(ids,&error)) { report(error); return; } refreshItems();
}

void ClipboardPage::historySettings()
{
    QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("历史设置")); auto *layout=new QFormLayout(&dialog);
    auto *limit=new QSpinBox(&dialog); limit->setRange(100,10000); limit->setSingleStep(100); limit->setValue(monitor_->historyLimit()); limit->setSuffix(QStringLiteral(" 条"));
    layout->addRow(QStringLiteral("自动历史保留上限"),limit);
    auto *hint=label(QStringLiteral("下次记录新内容时清理超额的自动历史。手动新建、编辑或移动过的内容会保留。\n单条内容上限 16 MiB；图片上限 3200 万像素。"),"muted",&dialog); hint->setWordWrap(true); hint->setMaximumWidth(390); layout->addRow(hint);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog); layout->addRow(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] { QString error; if (!monitor_->setHistoryLimit(limit->value(),&error)) { QMessageBox::warning(&dialog,QStringLiteral("设置未保存"),error); return; } dialog.accept(); });
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject); dialog.exec();
}
