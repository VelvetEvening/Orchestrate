#include "clipboardmonitor.h"
#include "clipboardcontent.h"
#include <QApplication>
#include <QClipboard>
#include <QScopedValueRollback>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
quint32 clipboardSequence()
{
#ifdef Q_OS_WIN
    if (QApplication::platformName()=="windows") return GetClipboardSequenceNumber();
#endif
    return 0;
}

bool excludedFromHistory(const QMimeData &mime)
{
    for (const auto &format : mime.formats()) {
        // Qt exposes registered Windows formats both directly and in a MIME wrapper.
        const QString prefix = QStringLiteral("application/x-qt-windows-mime;value=\"");
        QString name = format;
        if (name.startsWith(prefix,Qt::CaseInsensitive))
            name = name.mid(prefix.size()).section('"',0,0);
        if (name.compare("ExcludeClipboardContentFromMonitorProcessing",Qt::CaseInsensitive)==0
            || name.compare("Clipboard Viewer Ignore",Qt::CaseInsensitive)==0
            || name=="application/x-keepassxc" || name=="x-kde-passwordManagerHint") return true;
        if (name.compare("CanIncludeInClipboardHistory",Qt::CaseInsensitive)==0
            && mime.data(format)==QByteArray(4,'\0')) return true;
    }
    return false;
}
}

ClipboardMonitor::ClipboardMonitor(AppDatabase *database, QObject *parent) : QObject(parent),database_(database)
{
    enabled_=database_->setting("clipboard.monitor","false")=="true";
    historyLimit_=qBound(100,database_->setting("clipboard.limit","500").toInt(),10000);
    lastSequence_=clipboardSequence();
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
    enabled_=enabled; timer_.stop(); lastSequence_=clipboardSequence(); return true;
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
    const quint32 sequence=clipboardSequence();
    if (sequence && sequence==lastSequence_) return;
    const auto *mime=QApplication::clipboard()->mimeData();
    if (!mime || excludedFromHistory(*mime)) return;
    AppDatabase::ClipboardItem item; QString error;
    if (!ClipboardContent::read(mime,&item,&error)) { if (!error.isEmpty()) emit captureError(error); return; }
    if (item.kind=="text" && item.text.trimmed().isEmpty()) return;
    if (mime->data(ClipboardContent::OwnerFormat)==item.fingerprint) return;
    if (sequence && clipboardSequence()!=sequence) { timer_.start(); return; }
    if (!database_->captureClipboardItem(&item,historyLimit_,&error,editingItems_.values())) { emit captureError(QStringLiteral("剪贴板未保存：")+error); return; }
    lastSequence_=sequence;
    emit recorded();
}

bool ClipboardMonitor::copyItem(int id, QString *error, quint32 *writtenSequence)
{
    const auto item=database_->clipboardItem(id,error);
    if (!item.id) return false;
    auto mime=ClipboardContent::restore(item.payload,error);
    if (!mime) return false;
    mime->setData(ClipboardContent::OwnerFormat,item.fingerprint);
    const QScopedValueRollback<bool> writing(writing_,true);
    timer_.stop();
    QApplication::clipboard()->setMimeData(mime.release());
    const quint32 sequence=clipboardSequence();
    AppDatabase::ClipboardItem actual;
    QString readError;
    if (!ClipboardContent::read(QApplication::clipboard()->mimeData(),&actual,&readError) || actual.fingerprint!=item.fingerprint) {
        if (error) *error=QStringLiteral("写入系统剪贴板未成功，请重试。")+readError;
        return false;
    }
    if (item.automatic && !database_->promoteClipboardHistoryItem(id,&readError)) {
        if (error) *error=QStringLiteral("已复制，历史排序未保存：")+readError;
        return false;
    }
    if (writtenSequence) *writtenSequence=sequence;
    return true;
}
