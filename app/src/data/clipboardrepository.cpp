#include "clipboardrepository.h"
#include "repositoryhelpers.h"
#include <QSqlDatabase>
#include <QSet>

using namespace RepositoryHelpers;

QList<AppDatabase::ClipboardTab> ClipboardRepository::clipboardTabs(QString *errorMessage) const
{
    QList<ClipboardTab> result;
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    if (!query.exec(QStringLiteral("SELECT id,name,is_history FROM clipboard_tabs ORDER BY position,id"))) {
        setError(errorMessage, query); return result;
    }
    while (query.next()) result.append({query.value(0).toInt(), query.value(1).toString(), query.value(2).toBool()});
    return result;
}

QList<AppDatabase::ClipboardFolder> ClipboardRepository::clipboardFolders(int tabId, QString *errorMessage) const
{
    QList<ClipboardFolder> result;
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("SELECT id,tab_id,name FROM clipboard_folders WHERE tab_id=? ORDER BY position,id"));
    query.addBindValue(tabId);
    if (!query.exec()) { setError(errorMessage, query); return result; }
    while (query.next()) result.append({query.value(0).toInt(), query.value(1).toInt(), query.value(2).toString()});
    return result;
}

QList<AppDatabase::ClipboardItem> ClipboardRepository::clipboardItems(int tabId, int folderId, const QString &search, QString *errorMessage) const
{
    QList<ClipboardItem> result;
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("SELECT id,tab_id,COALESCE(folder_id,0),title,substr(text,1,240),kind,automatic,created_at FROM clipboard_items WHERE (?=0 OR tab_id=?) AND (?<0 OR COALESCE(folder_id,0)=?) AND (?='' OR instr(lower(title || char(10) || text),lower(?))>0) ORDER BY position,id DESC"));
    query.addBindValue(tabId); query.addBindValue(tabId);
    query.addBindValue(folderId); query.addBindValue(folderId);
    query.addBindValue(text(search)); query.addBindValue(text(search));
    if (!query.exec()) { setError(errorMessage, query); return result; }
    while (query.next()) {
        ClipboardItem item;
        item.id=query.value(0).toInt(); item.tabId=query.value(1).toInt(); item.folderId=query.value(2).toInt();
        item.title=query.value(3).toString(); item.text=query.value(4).toString(); item.kind=query.value(5).toString();
        item.automatic=query.value(6).toBool(); item.createdAt=QDateTime::fromString(query.value(7).toString(), Qt::ISODateWithMs);
        result.append(item);
    }
    return result;
}

AppDatabase::ClipboardItem ClipboardRepository::clipboardItem(int id, QString *errorMessage) const
{
    ClipboardItem item;
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("SELECT id,tab_id,COALESCE(folder_id,0),title,text,kind,payload,fingerprint,automatic,created_at FROM clipboard_items WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec()) { setError(errorMessage, query); return item; }
    if (!query.next()) { fail(errorMessage, QStringLiteral("条目已不存在，请刷新列表。")); return item; }
    item.id=query.value(0).toInt(); item.tabId=query.value(1).toInt(); item.folderId=query.value(2).toInt();
    item.title=query.value(3).toString(); item.text=query.value(4).toString(); item.kind=query.value(5).toString();
    item.payload=query.value(6).toByteArray(); item.fingerprint=query.value(7).toByteArray(); item.automatic=query.value(8).toBool();
    item.createdAt=QDateTime::fromString(query.value(9).toString(), Qt::ISODateWithMs);
    return item;
}

bool ClipboardRepository::addClipboardTab(const QString &name, int *id, QString *errorMessage)
{
    if (name.trimmed().isEmpty() || name.trimmed().size()>80) return fail(errorMessage, QStringLiteral("标签页名称需为 1–80 个字符。"));
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("INSERT INTO clipboard_tabs(name,position) VALUES(?,(SELECT COALESCE(MAX(position),-1)+1 FROM clipboard_tabs))"));
    query.addBindValue(name.trimmed());
    if (!query.exec()) { setError(errorMessage, query); return false; }
    if (id) *id=query.lastInsertId().toInt();
    return true;
}

bool ClipboardRepository::renameClipboardTab(int id, const QString &name, QString *errorMessage)
{
    if (name.trimmed().isEmpty() || name.trimmed().size()>80) return fail(errorMessage, QStringLiteral("标签页名称需为 1–80 个字符。"));
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("UPDATE clipboard_tabs SET name=? WHERE id=? AND is_history=0"));
    query.addBindValue(name.trimmed()); query.addBindValue(id);
    if (!query.exec()) { setError(errorMessage, query); return false; }
    return query.numRowsAffected()==1 || fail(errorMessage, QStringLiteral("历史标签页不能重命名，或标签页已不存在。"));
}

bool ClipboardRepository::addClipboardFolder(int tabId, const QString &name, int *id, QString *errorMessage)
{
    if (name.trimmed().isEmpty() || name.trimmed().size()>80) return fail(errorMessage, QStringLiteral("文件夹名称需为 1–80 个字符。"));
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("INSERT INTO clipboard_folders(tab_id,name,position) VALUES(?,?,(SELECT COALESCE(MAX(position),-1)+1 FROM clipboard_folders WHERE tab_id=?))"));
    query.addBindValue(tabId); query.addBindValue(name.trimmed()); query.addBindValue(tabId);
    if (!query.exec()) { setError(errorMessage, query); return false; }
    if (id) *id=query.lastInsertId().toInt();
    return true;
}

bool ClipboardRepository::renameClipboardFolder(int id, const QString &name, QString *errorMessage)
{
    if (name.trimmed().isEmpty() || name.trimmed().size()>80) return fail(errorMessage, QStringLiteral("文件夹名称需为 1–80 个字符。"));
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("UPDATE clipboard_folders SET name=? WHERE id=?"));
    query.addBindValue(name.trimmed()); query.addBindValue(id);
    if (!query.exec()) { setError(errorMessage, query); return false; }
    return query.numRowsAffected()==1 || fail(errorMessage, QStringLiteral("文件夹已不存在。"));
}

bool ClipboardRepository::saveClipboardItem(ClipboardItem *item, QString *errorMessage)
{
    if (!item || item->payload.isEmpty() || item->payload.size()>24*1024*1024 || item->title.size()>200
        || item->fingerprint.size()!=32 || !QStringList{"text","html","image","files"}.contains(item->kind))
        return fail(errorMessage, QStringLiteral("条目内容无效或超过容量限制。"));
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    const bool creating=item->id==0;
    if (creating) {
        query.prepare(QStringLiteral("INSERT INTO clipboard_items(tab_id,folder_id,title,text,kind,payload,fingerprint,automatic,position,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,(SELECT COALESCE(MAX(position),-1)+1 FROM clipboard_items WHERE tab_id=? AND COALESCE(folder_id,0)=?),?,?)"));
        query.addBindValue(item->tabId); query.addBindValue(item->folderId>0 ? QVariant(item->folderId) : QVariant());
    } else {
        query.prepare(QStringLiteral("UPDATE clipboard_items SET title=?,text=?,kind=?,payload=?,fingerprint=?,automatic=0,updated_at=? WHERE id=?"));
    }
    query.addBindValue(text(item->title)); query.addBindValue(text(item->text)); query.addBindValue(item->kind);
    query.addBindValue(item->payload); query.addBindValue(item->fingerprint);
    const auto timestamp=nowIso();
    if (creating) {
        query.addBindValue(item->automatic ? 1 : 0); query.addBindValue(item->tabId); query.addBindValue(item->folderId);
        query.addBindValue(timestamp); query.addBindValue(timestamp);
    } else { query.addBindValue(timestamp); query.addBindValue(item->id); }
    if (!query.exec()) { setError(errorMessage, query); return false; }
    if (!creating && query.numRowsAffected()!=1) return fail(errorMessage, QStringLiteral("条目已不存在。"));
    if (creating) { item->id=query.lastInsertId().toInt(); item->createdAt=QDateTime::fromString(timestamp, Qt::ISODateWithMs); }
    else item->automatic=false;
    return true;
}

bool ClipboardRepository::captureClipboardItem(ClipboardItem *item, int historyLimit, QString *errorMessage, int editingItemId, const QList<int> &editingItemIds)
{
    if (!item || item->id!=0) return fail(errorMessage, QStringLiteral("无法记录此剪贴板内容。"));
    auto database=QSqlDatabase::database(connectionName_);
    if (!database.transaction()) return fail(errorMessage, database.lastError().text());
    QSqlQuery match(database);
    match.prepare(QStringLiteral(
        "SELECT id,created_at FROM clipboard_items WHERE tab_id=1 AND folder_id IS NULL "
        "AND automatic=1 AND fingerprint=? ORDER BY position,id DESC LIMIT 1"));
    match.addBindValue(item->fingerprint);
    if (!match.exec()) { setError(errorMessage,match); database.rollback(); return false; }
    ClipboardItem saved=*item; saved.tabId=1; saved.folderId=0; saved.automatic=true;
    const bool existing = match.next();
    if (existing) {
        saved.id = match.value(0).toInt();
        saved.createdAt = QDateTime::fromString(match.value(1).toString(), Qt::ISODateWithMs);
    }
    match.finish();
    if (!existing && !saveClipboardItem(&saved,errorMessage)) { database.rollback(); return false; }

    QSqlQuery position(database);
    position.prepare(QStringLiteral(
        "UPDATE clipboard_items SET position=(SELECT MIN(position)-1 FROM clipboard_items WHERE tab_id=1 AND folder_id IS NULL) "
        "WHERE id=? AND id<>(SELECT id FROM clipboard_items WHERE tab_id=1 AND folder_id IS NULL ORDER BY position,id DESC LIMIT 1)"));
    position.addBindValue(saved.id);
    if (!position.exec()) { setError(errorMessage,position); database.rollback(); return false; }
    if (!existing) {
        QSqlQuery trim(database);
        // Retained drafts remain protected even while their module is hidden.
        auto protectedIds=editingItemIds; protectedIds.append(editingItemId);
        QStringList placeholders; for (int id:protectedIds) { Q_UNUSED(id); placeholders.append(QStringLiteral("?")); }
        trim.prepare(QStringLiteral("DELETE FROM clipboard_items WHERE id IN (SELECT id FROM clipboard_items WHERE tab_id=1 AND folder_id IS NULL AND automatic=1 AND id NOT IN (%1) ORDER BY position DESC,id ASC LIMIT (SELECT MAX(0,COUNT(*)-?) FROM clipboard_items WHERE tab_id=1 AND folder_id IS NULL AND automatic=1))").arg(placeholders.join(',')));
        for (int id:protectedIds) trim.addBindValue(id);
        trim.addBindValue(qBound(100,historyLimit,10000));
        if (!trim.exec()) { setError(errorMessage,trim); database.rollback(); return false; }
    }
    if (!database.commit()) { const auto e=database.lastError().text(); database.rollback(); return fail(errorMessage,e); }
    *item=saved;
    return true;
}

bool ClipboardRepository::moveClipboardItems(const QList<int> &ids, int tabId, int folderId, QString *errorMessage)
{
    if (ids.isEmpty()) return true;
    auto database=QSqlDatabase::database(connectionName_);
    if (!database.transaction()) return fail(errorMessage,database.lastError().text());
    QSqlQuery query(database);
    query.prepare(QStringLiteral("UPDATE clipboard_items SET tab_id=?,folder_id=?,automatic=0,position=(SELECT COALESCE(MAX(position),-1)+1 FROM clipboard_items WHERE tab_id=? AND COALESCE(folder_id,0)=?),updated_at=? WHERE id=?"));
    for (int id : ids) {
        query.bindValue(0,tabId); query.bindValue(1,folderId>0 ? QVariant(folderId) : QVariant());
        query.bindValue(2,tabId); query.bindValue(3,folderId); query.bindValue(4,nowIso()); query.bindValue(5,id);
        if (!query.exec() || query.numRowsAffected()!=1) {
            const auto e=query.lastError().isValid() ? query.lastError().text() : QStringLiteral("条目已不存在，移动已取消。");
            database.rollback(); return fail(errorMessage,e);
        }
    }
    if (!database.commit()) { const auto e=database.lastError().text(); database.rollback(); return fail(errorMessage,e); }
    return true;
}

bool ClipboardRepository::deleteClipboardItems(const QList<int> &ids, QString *errorMessage)
{
    auto database=QSqlDatabase::database(connectionName_);
    if (!database.transaction()) return fail(errorMessage,database.lastError().text());
    QSqlQuery query(database);
    query.prepare(QStringLiteral("DELETE FROM clipboard_items WHERE id=?"));
    for (int id : ids) {
        query.bindValue(0,id);
        if (!query.exec()) { setError(errorMessage,query); database.rollback(); return false; }
    }
    if (!database.commit()) { const auto e=database.lastError().text(); database.rollback(); return fail(errorMessage,e); }
    return true;
}

bool ClipboardRepository::deleteClipboardFolder(int id, QString *errorMessage)
{
    auto database=QSqlDatabase::database(connectionName_);
    if (!database.transaction()) return fail(errorMessage,database.lastError().text());
    QSqlQuery keep(database);
    keep.prepare(QStringLiteral("UPDATE clipboard_items SET folder_id=NULL,automatic=0,position=(SELECT COALESCE(MAX(position),0)+1 FROM clipboard_items) + id WHERE folder_id=?"));
    keep.addBindValue(id);
    QSqlQuery remove(database);
    remove.prepare(QStringLiteral("DELETE FROM clipboard_folders WHERE id=?")); remove.addBindValue(id);
    if (!keep.exec() || !remove.exec()) {
        fail(errorMessage,keep.lastError().isValid() ? keep.lastError().text() : remove.lastError().text());
        database.rollback(); return false;
    }
    if (!database.commit()) { const auto e=database.lastError().text(); database.rollback(); return fail(errorMessage,e); }
    return true;
}

bool ClipboardRepository::deleteClipboardTab(int id, QString *errorMessage)
{
    if (id<=1) return fail(errorMessage,QStringLiteral("历史标签页不能删除。"));
    auto database=QSqlDatabase::database(connectionName_);
    if (!database.transaction()) return fail(errorMessage,database.lastError().text());
    QSqlQuery keep(database);
    keep.prepare(QStringLiteral("UPDATE clipboard_items SET tab_id=1,folder_id=NULL,automatic=0,position=(SELECT COALESCE(MAX(position),0)+1 FROM clipboard_items) + id WHERE tab_id=?")); keep.addBindValue(id);
    QSqlQuery remove(database);
    remove.prepare(QStringLiteral("DELETE FROM clipboard_tabs WHERE id=? AND is_history=0")); remove.addBindValue(id);
    if (!keep.exec() || !remove.exec()) {
        fail(errorMessage,keep.lastError().isValid() ? keep.lastError().text() : remove.lastError().text());
        database.rollback(); return false;
    }
    if (!database.commit()) { const auto e=database.lastError().text(); database.rollback(); return fail(errorMessage,e); }
    return true;
}

bool ClipboardRepository::reorderClipboard(const QString &table, const QString &where, const QVariantList &bindings, const QList<int> &ids, QString *errorMessage)
{
    auto database=QSqlDatabase::database(connectionName_);
    if (!database.transaction()) return fail(errorMessage,database.lastError().text());
    QSqlQuery expected(database);
    expected.prepare(QStringLiteral("SELECT id FROM %1 WHERE %2").arg(table,where));
    for (const auto &binding : bindings) expected.addBindValue(binding);
    if (!expected.exec()) { setError(errorMessage,expected); database.rollback(); return false; }
    QSet<int> members;
    while (expected.next()) members.insert(expected.value(0).toInt());
    expected.finish();
    if (QSet<int>(ids.begin(),ids.end())!=members || ids.size()!=members.size()) {
        database.rollback(); return fail(errorMessage,QStringLiteral("列表内容已变化，排序未保存。请刷新后重试。"));
    }
    QSqlQuery save(database);
    save.prepare(QStringLiteral("UPDATE %1 SET position=? WHERE id=?").arg(table));
    for (int i=0; i<ids.size(); ++i) {
        save.bindValue(0,i); save.bindValue(1,ids.at(i));
        if (!save.exec()) { setError(errorMessage,save); database.rollback(); return false; }
    }
    if (!database.commit()) { const auto e=database.lastError().text(); database.rollback(); return fail(errorMessage,e); }
    return true;
}

bool ClipboardRepository::reorderClipboardTabs(const QList<int> &ids, QString *errorMessage)
{ return reorderClipboard(QStringLiteral("clipboard_tabs"),QStringLiteral("1"),{},ids,errorMessage); }
bool ClipboardRepository::reorderClipboardFolders(int tabId, const QList<int> &ids, QString *errorMessage)
{ return reorderClipboard(QStringLiteral("clipboard_folders"),QStringLiteral("tab_id=?"),{tabId},ids,errorMessage); }
bool ClipboardRepository::reorderClipboardItems(int tabId, int folderId, const QList<int> &ids, QString *errorMessage)
{ return reorderClipboard(QStringLiteral("clipboard_items"),QStringLiteral("tab_id=? AND COALESCE(folder_id,0)=?"),{tabId,folderId},ids,errorMessage); }
