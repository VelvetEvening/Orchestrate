#pragma once

#include <QDialog>
#include <QHash>
#include <functional>

class QLabel;
class QLineEdit;
class QPoint;
class QPushButton;
class QShowEvent;
class QTreeWidget;
class QTreeWidgetItem;
class SkinLibrary;

class SkinManagerDialog final : public QDialog
{
public:
    SkinManagerDialog(SkinLibrary &library, std::function<bool(const QString &, QString *)> removeTheme,
                      QWidget *parent);

    void refreshCatalog();
    void updateActiveSkin();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void refreshRows();
    void filterRows();
    void updateActions();
    void updateRowActions(QTreeWidgetItem *item);
    void download(QTreeWidgetItem *item);
    void remove(QTreeWidgetItem *item);
    void showItemMenu(QTreeWidgetItem *item, const QPoint &globalPosition);
    void showStatus(const QString &message, const QString &error = {}, bool clearOnReturn = false);

    SkinLibrary &library_;
    std::function<bool(const QString &, QString *)> removeTheme_;
    QTreeWidget *list_;
    QLineEdit *search_;
    QLabel *status_;
    QPushButton *refresh_;
    bool catalogLoading_ = false;
    bool catalogFailed_ = false;
    bool clearStatusOnReturn_ = false;
    QString downloadingId_;
    QHash<QString, QString> downloadErrors_;
};
