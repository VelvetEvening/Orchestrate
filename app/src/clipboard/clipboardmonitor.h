#pragma once
#include "data/appdatabase.h"
#include <QObject>
#include <QTimer>

class ClipboardMonitor final : public QObject {
    Q_OBJECT
public:
    explicit ClipboardMonitor(AppDatabase *database, QObject *parent=nullptr);
    bool enabled() const { return enabled_; }
    bool setEnabled(bool enabled, QString *error=nullptr);
    int historyLimit() const { return historyLimit_; }
    bool setHistoryLimit(int limit, QString *error=nullptr);
    void setEditingItem(int id) { editingItemId_=id; }
    bool copyItem(int id, QString *error=nullptr);
signals:
    void recorded();
    void captureError(const QString &message);
private:
    void capture();
    AppDatabase *database_;
    bool enabled_=false;
    bool writing_=false;
    int historyLimit_=500;
    int editingItemId_=0;
    QTimer timer_;
};
