#include "clipboardpage.h"
#include "clipboard/clipboardcontent.h"
#include "clipboard/clipboardmonitor.h"
#include "widgets/textlayoutguard.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
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
#include <QVBoxLayout>

namespace {
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
class ClipboardList final : public SiblingOrderList {
public:
    using SiblingOrderList::SiblingOrderList;
protected:
    void startDrag(Qt::DropActions actions) override {
        if (selectedItems().size()==1) SiblingOrderList::startDrag(actions);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (selectedItems().size()>1 && event->modifiers()==Qt::ControlModifier
            && (event->key()==Qt::Key_Up || event->key()==Qt::Key_Down)) { event->accept(); return; }
        SiblingOrderList::keyPressEvent(event);
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
    search_=new QLineEdit(this); search_->setObjectName("clipboardSearch"); search_->setPlaceholderText(QStringLiteral("搜索内容或标题…"));
    readablePlaceholder(search_);
    search_->setClearButtonEnabled(true); search_->setAccessibleName(QStringLiteral("搜索剪贴板内容")); searchRow->addWidget(search_,1);
    searchScope_=new QComboBox(this); searchScope_->setObjectName("clipboardSearchScope");
    searchScope_->addItems({QStringLiteral("当前标签页"),QStringLiteral("全部标签页")}); searchScope_->setAccessibleName(QStringLiteral("搜索范围"));
    searchRow->addWidget(searchScope_); layout->addLayout(searchRow);

    auto *splitter=new QSplitter(Qt::Horizontal,this); splitter->setChildrenCollapsible(false);
    auto *folderPane=new QWidget(splitter); auto *folderLayout=new QVBoxLayout(folderPane); folderLayout->setContentsMargins(0,0,8,0);
    folderLayout->addWidget(label(QStringLiteral("文件夹"),"cardTitle",folderPane));
    folders_=new SiblingOrderList(folderPane); folders_->setObjectName("clipboardFolders"); folders_->setMinimumWidth(125);
    folders_->setIconSize(QSize(16,16));
    folders_->setStyleSheet(QStringLiteral("QListWidget::item { padding: 7px 8px; color: #334155; } QListWidget::item:hover { background: #f1f5fb; } QListWidget::item:selected { color: #1d4ed8; background: #e0ebff; }"));
    folders_->setContextMenuPolicy(Qt::CustomContextMenu); folders_->setAccessibleName(QStringLiteral("当前标签页的文件夹"));
    folderLayout->addWidget(folders_,1);
    auto *newFolder=new QPushButton(QStringLiteral("新建文件夹"),folderPane); folderLayout->addWidget(newFolder);

    auto *content=new QWidget(splitter); auto *contentLayout=new QVBoxLayout(content); contentLayout->setContentsMargins(0,0,0,0); contentLayout->setSpacing(10);
    count_=label(QString(),"muted",content); contentLayout->addWidget(count_);
    auto *contentSplit=new QSplitter(Qt::Vertical,content); contentSplit->setChildrenCollapsible(false);
    auto *listPane=new QWidget(contentSplit); auto *listLayout=new QVBoxLayout(listPane); listLayout->setContentsMargins(0,0,0,0); listLayout->setSpacing(0);
    items_=new ClipboardList(listPane); items_->setObjectName("clipboardItems"); items_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    items_->setStyleSheet(QStringLiteral("QListWidget::item { padding: 4px 8px; color: #334155; } QListWidget::item:hover { background: #f1f5fb; } QListWidget::item:selected { color: #1d4ed8; background: #e0ebff; }"));
    items_->setContextMenuPolicy(Qt::CustomContextMenu); items_->setMinimumHeight(110); items_->setWordWrap(false);
    items_->setTextElideMode(Qt::ElideRight); items_->setUniformItemSizes(true); items_->setAccessibleName(QStringLiteral("剪贴板条目"));
    listLayout->addWidget(items_,1);
    empty_=label(QString(),"clipboardEmpty",listPane); empty_->setWordWrap(true); empty_->setAlignment(Qt::AlignCenter);
    empty_->setStyleSheet(QStringLiteral("color: #52627a; padding: 18px;")); listLayout->addWidget(empty_);

    auto *previewPane=new QWidget(contentSplit); auto *previewLayout=new QVBoxLayout(previewPane); previewLayout->setContentsMargins(0,4,0,0);
    previewTitle_=label(QStringLiteral("选择条目查看内容"),"muted",previewPane); previewTitle_->setWordWrap(true); previewLayout->addWidget(previewTitle_);
    previewImage_=label(QString(),"clipboardImage",previewPane); previewImage_->setAlignment(Qt::AlignCenter); previewImage_->setMinimumSize(1,1);
    previewImage_->setMaximumHeight(220); previewImage_->hide(); previewLayout->addWidget(previewImage_);
    previewText_=new TextLayoutGuard::Edit(previewPane); previewText_->setObjectName("clipboardPreview"); previewText_->setReadOnly(true);
    previewText_->setMinimumHeight(65); previewText_->setAccessibleName(QStringLiteral("条目内容预览")); previewLayout->addWidget(previewText_,1);
    contentSplit->setSizes({280,180}); contentSplit->setStretchFactor(0,3); contentSplit->setStretchFactor(1,2); contentLayout->addWidget(contentSplit,1);

    auto *actions=new QHBoxLayout; actions->setSpacing(6);
    auto *create=new QPushButton(QStringLiteral("新建"),content); create->setObjectName("clipboardNewItem");
    edit_=new QPushButton(QStringLiteral("编辑"),content); edit_->setObjectName("clipboardEditItem");
    move_=new QPushButton(QStringLiteral("移动到…"),content); move_->setObjectName("clipboardMoveItems");
    remove_=new QPushButton(QStringLiteral("删除"),content); remove_->setObjectName("clipboardDeleteItems");
    copy_=new QPushButton(QStringLiteral("复制并收起"),content); copy_->setObjectName("primaryButton");
    copy_->setToolTip(QStringLiteral("复制选中条目并隐藏 Orchestrate，随后可手动 Ctrl+V。"));
    actions->addWidget(create); actions->addWidget(edit_); actions->addWidget(move_); actions->addWidget(remove_); actions->addStretch(); actions->addWidget(copy_);
    contentLayout->addLayout(actions);
    splitter->setSizes({170,670}); splitter->setStretchFactor(0,0); splitter->setStretchFactor(1,1); layout->addWidget(splitter,1);
    status_=label(QString(),"clipboardStatus",this); status_->setWordWrap(true); status_->setStyleSheet(QStringLiteral("color: #52627a;")); layout->addWidget(status_);

    refreshTimer_=new QTimer(this); refreshTimer_->setSingleShot(true); refreshTimer_->setInterval(100);
    connect(refreshTimer_,&QTimer::timeout,this,&ClipboardPage::refreshItems);
    connect(monitor_,&ClipboardMonitor::recorded,this,[this] {
        dirty_=true;
        status_->setText(QStringLiteral("监听中 · 已保存到“历史”。双击条目复制并收起。"));
        if (isVisible()) refreshTimer_->start();
    });
    connect(monitor_,&ClipboardMonitor::captureError,this,&ClipboardPage::report);
    connect(listening_,&QCheckBox::toggled,this,[this](bool enabled) {
        QString error;
        if (!monitor_->setEnabled(enabled,&error)) { QSignalBlocker blocker(listening_); listening_->setChecked(monitor_->enabled()); report(error); return; }
        status_->setText(enabled ? QStringLiteral("监听中 · 新复制的内容进入“历史”。") : QStringLiteral("监听已暂停 · 已保存的内容仍可使用。"));
    });
    connect(settings,&QPushButton::clicked,this,&ClipboardPage::historySettings);
    connect(newTab,&QPushButton::clicked,this,&ClipboardPage::addTab);
    connect(newFolder,&QPushButton::clicked,this,&ClipboardPage::addFolder);
    connect(tabs_,&QTabBar::currentChanged,this,[this] {
        database_->setSetting("clipboard.tab",QString::number(tabId()));
        if (tabId()!=displayedTabId_) refreshFolders();
    });
    connect(tabs_,&QTabBar::tabMoved,this,[this] {
        QList<int> ids; for (int i=0; i<tabs_->count(); ++i) ids.append(tabs_->tabData(i).toInt());
        QString error;
        if (!database_->reorderClipboardTabs(ids,&error)) { const int selected=tabId(); refreshTabs(selected); report(error); }
    });
    connect(tabs_,&QTabBar::customContextMenuRequested,this,&ClipboardPage::tabMenu);
    connect(folders_,&QListWidget::currentRowChanged,this,[this] { refreshItems(); });
    connect(folders_,&QListWidget::customContextMenuRequested,this,&ClipboardPage::folderMenu);
    folders_->saveOrder=[this](QListWidgetItem *,const QList<int> &ids) {
        QString error; const bool ok=database_->reorderClipboardFolders(tabId(),ids,&error); if (!ok) report(error); return ok;
    };
    items_->saveOrder=[this](QListWidgetItem *,const QList<int> &ids) {
        QString error; const bool ok=database_->reorderClipboardItems(tabId(),folderId(),ids,&error); if (!ok) report(error); return ok;
    };
    connect(search_,&QLineEdit::textChanged,this,[this] { refreshTimer_->start(); });
    connect(searchScope_,&QComboBox::currentIndexChanged,this,[this] { refreshItems(); });
    connect(items_,&QListWidget::itemSelectionChanged,this,&ClipboardPage::updatePreview);
    connect(items_,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem *item) { items_->clearSelection(); item->setSelected(true); items_->setCurrentItem(item); copySelected(); });
    connect(items_,&QListWidget::customContextMenuRequested,this,&ClipboardPage::itemMenu);
    connect(create,&QPushButton::clicked,this,[this] { editItem(true); });
    connect(edit_,&QPushButton::clicked,this,[this] { editItem(false); });
    connect(copy_,&QPushButton::clicked,this,&ClipboardPage::copySelected);
    connect(move_,&QPushButton::clicked,this,&ClipboardPage::moveSelected);
    connect(remove_,&QPushButton::clicked,this,&ClipboardPage::deleteSelected);
    auto *enter=new QShortcut(QKeySequence(Qt::Key_Return),items_); enter->setContext(Qt::WidgetShortcut); connect(enter,&QShortcut::activated,this,&ClipboardPage::copySelected);
    auto *find=new QShortcut(QKeySequence::Find,this); find->setContext(Qt::WidgetWithChildrenShortcut); connect(find,&QShortcut::activated,search_,qOverload<>(&QWidget::setFocus));
    status_->setText(monitor_->enabled() ? QStringLiteral("监听中 · 双击条目复制并收起，随后手动 Ctrl+V。") : QStringLiteral("监听已暂停 · 开启右上角开关后，自动保存新复制的内容。"));
    refreshTabs(database_->setting("clipboard.tab","1").toInt());
}

int ClipboardPage::tabId() const { return tabs_->tabData(tabs_->currentIndex()).toInt(); }
int ClipboardPage::folderId() const { return folders_->currentItem() ? folders_->currentItem()->data(Qt::UserRole).toInt() : 0; }
QList<int> ClipboardPage::selectedIds() const
{
    QList<int> ids;
    // Preserve visual order for bulk moves.
    for (int i=0; i<items_->count(); ++i) if (items_->item(i)->isSelected()) ids.append(items_->item(i)->data(Qt::UserRole).toInt());
    return ids;
}
void ClipboardPage::showEvent(QShowEvent *event) { QWidget::showEvent(event); if (dirty_) refreshItems(); }
void ClipboardPage::report(const QString &message) { status_->setText(message); }

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
    refreshFolders(displayedTabId_==tabId() ? folderId() : 0);
}

void ClipboardPage::refreshFolders(int selected)
{
    displayedTabId_=tabId();
    QString error; const auto folders=database_->clipboardFolders(tabId(),&error);
    if (!error.isEmpty()) { report(error); return; }
    {
        const QSignalBlocker blocker(folders_); folders_->clear();
        auto *root=new QListWidgetItem(QStringLiteral("未归档"),folders_); root->setData(Qt::UserRole,0);
        auto *all=new QListWidgetItem(QStringLiteral("全部内容"),folders_); all->setData(Qt::UserRole,-1);
        QListWidgetItem *current=selected==-1 ? all : root;
        for (const auto &folder : folders) {
            auto *item=new QListWidgetItem(style()->standardIcon(QStyle::SP_DirIcon),folder.name,folders_);
            item->setData(Qt::UserRole,folder.id); item->setData(SiblingOrderScopeRole,QStringLiteral("folders.%1").arg(tabId()));
            item->setToolTip(folder.name+QStringLiteral(" · 拖动或 Ctrl+↑/↓ 调位"));
            if (folder.id==selected) current=item;
        }
        folders_->setCurrentItem(current);
    }
    refreshItems();
}

void ClipboardPage::refreshItems()
{
    refreshTimer_->stop(); dirty_=false;
    const auto selected=selectedIds(); const int scroll=items_->verticalScrollBar()->value();
    const auto search=search_->text().trimmed(); const bool global=searchScope_->currentIndex()==1;
    const int folder=global || !search.isEmpty() ? -1 : folderId();
    QString error; const auto entries=database_->clipboardItems(global ? 0 : tabId(),folder,search,&error);
    if (!error.isEmpty()) { report(error); return; }
    {
        const QSignalBlocker blocker(items_); items_->clear();
        items_->orderingEnabled=!global && search.isEmpty() && folder>=0;
        for (const auto &entry : entries) {
            const auto caption=itemTitle(entry);
            const auto date=entry.createdAt.toLocalTime().toString(QStringLiteral("MM-dd HH:mm"));
            auto *item=new QListWidgetItem(caption+QStringLiteral("\n")+ClipboardContent::kindName(entry.kind)+QStringLiteral(" · ")+date,items_);
            item->setData(Qt::UserRole,entry.id); item->setData(SiblingOrderScopeRole,QStringLiteral("items.%1.%2").arg(entry.tabId).arg(entry.folderId));
            item->setSizeHint(QSize(0,58)); item->setToolTip(caption+QStringLiteral("\n双击复制并收起"));
            if (selected.contains(entry.id)) { items_->setCurrentItem(item,QItemSelectionModel::NoUpdate); item->setSelected(true); }
        }
        items_->verticalScrollBar()->setValue(scroll);
    }
    count_->setText(QStringLiteral("%1 条内容%2").arg(entries.size()).arg(items_->orderingEnabled ? QStringLiteral(" · 拖动或 Ctrl+↑/↓ 调位") : QStringLiteral(" · 选择具体文件夹并清空搜索后可排序")));
    empty_->setText(!search.isEmpty() ? QStringLiteral("没有找到匹配内容。试试其他关键词。")
        : folderId()>0 ? QStringLiteral("这个文件夹还没有内容。新建条目，或从其他位置移动过来。")
        : tabId()==1 ? QStringLiteral("还没有内容。开启监听后复制一段文字、图片或文件，也可以手动新建。")
        : QStringLiteral("这个标签页还没有内容。可以新建条目，或从“历史”移动过来。"));
    empty_->setVisible(entries.isEmpty()); updatePreview();
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
    if (ids.size()!=1) {
        previewTitle_->setText(ids.isEmpty() ? QStringLiteral("选择条目查看内容") : QStringLiteral("已选择 %1 条，可批量移动或删除").arg(ids.size()));
        previewText_->clear(); return;
    }
    QString error; const auto item=database_->clipboardItem(ids.first(),&error);
    if (!item.id) { report(error); previewText_->clear(); return; }
    QString location;
    for (const auto &tab : database_->clipboardTabs()) if (tab.id==item.tabId) location=tab.name;
    for (const auto &folder : database_->clipboardFolders(item.tabId)) if (folder.id==item.folderId) location+=QStringLiteral(" / ")+folder.name;
    previewTitle_->setText(ClipboardContent::kindName(item.kind)+QStringLiteral(" · ")+location
        +(item.automatic ? QString() : QStringLiteral(" · 已保存"))+(item.text.size()>200000 ? QStringLiteral(" · 仅预览前 20 万字，复制保留完整内容") : QString()));
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
    status_->setText(QStringLiteral("已复制，可在目标位置手动 Ctrl+V。"));
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
    search_->clear(); searchScope_->setCurrentIndex(0); refreshFolders(id);
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
    const auto *item=folders_->itemAt(position); if (!item) return;
    const int id=item->data(Qt::UserRole).toInt();
    QMenu menu(this); auto *create=menu.addAction(QStringLiteral("新建文件夹"));
    auto *rename=menu.addAction(QStringLiteral("重命名")); auto *remove=menu.addAction(QStringLiteral("删除文件夹…"));
    rename->setEnabled(id>0); remove->setEnabled(id>0);
    const auto *action=menu.exec(folders_->viewport()->mapToGlobal(position)); QString error;
    if (action==create) addFolder();
    else if (action==rename) {
        bool ok=false; const auto name=QInputDialog::getText(this,QStringLiteral("重命名文件夹"),QStringLiteral("文件夹名称"),QLineEdit::Normal,item->text(),&ok);
        if (ok) { if (!database_->renameClipboardFolder(id,name,&error)) report(error); else refreshFolders(id); }
    } else if (action==remove) {
        if (QMessageBox::question(this,QStringLiteral("删除文件夹"),QStringLiteral("删除“%1”？内容会保留并移到当前标签页的“未归档”。").arg(item->text()),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return;
        if (!database_->deleteClipboardFolder(id,&error)) report(error); else refreshFolders();
    }
}

void ClipboardPage::itemMenu(const QPoint &position)
{
    QMenu menu(this); auto *copy=menu.addAction(QStringLiteral("复制并收起")); auto *edit=menu.addAction(QStringLiteral("编辑"));
    auto *move=menu.addAction(QStringLiteral("移动到…")); auto *remove=menu.addAction(QStringLiteral("删除…"));
    const auto ids=selectedIds(); copy->setEnabled(ids.size()==1); edit->setEnabled(ids.size()==1); move->setEnabled(!ids.isEmpty()); remove->setEnabled(!ids.isEmpty());
    const auto *action=menu.exec(items_->viewport()->mapToGlobal(position));
    if (action==copy) copySelected(); else if (action==edit) editItem(false); else if (action==move) moveSelected(); else if (action==remove) deleteSelected();
}

void ClipboardPage::editItem(bool creating)
{
    AppDatabase::ClipboardItem item; QString error;
    if (creating) { item.tabId=tabId(); item.folderId=qMax(0,folderId()); }
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
    const auto load=[&] { folders->clear(); folders->addItem(QStringLiteral("未归档"),0); for (const auto &folder : database_->clipboardFolders(tabs->currentData().toInt())) folders->addItem(folder.name,folder.id); };
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
    if (dialog.exec()==QDialog::Accepted) { refreshItems(); status_->setText(QStringLiteral("已移动 %1 条内容。").arg(ids.size())); }
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
