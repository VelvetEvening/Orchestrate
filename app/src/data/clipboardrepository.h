#pragma once

#include "appdatabase.h"
#include "repositorybase.h"

class ClipboardRepository final : public RepositoryBase
{
public:
    using RepositoryBase::RepositoryBase;
    using ClipboardTab = AppDatabase::ClipboardTab;
    using ClipboardFolder = AppDatabase::ClipboardFolder;
    using ClipboardItem = AppDatabase::ClipboardItem;

    QList<ClipboardTab> clipboardTabs(QString *errorMessage = nullptr) const;
    QList<ClipboardFolder> clipboardFolders(int tabId, QString *errorMessage = nullptr) const;
    // Lists contain bounded text previews, never the binary payload. folderId=-1 searches the whole tab;
    // tabId=0 searches all tabs. A concrete tab/folder pair is required for sorting.
    QList<ClipboardItem> clipboardItems(int tabId, int folderId, const QString &search = {}, QString *errorMessage = nullptr) const;
    ClipboardItem clipboardItem(int id, QString *errorMessage = nullptr) const;
    bool addClipboardTab(const QString &name, int *id = nullptr, QString *errorMessage = nullptr);
    bool renameClipboardTab(int id, const QString &name, QString *errorMessage = nullptr);
    bool deleteClipboardTab(int id, QString *errorMessage = nullptr);
    bool addClipboardFolder(int tabId, const QString &name, int *id = nullptr, QString *errorMessage = nullptr);
    bool renameClipboardFolder(int id, const QString &name, QString *errorMessage = nullptr);
    bool deleteClipboardFolder(int id, QString *errorMessage = nullptr);
    enum class InsertAt { Front, Back };
    bool saveClipboardItem(ClipboardItem *item, QString *errorMessage = nullptr, InsertAt insertAt = InsertAt::Back);
    bool captureClipboardItem(ClipboardItem *item, int historyLimit, QString *errorMessage = nullptr, const QList<int> &editingItemIds = {});
    bool promoteClipboardHistoryItem(int id, QString *errorMessage);
    bool moveClipboardItems(const QList<int> &ids, int tabId, int folderId, QString *errorMessage = nullptr);
    bool deleteClipboardItems(const QList<int> &ids, QString *errorMessage = nullptr);
    bool reorderClipboardTabs(const QList<int> &ids, QString *errorMessage = nullptr);
    bool reorderClipboardFolders(int tabId, const QList<int> &ids, QString *errorMessage = nullptr);
    bool reorderClipboardItems(int tabId, int folderId, const QList<int> &ids, QString *errorMessage = nullptr);

private:
    bool mergeTopTextCapture(ClipboardItem *item, const QList<int> &editingItemIds, QString *errorMessage);
    bool reorderClipboard(const QString &table, const QString &where, const QVariantList &bindings,
                          const QList<int> &ids, QString *errorMessage);
};
