#pragma once

#include <QDialog>
#include <functional>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class SkinLibrary;

class SkinManagerDialog final : public QDialog
{
public:
    SkinManagerDialog(SkinLibrary &library, std::function<bool(const QString &, QString *)> removeTheme,
                      QWidget *parent);

    void refreshCatalog();

private:
    void refreshRows();
    void updateActions();
    void actOnSelection();
    void showStatus(const QString &message, const QString &error = {});

    SkinLibrary &library_;
    std::function<bool(const QString &, QString *)> removeTheme_;
    QStackedWidget *content_;
    QTreeWidget *list_;
    QLabel *empty_;
    QLabel *status_;
    QPushButton *refresh_;
    QPushButton *action_;
    bool catalogLoading_ = false;
    bool catalogFailed_ = false;
    QString downloadingId_;
    QString failedDownloadId_;
};
