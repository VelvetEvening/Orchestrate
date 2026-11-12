#include "ui/theme.h"
#include "ui/contentworkspace.h"
#include "widgets/emptyviewhint.h"
#include "projectspage.h"
#include "platform/fileaccess.h"
#include "collaborationrecorddialog.h"
#include "githistorywindow.h"
#include "repositorybinding.h"
#include "widgets/summaryfield.h"
#include "widgets/treeaccessibility.h"
#include "widgets/siblingorderview.h"
#include <QDesktopServices>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QMenu>
#include <QActionGroup>
#include <QStyle>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>
#include <QUrl>

namespace {
constexpr int FolderRole = Qt::UserRole + 1;
constexpr int SummaryRole = Qt::UserRole + 2;
constexpr int StarredRole = Qt::UserRole + 3;
QString projectStatus(bool completed) {
    return completed ? QStringLiteral("已完结") : QStringLiteral("进行中");
}
void updateProjectStatus(QListWidgetItem *item, bool completed) {
    const QString name = item->data(Qt::UserRole + 3).toString();
    const QString directory = item->data(Qt::UserRole + 1).toString();
    item->setData(Qt::UserRole + 2, completed);
    item->setIcon(UiTheme::icon(completed ? QStringLiteral("project-complete") : QStringLiteral("project-active")));
    item->setData(Qt::AccessibleTextRole, name + QStringLiteral("，") + projectStatus(completed));
    item->setToolTip(name + QStringLiteral("\n") + projectStatus(completed)
        + QStringLiteral(" · 右键修改状态或打开项目设置")
        + (directory.isEmpty() ? QString() : QStringLiteral("\n双击打开目录：") + QDir::toNativeSeparators(directory)));
}
QString expandedFoldersKey(int projectId) {
    return QStringLiteral("projects.%1.expanded_record_folders").arg(projectId);
}
QFrame *surface(QWidget *parent, const char *role) {
    auto *widget = new QFrame(parent);
    UiTheme::setSurface(widget, role);
    return widget;
}
QLabel *label(const QString &text, const QString &name, QWidget *parent) {
    auto *result = new QLabel(text, parent);
    result->setTextFormat(Qt::PlainText);
    result->setObjectName(name);
    return result;
}
QString recordTitle(const AppDatabase::WorkRecord &record) {
    if (!record.title.trimmed().isEmpty()) return record.title;
    for (QString line : record.content.split('\n')) {
        line = line.trimmed();
        if (!line.isEmpty()) return line.left(120);
    }
    return QStringLiteral("未命名记录");
}
void styleList(QListWidget *list) {
    list->setSpacing(4);
    list->setUniformItemSizes(true);
    list->setTextElideMode(Qt::ElideRight);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

}
}

ProjectsPage::ProjectsPage(AppDatabase *database, QWidget *parent) : QWidget(parent), database_(database)
{
    UiTheme::setSurface(this, "content");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *splitter = new QSplitter(this);
    splitter->setChildrenCollapsible(false);
    splitter->setObjectName(QStringLiteral("workspaceSplit"));
    splitter->setHandleWidth(1);
    auto *listCard = surface(splitter, "index");
    listCard->setObjectName(QStringLiteral("workspaceIndex"));
    auto *left = new QVBoxLayout(listCard);
    left->setContentsMargins(16,22,16,24);
    left->addWidget(label(QStringLiteral("项目列表"), QStringLiteral("cardTitle"), listCard));
    auto *orderedProjects = new SiblingOrderList(listCard);
    projectList_ = orderedProjects;
    orderedProjects->saveOrder = [this](QListWidgetItem *, const QList<int> &ids) {
        QString error;
        const bool saved = database_->reorderProjects(ids, &error);
        if (!saved) showError(error);
        return saved;
    };
    projectList_->setObjectName(QStringLiteral("projectList"));
    projectList_->setContextMenuPolicy(Qt::CustomContextMenu);
    projectList_->setAccessibleDescription(QStringLiteral("拖动或 Ctrl+上或下箭头调整项目顺序；圆点表示进行中，勾选表示已完结；右键切换状态或打开项目设置；双击打开目录"));
    projectList_->setIconSize(QSize(18,18));
    styleList(projectList_);
    left->addWidget(projectList_, 1);
    auto *create = new QPushButton(QStringLiteral("新建项目"), listCard);
    create->setObjectName(QStringLiteral("primaryButton"));
    left->addWidget(create);
    details_ = surface(splitter, "content");
    detailsLayout_ = new QVBoxLayout(details_);
    detailsLayout_->setContentsMargins(24,22,24,24);
    detailsLayout_->setSpacing(12);
    auto *header = new QHBoxLayout;
    projectTitle_ = label(QString(), QStringLiteral("cardTitle"), details_);
    projectTitle_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header->addWidget(projectTitle_,1);
    gitHistoryButton_ = new QPushButton(QStringLiteral("Git 历史"), details_);
    gitHistoryButton_->setObjectName(QStringLiteral("gitHistoryButton"));
    UiTheme::setRole(gitHistoryButton_, "quiet");
    gitHistoryButton_->setToolTip(QStringLiteral("查看提交历史和分支对比"));
    connect(gitHistoryButton_, &QPushButton::clicked, this, &ProjectsPage::openGitHistory);
    gitHistoryButton_->installEventFilter(this);
    header->addWidget(gitHistoryButton_);
    auto *createFolder = new QPushButton(QStringLiteral("新建文件夹"), details_);
    createFolder->setObjectName(QStringLiteral("createRecordFolder"));
    UiTheme::setRole(createFolder, "quiet");
    header->addWidget(createFolder);
    detailsLayout_->addLayout(header);

    auto *recordsPanel_ = new QWidget(details_);
    recordsPanel_->setObjectName(QStringLiteral("recordsPanel"));
    auto *records = new QVBoxLayout(recordsPanel_);
    records->setContentsMargins(0,0,0,0);
    search_ = new QLineEdit(recordsPanel_);
    search_->setObjectName(QStringLiteral("recordSearch"));
    search_->setPlaceholderText(QStringLiteral("搜索标题、摘要或文件夹"));
    search_->setAccessibleName(QStringLiteral("搜索协作记录"));
    search_->setClearButtonEnabled(true);
    records->addWidget(search_);
    auto *orderedRecords = new SiblingOrderTree(recordsPanel_);
    workList_ = orderedRecords;
    orderedRecords->saveOrder = [this](QTreeWidgetItem *item, const QList<int> &ids) {
        QString error;
        const bool saved = item->data(0, FolderRole).toBool()
            ? database_->reorderRecordFolders(current_.id, item->data(0, StarredRole).toBool(), ids, &error)
            : database_->reorderWorkRecords(current_.id, item->parent() ? item->parent()->data(0, Qt::UserRole).toInt() : 0, ids, &error);
        if (!saved) showError(error);
        return saved;
    };
    orderedRecords->isContainer = [](QTreeWidgetItem *item) { return item->data(0,FolderRole).toBool(); };
    orderedRecords->canTransfer = [](const QList<QTreeWidgetItem *> &items, QTreeWidgetItem *folder) {
        return items.size() == 1 && !items.first()->data(0,FolderRole).toBool()
            && (!folder || folder->data(0,FolderRole).toBool()) && items.first()->parent() != folder;
    };
    orderedRecords->saveTransfer = [this](const QList<QTreeWidgetItem *> &items, QTreeWidgetItem *folder) {
        const int folderId = folder ? folder->data(0,Qt::UserRole).toInt() : 0;
        QString error;
        if (!database_->setWorkRecordFolder(items.first()->data(0,Qt::UserRole).toInt(),folderId,&error)) {
            showError(error); return false;
        }
        if (folder) { folder->setExpanded(true); saveExpandedFolders(); }
        refreshWorkRecords();
        if (workList_->currentItem()) workList_->scrollToItem(workList_->currentItem());
        return true;
    };
    synchronizeTreeAccessibility(workList_);
    workList_->setObjectName(QStringLiteral("workRecordList"));
    workList_->setHeaderHidden(true);
    workList_->header()->setStretchLastSection(true);
    workList_->setUniformRowHeights(true);
    workList_->setIndentation(20);
    workList_->setIconSize(QSize(16,16));
    workList_->setTextElideMode(Qt::ElideRight);
    workList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    workList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    workList_->setContextMenuPolicy(Qt::CustomContextMenu);
    workList_->setAccessibleName(QStringLiteral("协作记录与文件夹"));
    workList_->setToolTip(QStringLiteral("拖入文件夹归类，拖到空白处移回顶层；Ctrl+↑/↓ 调整同层顺序。"));
    workList_->setAccessibleDescription(QStringLiteral("拖动排序或移入文件夹，空白处为项目顶层；Ctrl+上或下箭头调序。搜索时暂停拖动。"));

    workList_->installEventFilter(this);
    workList_->setMinimumHeight(120);
    records->addWidget(workList_,1);
    recordHint_ = label(QString(), QStringLiteral("recordHint"), recordsPanel_);
    recordHint_->setWordWrap(true);

    records->addWidget(recordHint_);
    auto *actions = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("添加记录"), recordsPanel_);
    add->setObjectName(QStringLiteral("primaryButton"));
    editWorkButton_ = new QPushButton(QStringLiteral("打开"), recordsPanel_);
    UiTheme::setRole(editWorkButton_, "quiet");
    deleteWorkButton_ = new QPushButton(QStringLiteral("删除"), recordsPanel_);
    UiTheme::setRole(deleteWorkButton_, "danger");
    actions->addWidget(add);
    actions->addWidget(editWorkButton_);
    actions->addWidget(deleteWorkButton_);
    actions->addStretch();
    records->addLayout(actions);
    detailsLayout_->addWidget(recordsPanel_,1);
    splitter->addWidget(listCard);
    splitter->addWidget(details_);
    splitter->setSizes({230,650});
    splitter->setStretchFactor(0,0);
    splitter->setStretchFactor(1,1);
    layout->addWidget(splitter,1);

    connect(create, &QPushButton::clicked, this, [this]{ projectDialog(true); });
    connect(projectList_, &QListWidget::customContextMenuRequested, this, &ProjectsPage::showProjectContextMenu);
    connect(projectList_, &QListWidget::currentRowChanged, this, [this](int row){
        if (row >= 0) selectProject(projectList_->item(row)->data(Qt::UserRole).toInt());
    });
    connect(projectList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item) for (const auto &project : database_->projects())
            if (project.id == item->data(Qt::UserRole).toInt()) { openProjectDirectory(project); break; }
    });
    connect(add, &QPushButton::clicked, this, [this]{ recordDialog(true); });
    connect(createFolder, &QPushButton::clicked, this, [this]{ folderDialog(true); });
    connect(editWorkButton_, &QPushButton::clicked, this, [this]{ recordDialog(false); });
    connect(deleteWorkButton_, &QPushButton::clicked, this, &ProjectsPage::deleteWorkRecord);
    connect(workList_, &QTreeWidget::itemDoubleClicked, this, [this]{ recordDialog(false); });
    connect(workList_, &QTreeWidget::itemSelectionChanged, this, &ProjectsPage::updateRecordActions);
    connect(workList_, &QTreeWidget::customContextMenuRequested, this, &ProjectsPage::showRecordContextMenu);
    connect(workList_, &QTreeWidget::itemExpanded, this, [this]{ saveExpandedFolders(); updateRecordActions(); });
    connect(workList_, &QTreeWidget::itemCollapsed, this, [this]{ saveExpandedFolders(); updateRecordActions(); });
    connect(search_, &QLineEdit::textChanged, this, &ProjectsPage::filterRecords);
    loadProjects(database_->setting(QStringLiteral("projects.last_selected")).toInt());
}

void ProjectsPage::loadProjects(int preferred)
{
    const QSignalBlocker blocker(projectList_);
    projectList_->clear();
    QString error;
    const auto projects = database_->projects(&error);
    if (!error.isEmpty()) { showError(error); return; }
    int row = 0;
    for (const auto &project : projects) {
        auto *item = new QListWidgetItem(project.name, projectList_);
        item->setSizeHint(QSize(0,44));
        item->setData(Qt::UserRole,project.id);
        item->setData(SiblingOrderScopeRole, QStringLiteral("projects"));
        item->setData(Qt::UserRole + 1, project.directory);
        item->setData(Qt::UserRole + 3, project.name);
        updateProjectStatus(item, project.completed);
        if (project.id == preferred) row = projectList_->count()-1;
    }
    EmptyViewHint::update(projectList_, projects.isEmpty()
        ? QStringLiteral("还没有项目\n点击下方“新建项目”开始") : QString());
    if (projects.isEmpty()) {
        current_ = {};
        projectTitle_->setText(QStringLiteral("暂无项目"));
        details_->setEnabled(false);
        refreshWorkRecords();
        return;
    }
    details_->setEnabled(true);
    projectList_->setCurrentRow(row);
    selectProject(projectList_->item(row)->data(Qt::UserRole).toInt());
}

void ProjectsPage::showProjectContextMenu(const QPoint &position)
{
    auto *item = projectList_->itemAt(position);
    if (!item) return;
    const int id = item->data(Qt::UserRole).toInt();
    const bool completed = item->data(Qt::UserRole + 2).toBool();
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("projectStatusMenu"));
    auto *group = new QActionGroup(&menu);
    for (bool value : {false, true}) {
        auto *action = menu.addAction(projectStatus(value));
        action->setCheckable(true);
        action->setChecked(completed == value);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, item, id, value, completed] {
            if (completed == value) return;
            QString error;
            if (!database_->setProjectCompleted(id, value, &error)) { showError(error); return; }
            updateProjectStatus(item, value);
            if (current_.id == id) current_.completed = value;
        });
    }
    menu.addSeparator();
    menu.addAction(UiTheme::icon(QStringLiteral("settings")), QStringLiteral("项目设置"), this, [this, item, id] {
        projectList_->setCurrentItem(item);
        if (current_.id == id) projectDialog(false);
    });
    menu.exec(projectList_->viewport()->mapToGlobal(position));
}

void ProjectsPage::openProjectDirectory(const AppDatabase::Project &project)
{
    const QString directory = project.directory;
    if (directory.isEmpty() || openingDirectory_) return;
    if (project.targetType == QStringLiteral("ssh")) {
        QDialog dialog(this); dialog.setWindowTitle(project.name + QStringLiteral(" · SSH 目录")); dialog.resize(580,220);
        auto *layout = new QVBoxLayout(&dialog);
        auto *info = label(project.repository().description(),QStringLiteral("sshProjectDirectory"),&dialog);
        info->setWordWrap(true); info->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(info);
        auto *copy = new QPushButton(QStringLiteral("复制路径"),&dialog); layout->addWidget(copy);
        connect(copy,&QPushButton::clicked,&dialog,[directory]{ QApplication::clipboard()->setText(directory); });
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close,&dialog); buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject); layout->addWidget(buttons); dialog.exec(); return;
    }
    if (project.targetType == QStringLiteral("wsl")) {
        openingDirectory_ = true;
        auto *reader = new Git::Reader(this);
        reader->probe(project.repository(),[this,reader,project](Git::Result result){
            openingDirectory_ = false; reader->deleteLater();
            const auto fields = result.output.split('\0');
            if (!result.ok || fields.size()!=3 || !fields[1].startsWith('/')) {
                QMessageBox::warning(this,QStringLiteral("无法打开 WSL 目录"),result.ok?QStringLiteral("无法识别目录，请在项目设置中重新验证。"):result.error); return;
            }
            auto local = project; local.targetType = QStringLiteral("local");
            local.directory = QStringLiteral("//wsl.localhost/") + project.targetName + QString::fromUtf8(fields[1]);
            openProjectDirectory(local);
        }); return;
    }
    openingDirectory_ = true;
    FileAccess::request(this, QStringLiteral("open-directory"), directory,
        [this, directory](bool ok, const QByteArray &, const QString &error) {
            openingDirectory_ = false;
            if (!ok) QMessageBox::warning(this, QStringLiteral("无法打开项目目录"),
                error + QStringLiteral("\n请在“项目设置”中检查目录。\n\n") + QDir::toNativeSeparators(directory));
        });
}

void ProjectsPage::selectProject(int id)
{
    QString error;
    for (const auto &project : database_->projects(&error)) {
        if (project.id != id) continue;
        current_ = project;
        projectTitle_->setText(project.name);
        projectTitle_->setToolTip(project.name);
        database_->setSetting(QStringLiteral("projects.last_selected"),QString::number(id));
        const QSignalBlocker searchBlock(search_);
        search_->clear();
        refreshWorkRecords();
        return;
    }
    if (!error.isEmpty()) showError(error);
}

void ProjectsPage::projectDialog(bool creating)
{
    if (!creating && current_.id <= 0) return;
    QDialog dialog(this);
    dialog.setWindowTitle(creating ? QStringLiteral("新建项目") : QStringLiteral("项目设置"));
    dialog.resize(640,360);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *name = new QLineEdit(&dialog);
    name->setObjectName(QStringLiteral("projectName"));
    form->addRow(QStringLiteral("项目名称"),name);
    layout->addLayout(form);
    auto *binding = new RepositoryBinding(creating ? RepositoryTarget{} : current_.repository(),&dialog);
    layout->addWidget(binding);
    if (!creating) name->setText(current_.name);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,&dialog);
    auto *save = buttons->button(QDialogButtonBox::Save);
    save->setText(creating ? QStringLiteral("新建") : QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    save->setEnabled(!name->text().trimmed().isEmpty());
    auto *footer = new QHBoxLayout;
    if (!creating) {
        auto *remove = new QPushButton(QStringLiteral("删除项目…"), &dialog);
        remove->setObjectName(QStringLiteral("deleteProjectButton"));
        remove->setAutoDefault(false);
        remove->setAccessibleDescription(QStringLiteral("打开删除确认，须输入完整确认文字"));

        footer->addWidget(remove);
        connect(remove, &QPushButton::clicked, &dialog, [this, &dialog] {
            if (deleteProject(&dialog)) dialog.reject();
        });
    }
    footer->addStretch();
    footer->addWidget(buttons);
    layout->addLayout(footer);
    connect(name,&QLineEdit::textChanged,&dialog,[name,save]{ save->setEnabled(!name->text().trimmed().isEmpty()); });
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        if (name->text().trimmed().isEmpty()) return;
        save->setEnabled(false); name->setEnabled(false);
        binding->resolve([&, creating](bool valid, RepositoryTarget target, const QString &) {
            save->setEnabled(true); name->setEnabled(true);
            if (!valid) return;
            auto project = creating ? AppDatabase::Project{} : current_;
            project.name=name->text().trimmed(); project.directory=target.directory;
            project.targetType=target.type; project.targetName=target.location; project.targetUser=target.user;
            QString error; int id=project.id;
            const bool ok=creating ? database_->addProject(project,&id,&error) : database_->updateProject(project,&error);
            if (!ok) { QMessageBox::critical(&dialog,QStringLiteral("保存失败"),error); return; }
            if (gitWindows_.value(id) && !(gitWindows_[id]->target()==target)) gitWindows_[id]->close();
            dialog.accept(); loadProjects(id);
        });
    });
    name->setFocus();
    dialog.exec();
}

bool ProjectsPage::deleteProject(QWidget *parent)
{
    if (current_.id<=0) return false;
    const int projectId = current_.id;
    const QString projectName = current_.name;
    const QString required = QStringLiteral("我确认删除（%1）").arg(projectName);
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("deleteProjectDialog"));
    dialog.setWindowTitle(QStringLiteral("确认删除项目"));
    dialog.resize(580, 240);
    auto *layout = new QVBoxLayout(&dialog);
    auto *message = label(QStringLiteral("将删除项目“%1”、全部协作记录及文件夹，此操作无法撤销。项目目录中的文件不会删除。\n\n如果确定要删除，请完整输入：\n%2")
                              .arg(projectName, required), QStringLiteral("deleteProjectPrompt"), &dialog);
    message->setWordWrap(true);
    layout->addWidget(message);
    auto *confirmation = new QLineEdit(&dialog);
    confirmation->setObjectName(QStringLiteral("deleteProjectConfirmation"));
    confirmation->setPlaceholderText(required);
    layout->addWidget(confirmation);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto *confirm = buttons->button(QDialogButtonBox::Ok);
    confirm->setText(QStringLiteral("确定"));
    confirm->setEnabled(false);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(confirmation, &QLineEdit::textChanged, &dialog, [=](const QString &text) {
        confirm->setEnabled(text == required);
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (confirmation->text() != required || current_.id != projectId || current_.name != projectName) return;
        if (auto *workspace=ContentWorkspace::find(this))
            if (!workspace->closePrefix(QStringLiteral("project/%1/").arg(projectId))) return;
        QString error;
        if (!database_->deleteProject(projectId, &error)) {
            QMessageBox::critical(&dialog, QStringLiteral("删除失败"), error);
            return;
        }
        if (gitWindows_.value(projectId)) gitWindows_[projectId]->close();
        gitWindows_.remove(projectId);
        current_ = {};
        dialog.accept();
        loadProjects();
    });
    confirmation->setFocus();
    return dialog.exec() == QDialog::Accepted;
}

void ProjectsPage::refreshWorkRecords()
{
    const auto *previous = workList_->currentItem();
    const int selected = recordsProjectId_ == current_.id && previous ? previous->data(0,Qt::UserRole).toInt() : 0;
    const bool selectedFolder = previous && previous->data(0,FolderRole).toBool();
    const QSignalBlocker blocker(workList_);
    workList_->clear();
    recordsProjectId_ = current_.id;
    editWorkButton_->setEnabled(false);
    deleteWorkButton_->setEnabled(false);
    recordHint_->hide();
    if (current_.id<=0) return;
    QString error;
    const auto folders = database_->recordFolders(current_.id, &error);
    if (!error.isEmpty()) { showError(error); return; }
    const auto entries = database_->workRecords(current_.id, &error);
    if (!error.isEmpty()) { showError(error); return; }
    const auto expanded = database_->setting(expandedFoldersKey(current_.id)).split(',');
    QHash<int, QTreeWidgetItem *> folderItems;
    QIcon folderIcon = UiTheme::icon(QStringLiteral("folder"));
    const QIcon starredIcon(QStringLiteral(":/icons/star.svg"));
    for (const auto &folder : folders) {
        auto *item = new QTreeWidgetItem(workList_, {folder.name});
        item->setData(0,Qt::UserRole,folder.id);
        item->setData(0,FolderRole,true);
        item->setData(0,StarredRole,folder.starred);
        item->setData(0,SiblingOrderScopeRole,QStringLiteral("folders.%1").arg(folder.starred ? 1 : 0));
        item->setIcon(0,folder.starred ? starredIcon : folderIcon);
        item->setData(0,Qt::AccessibleTextRole,folder.starred
            ? folder.name + QStringLiteral("，重点文件夹，已置顶") : folder.name);
        item->setSizeHint(0,QSize(0,44));
        item->setToolTip(0,folder.name + (folder.starred
            ? QStringLiteral("\n重点文件夹 · 已置顶\n右键取消重点标记、重命名或删除文件夹")
            : QStringLiteral("\n右键标记重点并置顶、重命名或删除文件夹")));
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        item->setExpanded(expanded.contains(QString::number(folder.id)));
        folderItems.insert(folder.id,item);
        if (selectedFolder && folder.id == selected) workList_->setCurrentItem(item);
    }
    for (const auto &record : entries) {
        auto *folder = folderItems.value(record.folderId);
        auto *item = folder ? new QTreeWidgetItem(folder) : new QTreeWidgetItem(workList_);
        item->setText(0,recordTitle(record));
        item->setSizeHint(0,QSize(0,44));
        item->setData(0,Qt::UserRole,record.id);
        item->setData(0,FolderRole,false);
        item->setData(0,SiblingOrderScopeRole,QStringLiteral("records.%1").arg(record.folderId));
        item->setData(0,SummaryRole,record.summary);
        item->setToolTip(0,recordTitle(record)+QStringLiteral("\n")+record.createdAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        if (!selectedFolder && record.id == selected) workList_->setCurrentItem(item);
    }
    filterRecords();
}

void ProjectsPage::saveExpandedFolders()
{
    if (current_.id <= 0 || !search_->text().isEmpty()) return;
    QStringList expanded;
    for (int i=0; i<workList_->topLevelItemCount(); ++i) {
        const auto *item = workList_->topLevelItem(i);
        if (item->data(0,FolderRole).toBool() && item->isExpanded())
            expanded.append(QString::number(item->data(0,Qt::UserRole).toInt()));
    }
    database_->setSetting(expandedFoldersKey(current_.id), expanded.join(','));
}

void ProjectsPage::filterRecords()
{
    const QSignalBlocker blocker(workList_);
    const QString query = search_->text();
    auto *orderedRecords = static_cast<SiblingOrderTree *>(workList_);
    orderedRecords->orderingEnabled = query.isEmpty();
    orderedRecords->setDragEnabled(query.isEmpty());
    orderedRecords->transferEnabled = query.isEmpty();
    const auto expanded = database_->setting(expandedFoldersKey(current_.id)).split(',');
    bool anyVisible = false;
    for (int i=0; i<workList_->topLevelItemCount(); ++i) {
        auto *item = workList_->topLevelItem(i);
        const bool nameMatches = item->text(0).contains(query,Qt::CaseInsensitive);
        bool visible = nameMatches || (!item->data(0,FolderRole).toBool()
            && item->data(0,SummaryRole).toString().contains(query,Qt::CaseInsensitive));
        if (item->data(0,FolderRole).toBool()) {
            for (int j=0; j<item->childCount(); ++j) {
                auto *child = item->child(j);
                const bool match = nameMatches || child->text(0).contains(query,Qt::CaseInsensitive)
                    || child->data(0,SummaryRole).toString().contains(query,Qt::CaseInsensitive);
                child->setHidden(!match);
                visible |= match;
            }
            item->setExpanded(query.isEmpty() ? expanded.contains(QString::number(item->data(0,Qt::UserRole).toInt())) : visible);
        }
        item->setHidden(!visible);
        anyVisible |= visible;
    }
    recordHint_->setText(!query.isEmpty() ? (anyVisible ? QStringLiteral("清除搜索后可拖动调整顺序。") : QStringLiteral("没有匹配的协作记录或文件夹。"))
        : QStringLiteral("暂无协作记录。添加任务说明或提示词，并用文件夹按用途整理。"));
    recordHint_->setVisible((!anyVisible || !query.isEmpty()) && current_.id > 0);
    // Filtering changes visible rows with tree signals blocked, including when
    // the visible row count stays the same. Refresh their accessible mapping.
    workList_->doItemsLayout();
    resetTreeAccessibility(workList_);
    updateRecordActions();
}

void ProjectsPage::updateRecordActions()
{
    const auto *item = workList_->currentItem();
    const bool selected = item && !item->data(0,FolderRole).toBool() && !item->isHidden()
        && (!item->parent() || (!item->parent()->isHidden() && item->parent()->isExpanded()));
    editWorkButton_->setEnabled(selected);
    deleteWorkButton_->setEnabled(selected);
}

int ProjectsPage::selectedFolderId() const
{
    const auto *item = workList_->currentItem();
    if (!item || item->isHidden() || (item->parent() && item->parent()->isHidden())) return 0;
    if (item->data(0,FolderRole).toBool()) return item->data(0,Qt::UserRole).toInt();
    return item->parent() ? item->parent()->data(0,Qt::UserRole).toInt() : 0;
}

AppDatabase::WorkRecord ProjectsPage::currentWorkRecord() const
{
    auto *item=workList_->currentItem();
    if(!item || item->data(0,FolderRole).toBool() || item->isHidden()
       || (item->parent() && (item->parent()->isHidden() || !item->parent()->isExpanded()))) return {};
    for(const auto &record:database_->workRecords(current_.id))
        if(record.id==item->data(0,Qt::UserRole).toInt()) return record;
    return {};
}
void ProjectsPage::folderDialog(bool creating)
{
    auto *item = workList_->currentItem();
    if (current_.id <= 0 || (!creating && (!item || !item->data(0,FolderRole).toBool()))) return;
    const int folderId = creating ? 0 : item->data(0,Qt::UserRole).toInt();
    QDialog dialog(this);
    dialog.setWindowTitle(creating ? QStringLiteral("新建文件夹") : QStringLiteral("重命名文件夹"));
    dialog.resize(420,170);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *name = new QLineEdit(creating ? QString() : item->text(0), &dialog);
    name->setObjectName(QStringLiteral("recordFolderName"));
    name->setPlaceholderText(QStringLiteral("例如：初始化工作区"));
    form->addRow(QStringLiteral("文件夹名称"),name);
    layout->addLayout(form);
    auto *errorLabel = label(QString(),QStringLiteral("folderError"),&dialog);
    errorLabel->setWordWrap(true);
    UiTheme::setRole(errorLabel, "error");

    errorLabel->hide();
    layout->addWidget(errorLabel);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,&dialog);
    auto *save = buttons->button(QDialogButtonBox::Save);
    save->setText(creating ? QStringLiteral("新建") : QStringLiteral("保存"));
    save->setEnabled(!name->text().trimmed().isEmpty());
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(name,&QLineEdit::textChanged,&dialog,[=]{ save->setEnabled(!name->text().trimmed().isEmpty()); errorLabel->hide(); });
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        QString error;
        const auto folders = database_->recordFolders(current_.id,&error);
        for (const auto &folder : folders) {
            if (folder.id != folderId && folder.name == name->text().trimmed()) {
                error = QStringLiteral("当前项目已有同名文件夹，请换一个名称。");
                break;
            }
        }
        int id = folderId;
        if (error.isEmpty()) {
            if (creating) database_->addRecordFolder(current_.id,name->text(),&id,&error);
            else database_->renameRecordFolder(id,name->text(),&error);
        }
        if (!error.isEmpty()) { errorLabel->setText(error); errorLabel->show(); return; }
        dialog.accept();
        {
            const QSignalBlocker blocker(search_);
            search_->clear();
        }
        refreshWorkRecords();
        for (int i=0; i<workList_->topLevelItemCount(); ++i) {
            auto *folder = workList_->topLevelItem(i);
            if (folder->data(0,FolderRole).toBool() && folder->data(0,Qt::UserRole).toInt() == id) {
                folder->setExpanded(true);
                workList_->setCurrentItem(folder);
                workList_->scrollToItem(folder);
                break;
            }
        }
    });
    name->selectAll();
    name->setFocus();
    dialog.exec();
}

void ProjectsPage::deleteFolder()
{
    auto *item = workList_->currentItem();
    if (!item || !item->data(0,FolderRole).toBool()) return;
    const int id = item->data(0,Qt::UserRole).toInt();
    if (QMessageBox::question(this,QStringLiteral("删除文件夹"),
        QStringLiteral("删除文件夹“%1”？其中的协作记录会保留，并移回项目顶层。").arg(item->text(0)),
        QMessageBox::Yes | QMessageBox::No,QMessageBox::No) != QMessageBox::Yes) return;
    QString error;
    if (!database_->deleteRecordFolder(id,&error)) { showError(error); return; }
    refreshWorkRecords();
    saveExpandedFolders();
}

void ProjectsPage::showRecordContextMenu(const QPoint &position)
{
    if (current_.id <= 0) return;
    auto *item = workList_->itemAt(position);
    workList_->setCurrentItem(item);
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("recordContextMenu"));
    if (item && !item->data(0,FolderRole).toBool()) {
        menu.addAction(QStringLiteral("打开记录"),this,[this]{ recordDialog(false); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("删除记录…"),this,&ProjectsPage::deleteWorkRecord);
    } else {
        menu.addAction(item ? QStringLiteral("在此文件夹添加记录") : QStringLiteral("添加记录"),this,[this]{ recordDialog(true); });
        menu.addAction(QStringLiteral("新建文件夹"),this,[this]{ folderDialog(true); });
        if (item) {
            menu.addSeparator();
            const int folderId = item->data(0,Qt::UserRole).toInt();
            const bool starred = item->data(0,StarredRole).toBool();
            auto *star = menu.addAction(QIcon(QStringLiteral(":/icons/star.svg")),
                starred ? QStringLiteral("取消重点标记") : QStringLiteral("标记重点并置顶"), this, [this,folderId,starred] {
                    QString error;
                    if (!database_->setRecordFolderStarred(folderId,!starred,&error)) { showError(error); return; }
                    refreshWorkRecords();
                    if (workList_->currentItem()) workList_->scrollToItem(workList_->currentItem());
                });
            star->setObjectName(QStringLiteral("toggleRecordFolderStar"));
            menu.addAction(QStringLiteral("重命名文件夹"),this,[this]{ folderDialog(false); });
            menu.addAction(QStringLiteral("删除文件夹…"),this,&ProjectsPage::deleteFolder);
        }
    }
    menu.exec(workList_->viewport()->mapToGlobal(position));
}

void ProjectsPage::recordDialog(bool creating)
{
    auto record=currentWorkRecord();
    if(current_.id<=0 || (!creating && record.id<=0)) return;
    const int projectId=current_.id;
    const int folderId=creating ? selectedFolderId() : record.folderId;
    if (creating) {
        record = {};
        record.projectId = projectId;
        record.folderId = folderId;
    }
    QString location = QStringLiteral("项目顶层");
    for (const auto &folder : database_->recordFolders(projectId))
        if (folder.id == folderId) location = folder.name;
    auto *workspace=ContentWorkspace::find(this);
    const QString prefix=QStringLiteral("project/%1/record/").arg(projectId);
    const QString key=prefix+(creating ? QStringLiteral("new/%1").arg(folderId) : QString::number(record.id));
    if (workspace && workspace->activate(key,this)) return;
    auto *dialog=new CollaborationRecordDialog(database_, record, location, this);
    connect(dialog,&CollaborationRecordDialog::savedRecord,this,[this,dialog,workspace,prefix,projectId,folderId](int id) {
        if (workspace) workspace->rekey(dialog->property("contentKey").toString(),prefix+QString::number(id));
        if (folderId > 0) {
            auto expanded = database_->setting(expandedFoldersKey(projectId)).split(',',Qt::SkipEmptyParts);
            if (!expanded.contains(QString::number(folderId))) expanded.append(QString::number(folderId));
            database_->setSetting(expandedFoldersKey(projectId),expanded.join(','));
        }
        if (current_.id==projectId) refreshWorkRecords();
    });
    if (workspace) workspace->present(key,dialog,this);
    else { dialog->exec(); delete dialog; }
}
void ProjectsPage::deleteWorkRecord()
{
    const auto record=currentWorkRecord();
    if(record.id<=0) return;
    if(QMessageBox::question(this,QStringLiteral("删除协作记录"),QStringLiteral("确定删除这条协作记录吗？"),
                             QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return;
    if (auto *workspace=ContentWorkspace::find(this))
        if (!workspace->closePage(QStringLiteral("project/%1/record/%2").arg(record.projectId).arg(record.id))) return;
    QString error;
    if(!database_->deleteWorkRecord(record.id,&error)) { showError(error); return; }
    refreshWorkRecords();
}
bool ProjectsPage::eventFilter(QObject *watched, QEvent *event)
{
    // Space is native QPushButton behavior; retain the existing Enter shortcut.
    if (watched == gitHistoryButton_ && gitHistoryButton_->isEnabled() && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            gitHistoryButton_->click();
            return true;
        }
    }
    if (watched == workList_ && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            auto *item = workList_->currentItem();
            if (item && item->data(0,FolderRole).toBool()) item->setExpanded(!item->isExpanded());
            else recordDialog(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ProjectsPage::openGitHistory()
{
    if (current_.id <= 0) return;
    if (current_.directory.isEmpty()) { projectDialog(false); if(current_.id<=0 || current_.directory.isEmpty())return; }
    auto *workspace=ContentWorkspace::find(this);
    const QString key=QStringLiteral("project/%1/git").arg(current_.id);
    if (workspace && workspace->activate(key,this)) return;
    auto &window=gitWindows_[current_.id];
    if (!window) {
        window=new GitHistoryWindow(current_.name,current_.repository(),this);
        connect(window,&QDialog::finished,this,[this,id=current_.id]{ gitWindows_.remove(id); });
    }
    if (workspace) { workspace->present(key,window,this); return; }
    if (window->isMinimized()) window->showNormal(); else window->show();
    window->raise(); window->activateWindow();
}
void ProjectsPage::showError(const QString &message)
{
    QMessageBox::critical(this,QStringLiteral("数据操作失败"),message);
}
