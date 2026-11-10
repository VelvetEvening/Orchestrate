#include "clipboardmonitor.h"
#include "clipboardcontent.h"
#include <QApplication>
#include <QClipboard>
#include <QScopedValueRollback>

ClipboardMonitor::ClipboardMonitor(AppDatabase *database, QObject *parent) : QObject(parent),database_(database)
{
    enabled_=database_->setting("clipboard.monitor","false")=="true";
    historyLimit_=qBound(100,database_->setting("clipboard.limit","500").toInt(),10000);
    timer_.setSingleShot(true); timer_.setInterval(30);
    connect(&timer_,&QTimer::timeout,this,&ClipboardMonitor::capture);
    connect(QApplication::clipboard(),&QClipboard::dataChanged,this,[this] {
        if (enabled_ && !writing_) timer_.start();
    });
    // Enabling or restarting never imports a stale clipboard; wait for the next copy.
}

bool ClipboardMonitor::setEnabled(bool enabled, QString *error)
{
    if (!database_->setSetting("clipboard.monitor",enabled ? "true" : "false",error)) return false;
    enabled_=enabled; timer_.stop(); return true;
}

bool ClipboardMonitor::setHistoryLimit(int limit, QString *error)
{
    limit=qBound(100,limit,10000);
    if (!database_->setSetting("clipboard.limit",QString::number(limit),error)) return false;
    historyLimit_=limit; return true;
}

void ClipboardMonitor::capture()
{
    if (!enabled_ || writing_) return;
    const auto *mime=QApplication::clipboard()->mimeData();
    if (!mime) return;
    // Honor applications that explicitly mark their clipboard content as private.
    for (const auto &format : mime->formats()) {
        if (format.contains("ExcludeClipboardContentFromMonitorProcessing",Qt::CaseInsensitive)
            || format=="application/x-keepassxc" || format=="x-kde-passwordManagerHint") return;
    }
    AppDatabase::ClipboardItem item; QString error;
    if (!ClipboardContent::read(mime,&item,&error)) { if (!error.isEmpty()) emit captureError(error); return; }
    if (mime->data(ClipboardContent::OwnerFormat)==item.fingerprint) return;
    if (!database_->captureClipboardItem(&item,historyLimit_,&error)) { emit captureError(QStringLiteral("剪贴板未保存：")+error); return; }
    emit recorded();
}

bool ClipboardMonitor::copyItem(int id, QString *error)
{
    const auto item=database_->clipboardItem(id,error);
    if (!item.id) return false;
    auto mime=ClipboardContent::restore(item.payload,error);
    if (!mime) return false;
    mime->setData(ClipboardContent::OwnerFormat,item.fingerprint);
    const QScopedValueRollback<bool> writing(writing_,true);
    timer_.stop();
    QApplication::clipboard()->setMimeData(mime.release());
    AppDatabase::ClipboardItem actual;
    QString readError;
    if (!ClipboardContent::read(QApplication::clipboard()->mimeData(),&actual,&readError) || actual.fingerprint!=item.fingerprint) {
        if (error) *error=QStringLiteral("写入系统剪贴板未成功，请重试。")+readError;
        return false;
    }
    return true;
}
