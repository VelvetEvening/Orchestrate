#include "contentworkspace.h"
#include "theme.h"
#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
QString contentTitle(const QString &title) {
    const QString suffix=QStringLiteral(" · Orchestrate");
    return title.endsWith(suffix) ? title.chopped(suffix.size()) : title;
}
}

class ContentWindow final : public QDialog {
public:
    explicit ContentWindow(QWidget *owner) : QDialog(owner, Qt::Window) {
        setObjectName(QStringLiteral("contentWindow"));
        setWindowFlag(Qt::WindowMaximizeButtonHint);
        setAttribute(Qt::WA_DeleteOnClose);
        resize(1100, 760); setMinimumSize(720, 520);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        stack = new QStackedWidget(this); layout->addWidget(stack);
    }
    QStackedWidget *stack;
    QString rootKey;
    std::function<void()> requestClose;
    void reject() override { if (requestClose) requestClose(); }
protected:
    void closeEvent(QCloseEvent *event) override {
        event->ignore();
        if (requestClose) requestClose();
    }
};

struct ContentWorkspace::Entry {
    QString key, parentKey;
    QPointer<QDialog> page;
    QWidget *frame = nullptr;
    QLabel *title = nullptr;
    QPushButton *back = nullptr, *pop = nullptr;
    QPointer<QWidget> returnFocus, focus;
    QPointer<ContentWindow> host;
};

ContentWorkspace::ContentWorkspace(QWidget *root, QWidget *parent)
    : QWidget(parent), stack_(new QStackedWidget(this)), root_(root)
{
    setObjectName(QStringLiteral("contentWorkspace"));
    UiTheme::setSurface(this, "content");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
    layout->addWidget(stack_); stack_->addWidget(root);
    auto *shortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shortcut, &QShortcut::activated, this, &ContentWorkspace::back);
}
ContentWorkspace::~ContentWorkspace() { destroying_ = true; }
ContentWorkspace *ContentWorkspace::find(QWidget *source) {
    for (auto *widget = source; widget; widget = widget->parentWidget())
        if (auto *workspace = qobject_cast<ContentWorkspace *>(widget)) return workspace;
    return nullptr;
}
ContentWorkspace::EntryPtr ContentWorkspace::entryFor(QWidget *source) const {
    for (const auto &entry : entries_)
        if (entry->page && (entry->page == source || entry->page->isAncestorOf(source))) return entry;
    return {};
}
QDialog *ContentWorkspace::content(const QString &key) const {
    const auto entry = entries_.value(key); return entry ? entry->page.data() : nullptr;
}
QString ContentWorkspace::currentKey() const {
    for (const auto &entry : entries_)
        if (!entry->host && entry->frame == stack_->currentWidget()) return entry->key;
    return {};
}
bool ContentWorkspace::confirmLeaveCurrent() {
    const auto entry = entries_.value(currentKey());
    auto *guard = entry ? dynamic_cast<ContentLeaveGuard *>(entry->page.data()) : nullptr;
    return !guard || guard->confirmLeave();
}
void ContentWorkspace::setActivationGuard(std::function<bool()> guard) {
    activationGuard_ = std::move(guard);
}
bool ContentWorkspace::activate(const QString &key, QWidget *source) {
    const auto entry = entries_.value(key); if (!entry) return false;
    if (source && !entry->host) {
        const auto origin=entryFor(source);
        if (!origin || (origin!=entry && !descendant(origin,key))) entry->parentKey=origin ? origin->key : QString();
        entry->returnFocus=QApplication::focusWidget();
    }
    showEntry(entry); return true;
}
bool ContentWorkspace::eventFilter(QObject *object,QEvent *event) {
    if (event->type()==QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape) {
        for (const auto &entry:entries_) if (entry->page==object) { closePage(entry->key); return true; }
    }
    return QWidget::eventFilter(object,event);
}
void ContentWorkspace::present(const QString &key, QDialog *page, QWidget *source) {
    if (activate(key)) { page->deleteLater(); return; }
    auto entry = std::make_shared<Entry>(); entry->key = key; entry->page = page;
    page->setProperty("contentKey",key);
    entry->returnFocus = QApplication::focusWidget();
    const auto origin = source ? entryFor(source) : EntryPtr{};
    if (origin) { entry->parentKey = origin->key; entry->host = origin->host; }
    entry->frame = new QWidget;
    entry->frame->setObjectName(QStringLiteral("contentFrame"));
    auto *layout = new QVBoxLayout(entry->frame);
    layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
    auto *header = new QWidget(entry->frame); header->setObjectName(QStringLiteral("contentHeader"));
    auto *row = new QHBoxLayout(header); row->setContentsMargins(16,8,16,8); row->setSpacing(10);
    entry->back = new QPushButton(UiTheme::icon(QStringLiteral("chevron-left")), QStringLiteral("返回"), header);
    entry->back->setObjectName(QStringLiteral("contentBack"));
    entry->back->setToolTip(dynamic_cast<ContentLeaveGuard *>(page)
        ? QStringLiteral("返回；有修改时询问是否保存（Alt+←）")
        : QStringLiteral("返回并保留当前内容（Alt+←）"));
    entry->title = new QLabel(page->windowTitle(), header);
    entry->title->setTextFormat(Qt::PlainText); entry->title->setWordWrap(true);
    entry->title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    entry->pop = new QPushButton(QStringLiteral("独立打开"), header);
    entry->pop->setObjectName(QStringLiteral("contentDetach"));
    auto *close = new QPushButton(QStringLiteral("关闭"), header);
    close->setObjectName(QStringLiteral("contentClose"));
    close->setToolTip(QStringLiteral("关闭当前内容；有修改时询问是否保存"));
    for (auto *button : {entry->back, entry->pop, close}) { UiTheme::setRole(button,"quiet"); button->setAutoDefault(false); }
    row->addWidget(entry->back); row->addWidget(entry->title,1); row->addWidget(entry->pop); row->addWidget(close);
    layout->addWidget(header);
    page->setAttribute(Qt::WA_DeleteOnClose, false);
    page->setParent(entry->frame, Qt::Widget);
    page->setModal(false); page->setMinimumSize(0,0);
    page->installEventFilter(this);
    layout->addWidget(page,1);
    entries_.insert(key,entry); order_.append(key);
    (entry->host ? entry->host->stack : stack_)->addWidget(entry->frame);
    connect(entry->back,&QPushButton::clicked,this,[this,entry]{ returnFrom(entry); });
    connect(entry->pop,&QPushButton::clicked,this,[this,entry]{
        if (entry->host) attach(entry->host,entry->key); else detach(entry);
    });
    connect(close,&QPushButton::clicked,this,[this,entry]{ closePage(entry->key); });
    connect(page,&QWidget::windowTitleChanged,this,[this,entry]{ updateHeader(entry); });
    connect(page,&QDialog::finished,this,[this,entry]{ if (!destroying_) remove(entry); });
    updateHeader(entry); showEntry(entry);
}
void ContentWorkspace::showEntry(const EntryPtr &entry) {
    if (!entry || !entry->page) return;
    auto *target = entry->host ? entry->host->stack : stack_;
    for (const auto &other : entries_) {
        if (other->frame == target->currentWidget() && other->page
            && other->page->isAncestorOf(QApplication::focusWidget())) other->focus = QApplication::focusWidget();
    }
    target->setCurrentWidget(entry->frame); entry->page->show();
    if (!entry->host) emit activationRequested();
    if (entry->host) {
        entry->host->setWindowTitle(contentTitle(entry->page->windowTitle()) + QStringLiteral(" · Orchestrate"));
        if (entry->host->isMinimized()) entry->host->showNormal(); else entry->host->show();
        entry->host->raise(); entry->host->activateWindow();
    }
    if (entry->focus && entry->focus->isVisible()) entry->focus->setFocus();
    else entry->page->setFocus();
}
void ContentWorkspace::returnFrom(const EntryPtr &entry, bool confirmLeave) {
    if (!entry) return;
    if (entry->page && entry->page->isAncestorOf(QApplication::focusWidget())) entry->focus=QApplication::focusWidget();
    const auto parent = entries_.value(entry->parentKey);
    if (entry->host && (!parent || parent->host != entry->host)) {
        attach(entry->host,entry->key); return;
    }
    if (confirmLeave) {
        auto *guard = dynamic_cast<ContentLeaveGuard *>(entry->page.data());
        if (guard && !guard->confirmLeave()) return;
    }
    if (parent) showEntry(parent); else stack_->setCurrentWidget(root_);
    if (entry->returnFocus && entry->returnFocus->isVisible()) entry->returnFocus->setFocus();
}
void ContentWorkspace::back() {
    const auto entry=entries_.value(currentKey());
    if (entry) returnFrom(entry);
}
bool ContentWorkspace::descendant(const EntryPtr &entry, const QString &ancestor) const {
    auto key=entry->parentKey;
    while (!key.isEmpty()) {
        if (key==ancestor) return true;
        const auto parent=entries_.value(key); if (!parent) break; key=parent->parentKey;
    }
    return false;
}
bool ContentWorkspace::closePage(const QString &key) {
    auto entry=entries_.value(key); if (!entry) return true;
    const auto keys=order_;
    for (auto it=keys.crbegin(); it!=keys.crend(); ++it) {
        const auto child=entries_.value(*it);
        if (child && descendant(child,key) && !closePage(child->key)) return false;
    }
    entry=entries_.value(key); if (!entry) return true;
    showEntry(entry);
    entry->page->reject();
    return !entries_.contains(key);
}
bool ContentWorkspace::closePrefix(const QString &prefix) {
    const auto keys=order_;
    for (const auto &key:keys) if (key.startsWith(prefix) && !closePage(key)) return false;
    return true;
}
bool ContentWorkspace::closeAll() { return closePrefix(QString()); }
void ContentWorkspace::remove(const EntryPtr &entry) {
    if (!entries_.contains(entry->key)) return;
    auto host=entry->host;
    auto *target=host ? host->stack : stack_;
    const bool current=target->currentWidget()==entry->frame;
    const auto parent=entries_.value(entry->parentKey);
    entries_.remove(entry->key); order_.removeAll(entry->key);
    target->removeWidget(entry->frame); entry->frame->hide(); entry->frame->deleteLater();
    if (current) {
        if (parent && parent->host==host) showEntry(parent);
        else if (!host) stack_->setCurrentWidget(root_);
    }
    if (host && !host->stack->count()) { host->hide(); host->deleteLater(); }
    if (entry->returnFocus && entry->returnFocus->isVisible()) entry->returnFocus->setFocus();
}
void ContentWorkspace::rekey(const QString &oldKey,const QString &newKey) {
    if (oldKey==newKey || entries_.contains(newKey)) return;
    auto entry=entries_.take(oldKey); if (!entry) return;
    entry->key=newKey; entries_.insert(newKey,entry);
    entry->page->setProperty("contentKey",newKey);
    const int index=order_.indexOf(oldKey); if (index>=0) order_[index]=newKey;
    for (const auto &other:entries_) {
        if (other->parentKey==oldKey) other->parentKey=newKey;
        if (other->host && other->host->rootKey==oldKey) other->host->rootKey=newKey;
    }
}
void ContentWorkspace::updateHeader(const EntryPtr &entry) {
    entry->title->setText(contentTitle(entry->page->windowTitle()));
    entry->pop->setText(entry->host ? QStringLiteral("收回主窗口") : QStringLiteral("独立打开"));
    entry->back->setVisible(!entry->host || entry->key!=entry->host->rootKey);
    if (entry->host && entry->host->stack->currentWidget()==entry->frame)
        entry->host->setWindowTitle(contentTitle(entry->page->windowTitle())+QStringLiteral(" · Orchestrate"));
}
void ContentWorkspace::detach(const EntryPtr &entry) {
    auto *host=new ContentWindow(this); host->rootKey=entry->key;
    host->requestClose=[this,host]{ closePage(host->rootKey); };
    returnFrom(entry, false); // Moving the same editor does not leave its content.
    for (const auto &other:entries_) {
        if (other->host || (other!=entry && !descendant(other,entry->key))) continue;
        stack_->removeWidget(other->frame); host->stack->addWidget(other->frame);
        other->host=host; updateHeader(other);
    }
    auto *shortcut=new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left),host);
    connect(shortcut,&QShortcut::activated,this,[this,host]{
        for (const auto &item:entries_) if (item->host==host && item->frame==host->stack->currentWidget()) { returnFrom(item); break; }
    });
    showEntry(entry);
}
void ContentWorkspace::attach(ContentWindow *host,const QString &selected) {
    if (!host) return;
    if (activationGuard_ && !activationGuard_()) return;
    if (!confirmLeaveCurrent()) return;
    for (const auto &entry:entries_) if (entry->host==host) {
        host->stack->removeWidget(entry->frame); stack_->addWidget(entry->frame);
        entry->host=nullptr; updateHeader(entry);
    }
    host->hide(); host->deleteLater();
    emit activationRequested();
    window()->setWindowState(window()->windowState() & ~Qt::WindowMinimized);
    window()->show(); window()->raise(); window()->activateWindow(); activate(selected);
}
QLabel *DraftDialog::createSaveStatus(const QString &name) {
    saveStatus_ = new QLabel(this);
    saveStatus_->setObjectName(name);
    saveStatus_->setAccessibleName(QStringLiteral("保存状态"));
    saveStatus_->setTextFormat(Qt::PlainText);
    saveStatus_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return saveStatus_;
}
void DraftDialog::updateSaveStatus(const QString &missing) {
    const bool modified = dirty && dirty();
    saveStatus_->setText(!missing.isEmpty() ? missing : modified
        ? QStringLiteral("未保存 · Ctrl+S") : QStringLiteral("已保存"));
    UiTheme::setRole(saveStatus_, modified ? "warning" : "muted");
    const auto detail = missing.isEmpty() ? QStringLiteral("Ctrl+S 保存当前内容") : missing;
    saveStatus_->setToolTip(detail); saveStatus_->setAccessibleDescription(detail);
}
void DraftDialog::showSaveError(const QString &error) {
    saveStatus_->setText(QStringLiteral("保存失败"));
    UiTheme::setRole(saveStatus_, "error");
    saveStatus_->setToolTip(error); saveStatus_->setAccessibleDescription(error);
}
void DraftDialog::showSavedStatus(const QString &text) {
    updateSaveStatus(); saveStatus_->setText(text); UiTheme::setRole(saveStatus_, "success");
}
bool DraftDialog::confirmLeave() {
    if (dirty && dirty()) {
        QMessageBox prompt(QMessageBox::Question,QStringLiteral("未保存的修改"),
            QStringLiteral("要保存当前内容的修改吗？"),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,this);
        prompt.button(QMessageBox::Save)->setText(QStringLiteral("保存"));
        prompt.button(QMessageBox::Discard)->setText(QStringLiteral("放弃修改"));
        prompt.button(QMessageBox::Cancel)->setText(QStringLiteral("继续编辑"));
        prompt.setDefaultButton(QMessageBox::Cancel);
        const auto choice=prompt.exec();
        if (choice==QMessageBox::Save) return saveDraft && saveDraft();
        if (choice!=QMessageBox::Discard) return false;
        if (resetDraft) resetDraft();
        return !dirty || !dirty();
    }
    return true;
}
void DraftDialog::reject() {
    if (confirmLeave()) QDialog::reject();
}
