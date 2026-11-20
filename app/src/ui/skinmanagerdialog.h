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
    using ThemeAction = std::function<bool(const QString &, QString *)>;
    SkinManagerDialog(SkinLibrary &library, ThemeAction activateTheme, ThemeAction removeTheme, QWidget *parent);

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
    void activateOrDownload(QTreeWidgetItem *item);
    void remove(QTreeWidgetItem *item);
    void showItemMenu(QTreeWidgetItem *item, const QPoint &globalPosition);
    void showStatus(const QString &message, const QString &error = {}, bool clearOnReturn = false);

    SkinLibrary &library_;
    ThemeAction activateTheme_;
    ThemeAction removeTheme_;
    QTreeWidget *list_;
    QTreeWidgetItem *builtInGroup_ = nullptr;
    QTreeWidgetItem *extensionGroup_ = nullptr;
    QLineEdit *search_;
    QLabel *status_;
    QPushButton *refresh_;
    bool catalogLoading_ = false;
    bool catalogFailed_ = false;
    bool clearStatusOnReturn_ = false;
    QString downloadingId_;
    QHash<QString, QString> downloadErrors_;
};
