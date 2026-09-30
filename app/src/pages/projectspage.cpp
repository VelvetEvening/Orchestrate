#include "projectspage.h"
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
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>
#include <QUrl>

namespace {
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
    recordHeader->addWidget(label(QStringLiteral("工作记录"), QStringLiteral("cardTitle"), recordsPanel_),1);
    records->addLayout(recordHeader);
    search_ = new QLineEdit(recordsPanel_);
    search_->setObjectName(QStringLiteral("recordSearch"));
    search_->setPlaceholderText(QStringLiteral("搜索简介"));
    search_->setClearButtonEnabled(true);
    records->addWidget(search_);
    workList_ = new QListWidget(recordsPanel_);
    workList_->setObjectName(QStringLiteral("workRecordList"));
    styleList(workList_);
    workList_->setMinimumHeight(120);
    records->addWidget(workList_,1);
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
    connect(projectList_, &QListWidget::currentRowChanged, this, [this](int row){
        if (row >= 0) selectProject(projectList_->item(row)->data(Qt::UserRole).toInt());
    });
    connect(projectList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item) openProjectDirectory(item->data(Qt::UserRole + 1).toString());
    });
    connect(outlineEdit_, &HeadingTextEdit::textChanged, this, [this]{ saveOutline(); });
    connect(add, &QPushButton::clicked, this, [this]{ recordDialog(true); });
    connect(editWorkButton_, &QPushButton::clicked, this, [this]{ recordDialog(false); });
    connect(deleteWorkButton_, &QPushButton::clicked, this, &ProjectsPage::deleteWorkRecord);
    connect(workList_, &QListWidget::itemDoubleClicked, this, [this]{ recordDialog(false); });
    connect(workList_, &QListWidget::itemSelectionChanged, this, [this] {
        const bool selected = workList_->currentItem() && !workList_->currentItem()->isHidden();
        editWorkButton_->setEnabled(selected);
        deleteWorkButton_->setEnabled(selected);
    });
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
        item->setSizeHint(QSize(0,46));
        item->setData(Qt::UserRole,project.id);
        item->setData(Qt::UserRole + 1, project.directory);
        item->setToolTip(project.directory.isEmpty() ? project.name
            : project.name + QStringLiteral("\n双击打开目录：") + QDir::toNativeSeparators(project.directory));
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
    auto *message = label(QStringLiteral("将删除项目“%1”、项目大纲及全部工作记录，此操作无法撤销。项目目录中的文件不会删除。\n\n如果确定要删除，请完整输入：\n%2")
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
    int selected=workList_->currentItem() ? workList_->currentItem()->data(Qt::UserRole).toInt() : 0;
    workList_->clear();
    editWorkButton_->setEnabled(false);
    deleteWorkButton_->setEnabled(false);
    if (current_.id<=0) return;
    QString error;
    for (const auto &record : database_->workRecords(current_.id,&error)) {
        auto *item=new QListWidgetItem(recordTitle(record),workList_);
        item->setSizeHint(QSize(0,46));
        item->setData(Qt::UserRole,record.id);
        item->setToolTip(recordTitle(record)+QStringLiteral("\n")+record.createdAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        if(record.id==selected) workList_->setCurrentItem(item);
    }
    if (!error.isEmpty()) showError(error);
    filterRecords();
}
void ProjectsPage::filterRecords()
{
    for(int i=0;i<workList_->count();++i) {
        auto *item=workList_->item(i);
        item->setHidden(!item->text().contains(search_->text(),Qt::CaseInsensitive));
    }
    const bool selected=workList_->currentItem() && !workList_->currentItem()->isHidden();
    editWorkButton_->setEnabled(selected);
    deleteWorkButton_->setEnabled(selected);
}
AppDatabase::WorkRecord ProjectsPage::currentWorkRecord() const
{
    auto *item=workList_->currentItem();
    if(!item || item->isHidden()) return {};
    for(const auto &record:database_->workRecords(current_.id))
        if(record.id==item->data(Qt::UserRole).toInt()) return record;
    return {};
}
void ProjectsPage::recordDialog(bool creating)
{
    const auto record=currentWorkRecord();
    if(current_.id<=0 || (!creating && record.id<=0)) return;
    const int projectId=current_.id;
    QDialog dialog(this);
    dialog.setWindowTitle(creating ? QStringLiteral("添加工作记录") : QStringLiteral("编辑工作记录"));
    dialog.setWindowFlag(Qt::WindowMaximizeButtonHint);
    dialog.resize(800,620);
    dialog.setMinimumSize(480,360);
    auto *layout=new QVBoxLayout(&dialog);
    auto *title=addSummaryField(layout,&dialog,QStringLiteral("workRecordTitle"));
    auto *editor=new HeadingTextEdit(&dialog);
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
            ? database_->addWorkRecord(projectId,title->text().trimmed(),editor->toPlainText(),&error)
            : database_->updateWorkRecord(record.id,title->text().trimmed(),editor->toPlainText(),&error);
        if(!ok) { QMessageBox::critical(&dialog,QStringLiteral("保存失败"),error); return; }
        dialog.accept();
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
    if(QMessageBox::question(this,QStringLiteral("删除工作记录"),QStringLiteral("确定删除这条工作记录吗？"),
                             QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return;
    QString error;
    if(!database_->deleteWorkRecord(record.id,&error)) { showError(error); return; }
    refreshWorkRecords();
}
bool ProjectsPage::eventFilter(QObject *watched, QEvent *event)
{
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
