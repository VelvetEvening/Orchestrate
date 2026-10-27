#include "projectspage.h"
#include "widgets/persistenttextzoom.h"
#include "widgets/headingtextedit.h"
#include "widgets/summaryfield.h"
#include <QDesktopServices>
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
QString projectStatus(bool completed) {
    return completed ? QStringLiteral("已完结") : QStringLiteral("进行中");
}
QString expandedFoldersKey(int projectId) {
    return QStringLiteral("projects.%1.expanded_record_folders").arg(projectId);
}
QFrame *card(QWidget *parent) {
    auto *widget = new QFrame(parent);
    widget->setObjectName(QStringLiteral("card"));
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
    list->setStyleSheet(QStringLiteral(
        "QListWidget { border: none; background: transparent; outline: none; padding: 0; }"
        "QListWidget::item { padding: 10px 12px; border-radius: 7px; color: #334155; }"
        "QListWidget::item:hover { background: #f1f5fb; }"
        "QListWidget::item:selected { background: #e0ebff; color: #1d4ed8; }"));
}
}

ProjectsPage::ProjectsPage(AppDatabase *database, QWidget *parent) : QWidget(parent), database_(database)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(34, 30, 34, 34);
    layout->setSpacing(18);
    layout->addWidget(label(QStringLiteral("项目记录"), QStringLiteral("pageTitle"), this));
    auto *splitter = new QSplitter(this);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(14);
    auto *listCard = card(splitter);
    auto *left = new QVBoxLayout(listCard);
    left->setContentsMargins(18,18,18,18);
    left->addWidget(label(QStringLiteral("项目列表"), QStringLiteral("cardTitle"), listCard));
    projectList_ = new QListWidget(listCard);
    projectList_->setObjectName(QStringLiteral("projectList"));
    projectList_->setContextMenuPolicy(Qt::CustomContextMenu);
    projectList_->setAccessibleDescription(QStringLiteral("右键项目可切换进行中或已完结状态；双击打开项目目录"));
    styleList(projectList_);
    left->addWidget(projectList_, 1);
    auto *create = new QPushButton(QStringLiteral("新建项目"), listCard);
    create->setObjectName(QStringLiteral("primaryButton"));
    left->addWidget(create);
    details_ = card(splitter);
    detailsLayout_ = new QVBoxLayout(details_);
    detailsLayout_->setContentsMargins(22,18,22,18);
    detailsLayout_->setSpacing(12);
    auto *header = new QHBoxLayout;
    projectTitle_ = label(QString(), QStringLiteral("cardTitle"), details_);
    projectTitle_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header->addWidget(projectTitle_,1);
    outlineTitle_ = label(QStringLiteral("项目大纲"), QStringLiteral("outlineTitle"), details_);
    outlineTitle_->setCursor(Qt::PointingHandCursor);
    outlineTitle_->setFocusPolicy(Qt::StrongFocus);
    outlineTitle_->setToolTip(QStringLiteral("双击打开项目大纲"));
    outlineTitle_->setAccessibleDescription(QStringLiteral("双击或按回车打开项目大纲"));
    outlineTitle_->setStyleSheet(QStringLiteral(
        "QLabel#outlineTitle { background: #eaf2ff; color: #2456a6; font-size: 14px; font-weight: 600; padding: 8px 14px; border: 1px solid #a9c5eb; border-radius: 6px; }"
        "QLabel#outlineTitle:hover { background: #dceaff; border-color: #6c9cda; }"
        "QLabel#outlineTitle:focus { background: #dceaff; border-color: #3474c9; }"
        "QLabel#outlineTitle:disabled { background: #f1f3f6; color: #98a2b1; border-color: #dce2ea; }"));
    outlineTitle_->installEventFilter(this);
    header->addWidget(outlineTitle_);
    auto *edit = new QPushButton(QStringLiteral("项目设置"), details_);
    header->addWidget(edit);
    detailsLayout_->addLayout(header);

    outlineWindow_ = new QDialog(this, Qt::Window);
    outlineWindow_->setObjectName(QStringLiteral("outlineWindow"));
    outlineWindow_->setWindowFlag(Qt::WindowMaximizeButtonHint);
    outlineWindow_->resize(940, 720);
    outlineWindow_->setMinimumSize(560, 400);
    auto *outlineLayout = new QVBoxLayout(outlineWindow_);
    outlineLayout->setContentsMargins(24,24,24,24);
    outlineEdit_ = new HeadingTextEdit(outlineWindow_);
    bindPersistentTextZoom(outlineEdit_, database_, QStringLiteral("text_zoom.outline"));
    outlineEdit_->setObjectName(QStringLiteral("projectOutline"));
    outlineEdit_->setMinimumHeight(150);
    outlineLayout->addWidget(outlineEdit_, 1);
    saveStatus_ = label(QString(), QStringLiteral("outlineSaveError"), outlineWindow_);
    saveStatus_->setWordWrap(true);
    saveStatus_->hide();
    outlineLayout->addWidget(saveStatus_);
    auto *recordsPanel_ = new QWidget(details_);
    recordsPanel_->setObjectName(QStringLiteral("recordsPanel"));
    auto *records = new QVBoxLayout(recordsPanel_);
    records->setContentsMargins(0,0,0,0);
    auto *recordHeader = new QHBoxLayout;
    recordHeader->addWidget(label(QStringLiteral("协作记录"), QStringLiteral("cardTitle"), recordsPanel_),1);
    auto *createFolder = new QPushButton(QStringLiteral("新建文件夹"), recordsPanel_);
    createFolder->setObjectName(QStringLiteral("createRecordFolder"));
    recordHeader->addWidget(createFolder);
    records->addLayout(recordHeader);
    search_ = new QLineEdit(recordsPanel_);
    search_->setObjectName(QStringLiteral("recordSearch"));
    search_->setPlaceholderText(QStringLiteral("搜索简介或文件夹"));
    search_->setAccessibleName(QStringLiteral("搜索协作记录"));
    search_->setClearButtonEnabled(true);
    records->addWidget(search_);
    workList_ = new QTreeWidget(recordsPanel_);
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
    workList_->setToolTip(QStringLiteral("右键记录可移动到文件夹；右键文件夹可重命名或删除"));
    workList_->setStyleSheet(QStringLiteral(
        "QTreeWidget { border: none; background: transparent; padding: 0; selection-background-color: #e0ebff; selection-color: #1d4ed8; }"
        "QTreeWidget::item { padding: 8px 10px; color: #334155; }"
        "QTreeWidget::item:hover { background: #f1f5fb; }"
        "QTreeWidget::item:selected { background: #e0ebff; color: #1d4ed8; }"));
    workList_->installEventFilter(this);
    workList_->setMinimumHeight(120);
    records->addWidget(workList_,1);
    recordHint_ = label(QString(), QStringLiteral("recordHint"), recordsPanel_);
    recordHint_->setWordWrap(true);
    recordHint_->setStyleSheet(QStringLiteral("color: #52627a;"));
    records->addWidget(recordHint_);
    auto *actions = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("添加记录"), recordsPanel_);
    add->setObjectName(QStringLiteral("primaryButton"));
    editWorkButton_ = new QPushButton(QStringLiteral("编辑"), recordsPanel_);
    deleteWorkButton_ = new QPushButton(QStringLiteral("删除"), recordsPanel_);
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
    connect(edit, &QPushButton::clicked, this, [this]{ projectDialog(false); });
    connect(projectList_, &QListWidget::customContextMenuRequested, this, &ProjectsPage::showProjectContextMenu);
    connect(projectList_, &QListWidget::currentRowChanged, this, [this](int row){
        if (row >= 0) selectProject(projectList_->item(row)->data(Qt::UserRole).toInt());
    });
    connect(projectList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item) openProjectDirectory(item->data(Qt::UserRole + 1).toString());
    });
    connect(outlineEdit_, &HeadingTextEdit::textChanged, this, [this]{ saveOutline(); });
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
        auto *item = new QListWidgetItem(project.name + QStringLiteral("\n") + projectStatus(project.completed), projectList_);
        item->setSizeHint(QSize(0,64));
        item->setData(Qt::UserRole,project.id);
        item->setData(Qt::UserRole + 1, project.directory);
        item->setData(Qt::UserRole + 2, project.completed);
        item->setData(Qt::UserRole + 3, project.name);
        item->setToolTip(project.name + QStringLiteral("\n") + projectStatus(project.completed)
            + QStringLiteral(" · 右键修改状态")
            + (project.directory.isEmpty() ? QString() : QStringLiteral("\n双击打开目录：") + QDir::toNativeSeparators(project.directory)));
        if (project.id == preferred) row = projectList_->count()-1;
    }
    if (projects.isEmpty()) {
        current_ = {};
        projectTitle_->setText(QStringLiteral("暂无项目"));
        details_->setEnabled(false);
        const QSignalBlocker outlineBlock(outlineEdit_);
        outlineEdit_->clear();
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
            const QString name = item->data(Qt::UserRole + 3).toString();
            const QString directory = item->data(Qt::UserRole + 1).toString();
            item->setData(Qt::UserRole + 2, value);
            item->setText(name + QStringLiteral("\n") + projectStatus(value));
            item->setToolTip(name + QStringLiteral("\n") + projectStatus(value) + QStringLiteral(" · 右键修改状态")
                + (directory.isEmpty() ? QString() : QStringLiteral("\n双击打开目录：") + QDir::toNativeSeparators(directory)));
            if (current_.id == id) current_.completed = value;
        });
    }
    menu.exec(projectList_->viewport()->mapToGlobal(position));
}

void ProjectsPage::openProjectDirectory(const QString &directory)
{
    if (directory.isEmpty()) return;
    const QFileInfo folder(directory);
    if (!folder.isDir()) {
        QMessageBox::warning(this, QStringLiteral("无法打开项目目录"),
            QStringLiteral("目录不存在、无法访问或不是文件夹，请在“项目设置”中更新目录。\n\n%1")
                .arg(QDir::toNativeSeparators(directory)));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder.absoluteFilePath()))) {
        QMessageBox::warning(this, QStringLiteral("无法打开项目目录"),
            QStringLiteral("系统未能打开项目目录。\n\n%1")
                .arg(QDir::toNativeSeparators(folder.absoluteFilePath())));
    }
}

bool ProjectsPage::saveOutline()
{
    if (current_.id <= 0 || current_.outline == outlineEdit_->toPlainText()) return true;
    QString error;
    const QString text = outlineEdit_->toPlainText();
    if (!database_->updateProject(current_.id, current_.name, current_.directory, text, &error)) {
        saveStatus_->setText(QStringLiteral("大纲保存失败：") + error);
        saveStatus_->show();
        return false;
    }
    current_.outline = text;
    saveStatus_->hide();
    return true;
}

bool ProjectsPage::savePendingChanges()
{
    if (saveOutline()) return true;
    openOutline();
    QMessageBox::warning(outlineWindow_, QStringLiteral("暂时无法退出"),
                         QStringLiteral("项目大纲尚未保存，已取消退出。请保留当前窗口，恢复数据库后重试，或先复制大纲备份。\n\n")
                             + saveStatus_->text());
    return false;
}

void ProjectsPage::selectProject(int id)
{
    if (!saveOutline()) {
        const QSignalBlocker blocker(projectList_);
        for(int i=0;i<projectList_->count();++i)
            if(projectList_->item(i)->data(Qt::UserRole).toInt()==current_.id) projectList_->setCurrentRow(i);
        return;
    }
    QString error;
    for (const auto &project : database_->projects(&error)) {
        if (project.id != id) continue;
        current_ = project;
        projectTitle_->setText(project.name);
        projectTitle_->setToolTip(project.name);
        const QSignalBlocker blocker(outlineEdit_);
        outlineEdit_->setPlainText(project.outline);
        outlineEdit_->setPreviewMode(true);
        database_->setSetting(QStringLiteral("projects.last_selected"),QString::number(id));
        const QSignalBlocker searchBlock(search_);
        search_->clear();
        refreshWorkRecords();
        if (outlineWindow_) outlineWindow_->setWindowTitle(project.name + QStringLiteral(" · 项目大纲"));
        return;
    }
    if (!error.isEmpty()) showError(error);
}

void ProjectsPage::projectDialog(bool creating)
{
    if (!saveOutline() || (!creating && current_.id <= 0)) return;
    QDialog dialog(this);
    dialog.setWindowTitle(creating ? QStringLiteral("新建项目") : QStringLiteral("项目设置"));
    dialog.resize(520,220);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *name = new QLineEdit(&dialog);
    name->setObjectName(QStringLiteral("projectName"));
    auto *directory = new QLineEdit(&dialog);
    directory->setObjectName(QStringLiteral("projectDirectory"));
    auto *directoryRow = new QHBoxLayout;
    auto *browse = new QPushButton(QStringLiteral("选择"),&dialog);
    directoryRow->addWidget(directory,1);
    directoryRow->addWidget(browse);
    form->addRow(QStringLiteral("项目名称"),name);
    form->addRow(QStringLiteral("项目目录（可选）"),directoryRow);
    layout->addLayout(form);
    if (!creating) { name->setText(current_.name); directory->setText(current_.directory); }
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
        remove->setStyleSheet(QStringLiteral(
            "QPushButton#deleteProjectButton { color: #b42318; background: transparent; border: 1px solid transparent; padding: 7px 8px; }"
            "QPushButton#deleteProjectButton:hover { color: #912018; background: #fff1f0; }"
            "QPushButton#deleteProjectButton:pressed { color: #7a271a; background: #fee4e2; }"
            "QPushButton#deleteProjectButton:focus { border-color: #fda29b; }"));
        footer->addWidget(remove);
        connect(remove, &QPushButton::clicked, &dialog, [this, &dialog] {
            if (deleteProject(&dialog)) dialog.reject();
        });
    }
    footer->addStretch();
    footer->addWidget(buttons);
    layout->addLayout(footer);
    connect(name,&QLineEdit::textChanged,&dialog,[name,save]{ save->setEnabled(!name->text().trimmed().isEmpty()); });
    connect(browse,&QPushButton::clicked,&dialog,[&dialog,directory] {
        const QString selected=QFileDialog::getExistingDirectory(&dialog,QStringLiteral("选择项目目录"),directory->text());
        if (!selected.isEmpty()) directory->setText(selected);
    });
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        QString error;
        int id=current_.id;
        const bool ok=creating
            ? database_->addProject(name->text().trimmed(),directory->text().trimmed(),QStringLiteral(""),&id,&error)
            : database_->updateProject(id,name->text().trimmed(),directory->text().trimmed(),current_.outline,&error);
        if (!ok) { QMessageBox::critical(&dialog,QStringLiteral("保存失败"),error); return; }
        dialog.accept();
        loadProjects(id);
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
    auto *message = label(QStringLiteral("将删除项目“%1”、项目大纲、全部协作记录及文件夹，此操作无法撤销。项目目录中的文件不会删除。\n\n如果确定要删除，请完整输入：\n%2")
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
        QString error;
        if (!database_->deleteProject(projectId, &error)) {
            QMessageBox::critical(&dialog, QStringLiteral("删除失败"), error);
            return;
        }
        if (outlineWindow_) outlineWindow_->close();
        current_ = {};
        { const QSignalBlocker blocker(outlineEdit_); outlineEdit_->clear(); }
        saveStatus_->hide();
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
    QIcon folderIcon = style()->standardIcon(QStyle::SP_DirClosedIcon);
    folderIcon.addPixmap(style()->standardPixmap(QStyle::SP_DirOpenIcon), QIcon::Normal, QIcon::On);
    for (const auto &folder : folders) {
        auto *item = new QTreeWidgetItem(workList_, {folder.name});
        item->setData(0,Qt::UserRole,folder.id);
        item->setData(0,FolderRole,true);
        item->setIcon(0,folderIcon);
        item->setSizeHint(0,QSize(0,44));
        item->setToolTip(0,folder.name + QStringLiteral("\n右键重命名或删除文件夹"));
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
    const auto expanded = database_->setting(expandedFoldersKey(current_.id)).split(',');
    bool anyVisible = false;
    for (int i=0; i<workList_->topLevelItemCount(); ++i) {
        auto *item = workList_->topLevelItem(i);
        const bool nameMatches = item->text(0).contains(query,Qt::CaseInsensitive);
        bool visible = nameMatches;
        if (item->data(0,FolderRole).toBool()) {
            for (int j=0; j<item->childCount(); ++j) {
                auto *child = item->child(j);
                const bool match = nameMatches || child->text(0).contains(query,Qt::CaseInsensitive);
                child->setHidden(!match);
                visible |= match;
            }
            item->setExpanded(query.isEmpty() ? expanded.contains(QString::number(item->data(0,Qt::UserRole).toInt())) : visible);
        }
        item->setHidden(!visible);
        anyVisible |= visible;
    }
    recordHint_->setText(!query.isEmpty() ? QStringLiteral("没有匹配的协作记录或文件夹。")
        : QStringLiteral("暂无协作记录。添加任务说明或提示词，并用文件夹按用途整理。"));
    recordHint_->setVisible(!anyVisible && current_.id > 0);
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
    errorLabel->setStyleSheet(QStringLiteral("color: #b42318;"));
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
        auto *move = menu.addMenu(QStringLiteral("移动到文件夹"));
        const auto record = currentWorkRecord();
        QString error;
        const auto folders = database_->recordFolders(current_.id,&error);
        if (!error.isEmpty()) { showError(error); return; }
        auto addDestination = [&](const QString &name, int folderId) {
            auto *action = move->addAction(name);
            action->setCheckable(true);
            action->setChecked(record.folderId == folderId);
            action->setEnabled(record.folderId != folderId);
            connect(action,&QAction::triggered,this,[this,record,folderId]{
                QString error;
                if (!database_->setWorkRecordFolder(record.id,folderId,&error)) { showError(error); return; }
                {
                    const QSignalBlocker blocker(search_);
                    search_->clear();
                }
                if (folderId > 0) {
                    auto expanded = database_->setting(expandedFoldersKey(current_.id)).split(',',Qt::SkipEmptyParts);
                    if (!expanded.contains(QString::number(folderId))) expanded.append(QString::number(folderId));
                    database_->setSetting(expandedFoldersKey(current_.id),expanded.join(','));
                }
                refreshWorkRecords();
                if (workList_->currentItem()) workList_->scrollToItem(workList_->currentItem());
            });
        };
        addDestination(QStringLiteral("项目顶层"),0);
        if (!folders.isEmpty()) move->addSeparator();
        for (const auto &folder : folders) addDestination(QString(folder.name).replace('&',QStringLiteral("&&")),folder.id);
        menu.addSeparator();
        menu.addAction(QStringLiteral("删除记录…"),this,&ProjectsPage::deleteWorkRecord);
    } else {
        menu.addAction(item ? QStringLiteral("在此文件夹添加记录") : QStringLiteral("添加记录"),this,[this]{ recordDialog(true); });
        menu.addAction(QStringLiteral("新建文件夹"),this,[this]{ folderDialog(true); });
        if (item) {
            menu.addSeparator();
            menu.addAction(QStringLiteral("重命名文件夹"),this,[this]{ folderDialog(false); });
            menu.addAction(QStringLiteral("删除文件夹…"),this,&ProjectsPage::deleteFolder);
        }
    }
    menu.exec(workList_->viewport()->mapToGlobal(position));
}

void ProjectsPage::recordDialog(bool creating)
{
    const auto record=currentWorkRecord();
    if(current_.id<=0 || (!creating && record.id<=0)) return;
    const int projectId=current_.id;
    const int folderId=creating ? selectedFolderId() : record.folderId;
    QDialog dialog(this);
    dialog.setWindowTitle(creating ? QStringLiteral("添加协作记录") : QStringLiteral("编辑协作记录"));
    dialog.setWindowFlag(Qt::WindowMaximizeButtonHint);
    dialog.resize(800,620);
    dialog.setMinimumSize(480,360);
    auto *layout=new QVBoxLayout(&dialog);
    QString location = QStringLiteral("项目顶层");
    for (const auto &folder : database_->recordFolders(projectId))
        if (folder.id == folderId) location = folder.name;
    auto *locationLabel = label(QStringLiteral("位置：%1").arg(location),QStringLiteral("recordLocation"),&dialog);
    locationLabel->setWordWrap(true);
    layout->addWidget(locationLabel);
    auto *title=addSummaryField(layout,&dialog,QStringLiteral("workRecordTitle"));
    auto *editor=new HeadingTextEdit(&dialog);
    bindPersistentTextZoom(editor, database_, QStringLiteral("text_zoom.work_record"));
    layout->addWidget(editor,1);
    if(!creating) { title->setText(recordTitle(record)); editor->setPlainText(record.content); }
    connect(editor,&HeadingTextEdit::previewModeChanged,title,&QLineEdit::setReadOnly);
    editor->setPreviewMode(!creating);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);
    auto *save=buttons->button(QDialogButtonBox::Save);
    save->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    auto validate=[=]{
        const bool changed=title->text().trimmed()!=recordTitle(record) || editor->toPlainText()!=record.content;
        save->setEnabled(!title->text().trimmed().isEmpty() && !editor->toPlainText().trimmed().isEmpty()
                         && (creating || changed));
    };
    connect(title,&QLineEdit::textChanged,&dialog,validate);
    connect(editor,&HeadingTextEdit::textChanged,&dialog,validate);
    validate();
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        QString error;
        const bool ok=creating
            ? database_->addWorkRecord(projectId,title->text().trimmed(),editor->toPlainText(),folderId,&error)
            : database_->updateWorkRecord(record.id,title->text().trimmed(),editor->toPlainText(),&error);
        if(!ok) { QMessageBox::critical(&dialog,QStringLiteral("保存失败"),error); return; }
        dialog.accept();
        if (creating) {
            if (folderId > 0) {
                auto expanded = database_->setting(expandedFoldersKey(projectId)).split(',',Qt::SkipEmptyParts);
                if (!expanded.contains(QString::number(folderId))) expanded.append(QString::number(folderId));
                database_->setSetting(expandedFoldersKey(projectId),expanded.join(','));
            }
            const QSignalBlocker blocker(search_);
            search_->clear();
        }
        refreshWorkRecords();
    });
    if (creating) title->setFocus();
    else editor->setFocus();
    dialog.exec();
}
void ProjectsPage::deleteWorkRecord()
{
    const auto record=currentWorkRecord();
    if(record.id<=0) return;
    if(QMessageBox::question(this,QStringLiteral("删除协作记录"),QStringLiteral("确定删除这条协作记录吗？"),
                             QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return;
    QString error;
    if(!database_->deleteWorkRecord(record.id,&error)) { showError(error); return; }
    refreshWorkRecords();
}
bool ProjectsPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == workList_ && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            auto *item = workList_->currentItem();
            if (item && item->data(0,FolderRole).toBool()) item->setExpanded(!item->isExpanded());
            else recordDialog(false);
            return true;
        }
    }
    if (watched == outlineTitle_ && outlineTitle_->isEnabled()) {
        if (event->type() == QEvent::MouseButtonDblClick &&
            static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            openOutline();
            return true;
        }
        if (event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent *>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) {
                openOutline();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ProjectsPage::openOutline()
{
    if (current_.id <= 0) return;
    if (!outlineWindow_->isVisible()) outlineEdit_->setPreviewMode(true);
    if (outlineWindow_->isMinimized()) outlineWindow_->showNormal();
    else outlineWindow_->show();
    outlineWindow_->raise();
    outlineWindow_->activateWindow();
}
void ProjectsPage::showError(const QString &message)
{
    QMessageBox::critical(this,QStringLiteral("数据操作失败"),message);
}
