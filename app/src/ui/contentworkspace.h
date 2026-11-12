#pragma once

#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QWidget>
#include <functional>
#include <memory>

class QStackedWidget;
class ContentWindow;

// One content instance moves between the module and an optional native window.
// Returning hides it; closing goes through the editor's normal reject/save guard.
class ContentWorkspace final : public QWidget {
    Q_OBJECT
public:
    explicit ContentWorkspace(QWidget *root, QWidget *parent = nullptr);
    ~ContentWorkspace() override;
    static ContentWorkspace *find(QWidget *source);
    bool activate(const QString &key, QWidget *source = nullptr);
    void present(const QString &key, QDialog *content, QWidget *source = nullptr);
    void rekey(const QString &oldKey, const QString &newKey);
    bool closePage(const QString &key);
    bool closePrefix(const QString &prefix);
    bool closeAll();
    void back();
    QString currentKey() const;
    QDialog *content(const QString &key) const;
signals:
    void activationRequested();
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    struct Entry;
    using EntryPtr = std::shared_ptr<Entry>;
    EntryPtr entryFor(QWidget *source) const;
    void showEntry(const EntryPtr &entry);
    void returnFrom(const EntryPtr &entry);
    void remove(const EntryPtr &entry);
    void detach(const EntryPtr &entry);
    void attach(ContentWindow *host, const QString &selected);
    void updateHeader(const EntryPtr &entry);
    bool descendant(const EntryPtr &entry, const QString &ancestor) const;
    QStackedWidget *stack_;
    QWidget *root_;
    QHash<QString, EntryPtr> entries_;
    QList<QString> order_;
    bool destroying_ = false;
};

// Small editor forms share the same explicit save/discard/cancel boundary.
class DraftDialog : public QDialog {
public:
    using QDialog::QDialog;
    std::function<bool()> dirty;
    std::function<bool()> saveDraft;
    void reject() override;
};
