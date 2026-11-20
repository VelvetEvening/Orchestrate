#include "mainwindow.h"
#include "test_support.h"
#include "clipboard/clipboardcontent.h"
#include "clipboard/clipboardmonitor.h"
#include "pages/clipboardpage.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTabBar>
#include <QTest>
#include <QTimer>
#include <QTreeWidgetItemIterator>
#include <QUuid>

static void saveEditor(QDialog *dialog) {
    auto *window = dialog->window();
    QWidget activationBridge;
    // After copy-and-hide, offscreen can retain the same native focus window
    // while QApplication has no active widget. Change focus through the normal
    // activation API so the real save shortcut receives its key event.
    if (QApplication::platformName()=="offscreen" && QApplication::activeWindow()!=window) {
        activationBridge.show();
        activationBridge.activateWindow();
        QCoreApplication::processEvents();
    }
    window->activateWindow(); QTest::qWait(20);
    auto *body=dialog->findChild<QPlainTextEdit *>("clipboardItemBody"); body->setFocus();
    QTest::keyClick(body,Qt::Key_S,Qt::ControlModifier);
}
class ClipboardTest : public QObject {
    Q_OBJECT
    std::unique_ptr<AppDatabase> db;
    QString error;
    bool sql(const QString &statement) {
        bool ok=false;
        {
            auto conn=QSqlDatabase::addDatabase("QSQLITE","clipboard-test-fixture"); conn.setDatabaseName(db->databasePath());
            if (conn.open()) { QSqlQuery q(conn); ok=q.exec(statement); if (!ok) error=q.lastError().text(); }
        }
        QSqlDatabase::removeDatabase("clipboard-test-fixture"); return ok;
    }
    AppDatabase::ClipboardItem item(const QString &text, int tab=1, int folder=0) {
        AppDatabase::ClipboardItem entry; entry.tabId=tab; entry.folderId=folder;
        ClipboardContent::plainText(text,&entry,&error); return entry;
    }
    QTreeWidgetItem *node(QTreeWidget *tree,int id,bool folder=false) {
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if ((*it)->data(0,Qt::UserRole).toInt()==id && (*it)->data(0,Qt::UserRole+1).toBool()==folder
                && !(*it)->data(0,Qt::UserRole+4).toBool()) return *it;
        return nullptr;
    }
    void select(QTreeWidget *tree,int id,bool folder=false) {
        tree->clearSelection();
        if (auto *item=node(tree,id,folder)) {
            for (auto *parent=item->parent(); parent; parent=parent->parent()) parent->setExpanded(true);
            tree->setCurrentItem(item);
        }
        QCoreApplication::processEvents();
    }
    int entryCount(QTreeWidget *tree) {
        int count=0;
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if (!(*it)->data(0,Qt::UserRole+1).toBool() && !(*it)->data(0,Qt::UserRole+4).toBool()) ++count;
        return count;
    }
private slots:
    void initTestCase() {
        QVERIFY(QFileInfo(qApp->applicationDirPath()).fileName()=="clipboard-test");
        QVERIFY(QApplication::platformName()=="offscreen");
        QDir().mkpath(qApp->applicationDirPath()+"/artifacts");
    }
    void init() {
        const QString data=qApp->applicationDirPath()+"/data";
        QVERIFY(!QFileInfo::exists(data) || QDir(data).removeRecursively());
        db=std::make_unique<AppDatabase>(); error.clear(); QVERIFY2(db->open(&error),qPrintable(error));
    }
    void cleanup() { db.reset(); QApplication::clipboard()->clear(); }

    void v6MigrationPreservesExistingData() {
        QVERIFY(db->addDiaryEntry(QDate(2026,11,10),"原来的日记",&error));
        QVERIFY(sql("DROP TABLE clipboard_items")); QVERIFY(sql("DROP TABLE clipboard_folders")); QVERIFY(sql("DROP TABLE clipboard_tabs"));
        QVERIFY(sql("ALTER TABLE automation_tools DROP COLUMN customized")); QVERIFY(sql("ALTER TABLE automation_tools DROP COLUMN environment")); QVERIFY(sql("ALTER TABLE tool_commands DROP COLUMN enabled")); QVERIFY(sql("ALTER TABLE tool_commands DROP COLUMN position"));
        QVERIFY(sql("PRAGMA user_version=6")); db.reset(); db=std::make_unique<AppDatabase>(); QVERIFY2(db->open(&error),qPrintable(error));
        QCOMPARE(db->clipboardTabs().size(),1); QCOMPARE(db->clipboardTabs().first().name,QString("历史"));
        QCOMPARE(db->diaryEntries(QDate(2026,11,10)).first().content,QString("原来的日记"));
        const QDir backup(qApp->applicationDirPath()+"/data/backups"); QCOMPARE(backup.entryList({"schema-6-*.sqlite3"},QDir::Files).size(),1);
        const auto count=backup.entryList(QDir::Files).size(); db.reset(); db=std::make_unique<AppDatabase>(); QVERIFY(db->open(&error));
        QCOMPARE(backup.entryList(QDir::Files).size(),count);
    }

    void formatsRoundTrip() {
        const QString text="中文\r\n  spaces\t<&>\n";
        QMimeData plain; plain.setText(text);
        QMimeData html; html.setText("粗体与链接"); html.setHtml("<p><b>粗体</b>与<a href='https://example.invalid'>链接</a><img src='https://example.invalid/remote.png'></p>");
        QMimeData image; QImage pixel(64,32,QImage::Format_ARGB32); pixel.fill(QColor(20,80,140,100)); image.setImageData(pixel);
        QMimeData files; files.setUrls({QUrl::fromLocalFile("C:/测试 文件/one #.txt"),QUrl::fromLocalFile("D:/不存在/two.txt")});
        for (const auto *mime : {&plain,&html,&image,&files}) {
            AppDatabase::ClipboardItem entry; QVERIFY2(ClipboardContent::read(mime,&entry,&error),qPrintable(error));
            QVERIFY(db->saveClipboardItem(&entry,&error)); const auto saved=db->clipboardItem(entry.id);
            auto restored=ClipboardContent::restore(saved.payload,&error); QVERIFY2(restored!=nullptr,qPrintable(error));
            AppDatabase::ClipboardItem reread; QVERIFY(ClipboardContent::read(restored.get(),&reread,&error));
            QCOMPARE(reread.fingerprint,entry.fingerprint); QCOMPARE(reread.text,entry.text);
            if (entry.kind=="image") QCOMPARE(qvariant_cast<QImage>(restored->imageData()),pixel);
            if (entry.kind=="files") QCOMPARE(restored->urls(),files.urls());
            if (entry.kind=="html") QCOMPARE(restored->html(),html.html());
        }
        QMimeData huge; huge.setText(QString(ClipboardContent::MaximumBytes+1,QLatin1Char('a')));
        AppDatabase::ClipboardItem rejected; QVERIFY(!ClipboardContent::read(&huge,&rejected,&error)); QVERIFY(!error.isEmpty());
        QVERIFY(!ClipboardContent::restore("{}",&error));
    }

    void organizeAndReorderAcrossRestart() {
        int a=0,b=0,fa=0,fb=0,fa2=0;
        QVERIFY(db->addClipboardTab("开发",&a,&error)); QVERIFY(db->addClipboardTab("论文",&b,&error));
        QVERIFY(!db->addClipboardTab("开发",nullptr,&error)); QVERIFY(!db->renameClipboardTab(1,"别名",&error)); QVERIFY(!db->deleteClipboardTab(1,&error));
        QVERIFY(db->addClipboardFolder(a,"提示词",&fa,&error)); QVERIFY(db->addClipboardFolder(a,"命令",&fa2,&error)); QVERIFY(db->addClipboardFolder(b,"摘录",&fb,&error));
        auto first=item("first",a,fa),second=item("second",a,fa),third=item("third",a,fa);
        QVERIFY(db->saveClipboardItem(&first,&error)); QVERIFY(db->saveClipboardItem(&second,&error)); QVERIFY(db->saveClipboardItem(&third,&error));
        QVERIFY(db->reorderClipboardTabs({b,1,a},&error)); QVERIFY(db->reorderClipboardFolders(a,{fa2,fa},&error));
        QVERIFY(db->reorderClipboardItems(a,fa,{third.id,first.id,second.id},&error));
        QVERIFY(!db->reorderClipboardItems(a,fa,{third.id,first.id},&error));
        QVERIFY(!db->moveClipboardItems({first.id},a,fb,&error)); QCOMPARE(db->clipboardItem(first.id).folderId,fa);
        QVERIFY(!db->moveClipboardItems({first.id,99999},b,fb,&error)); QCOMPARE(db->clipboardItem(first.id).tabId,a);
        QVERIFY(db->moveClipboardItems({first.id,second.id},b,fb,&error));
        QCOMPARE(db->clipboardItems(b,fb).at(0).id,first.id); QCOMPARE(db->clipboardItems(b,fb).at(1).id,second.id);
        db.reset(); db=std::make_unique<AppDatabase>(); QVERIFY(db->open(&error));
        QCOMPARE(db->clipboardTabs().at(0).id,b); QCOMPARE(db->clipboardFolders(a).at(0).id,fa2);
        QCOMPARE(db->clipboardItems(a,fa).first().id,third.id);
        QVERIFY(db->deleteClipboardFolder(fb,&error)); QCOMPARE(db->clipboardItems(b,0).size(),2);
        QVERIFY(db->deleteClipboardTab(b,&error)); QCOMPARE(db->clipboardItems(1,0).size(),2);
        QCOMPARE(db->clipboardItem(first.id).text,QString("first")); QVERIFY(!db->clipboardItem(first.id).automatic);
    }

    void repeatedCopiesMoveTheSameHistoryRowsToTheTop() {
        ClipboardPage page(db.get());
        page.show();
        auto *monitor = page.findChild<ClipboardMonitor *>();
        auto *tree = page.findChild<QTreeWidget *>("clipboardItems");
        QVERIFY(monitor && tree && monitor->setEnabled(true, &error));
        QApplication::clipboard()->setText("A");
        QTRY_COMPARE(tree->topLevelItemCount(), 1);
        const int a = tree->topLevelItem(0)->data(0, Qt::UserRole).toInt();
        QApplication::clipboard()->setText("B");
        QTRY_COMPARE(tree->topLevelItemCount(), 2);
        const int b = tree->topLevelItem(0)->data(0, Qt::UserRole).toInt();
        QVERIFY(a != b);
        const auto original = db->clipboardItem(a);
        QApplication::clipboard()->setText("A");
        QTRY_COMPARE(tree->topLevelItem(0)->data(0, Qt::UserRole).toInt(), a);
        QCOMPARE(tree->topLevelItemCount(), 2);
        QCOMPARE(db->clipboardItem(a).createdAt, original.createdAt);
        QApplication::clipboard()->setText("B");
        QTRY_COMPARE(tree->topLevelItem(0)->data(0, Qt::UserRole).toInt(), b);
        QCOMPARE(tree->topLevelItem(1)->data(0, Qt::UserRole).toInt(), a);
        QCOMPARE(tree->topLevelItemCount(), 2);
    }

    void consecutiveTextCopies_data() {
        QTest::addColumn<QString>("firstHtml");
        QTest::addColumn<QString>("secondHtml");
        QTest::newRow("plain-plain") << QString() << QString();
        QTest::newRow("rich-plain") << QString("<b>A</b>") << QString();
        QTest::newRow("plain-rich") << QString() << QString("<i>A</i>");
        QTest::newRow("rich-rich") << QString("<b>A</b>") << QString("<i>A</i>");
    }

    void consecutiveTextCopies() {
        QFETCH(QString, firstHtml);
        QFETCH(QString, secondHtml);
        ClipboardMonitor monitor(db.get());
        QVERIFY(monitor.setEnabled(true, &error));
        QSignalSpy recorded(&monitor, &ClipboardMonitor::recorded);
        auto copy = [](const QString &html) {
            auto *mime = new QMimeData;
            mime->setText("A");
            if (!html.isEmpty()) mime->setHtml(html);
            QApplication::clipboard()->setMimeData(mime);
        };
        copy(firstHtml);
        QTRY_COMPARE(recorded.size(), 1);
        const auto initial = db->clipboardItems(1, 0);
        QCOMPARE(initial.size(), 1);
        const auto original = initial.first();
        copy(secondHtml);
        QTRY_COMPARE(recorded.size(), 2);
        const auto history = db->clipboardItems(1, 0);
        QCOMPARE(history.size(), 1);
        QCOMPARE(history.first().id, original.id);
        const auto saved = db->clipboardItem(original.id);
        QVERIFY(saved.automatic);
        QCOMPARE(saved.createdAt, original.createdAt);
        QVERIFY2(monitor.copyItem(saved.id, &error), qPrintable(error));
        QCOMPARE(QApplication::clipboard()->text(), QString("A"));
        QCOMPARE(QApplication::clipboard()->mimeData()->html(), secondHtml.isEmpty() ? firstHtml : secondHtml);
    }

    void existingHistoryReusesFirstMatchingRowAfterRestart() {
        auto first = item("A"), otherA = item("A"), b = item("B");
        QVERIFY(db->captureClipboardItem(&first, 100, &error));
        otherA.automatic = true;
        QVERIFY(db->saveClipboardItem(&otherA, &error)); // A duplicate from an older history.
        QVERIFY(db->captureClipboardItem(&b, 100, &error));
        QVERIFY(sql("DROP INDEX clipboard_history_fingerprint"));
        QVERIFY(sql("CREATE INDEX clipboard_auto_fingerprint ON clipboard_items(fingerprint,position,id DESC) "
                    "WHERE tab_id=1 AND folder_id IS NULL AND automatic=1"));
        db.reset();
        db = std::make_unique<AppDatabase>();
        QVERIFY2(db->open(&error), qPrintable(error));
        QVERIFY2(sql("SELECT id FROM clipboard_items INDEXED BY clipboard_history_fingerprint "
                     "WHERE tab_id=1 AND folder_id IS NULL LIMIT 0"), qPrintable(error));

        auto repeat = item("A");
        QVERIFY(sql("CREATE TRIGGER block_promotion BEFORE UPDATE OF position ON clipboard_items "
                    "BEGIN SELECT RAISE(ABORT,'test failure'); END"));
        QVERIFY(!db->captureClipboardItem(&repeat, 100, &error));
        QCOMPARE(repeat.id, 0);
        QCOMPARE(db->clipboardItems(1, 0).first().id, b.id);
        QVERIFY(sql("DROP TRIGGER block_promotion"));
        QVERIFY(db->captureClipboardItem(&repeat, 100, &error));
        QCOMPARE(repeat.id, first.id);
        QCOMPARE(repeat.createdAt, first.createdAt);
        const auto history = db->clipboardItems(1, 0);
        QCOMPARE(history.size(), 3);
        QCOMPARE(history.at(0).id, first.id);
        QCOMPARE(history.at(1).id, b.id);
        QCOMPARE(history.at(2).id, otherA.id);
    }

    void textMergeOnlyUsesTheTopAfterAnExactMiss() {
        QMimeData mime;
        mime.setText("A");
        mime.setHtml("<b>A</b>");
        AppDatabase::ClipboardItem rich;
        QVERIFY(ClipboardContent::read(&mime, &rich, &error));
        auto plain = item("A"), other = item("B");
        QVERIFY(db->captureClipboardItem(&plain, 100, &error));
        QVERIFY(db->captureClipboardItem(&other, 100, &error));
        QVERIFY(db->captureClipboardItem(&rich, 100, &error));
        QCOMPARE(db->clipboardItems(1, 0).size(), 3);
        auto repeat = item("A");
        QVERIFY(db->captureClipboardItem(&repeat, 100, &error));
        QCOMPARE(repeat.id, plain.id);
        QCOMPARE(db->clipboardItems(1, 0).first().id, plain.id);
        QCOMPARE(db->clipboardItem(rich.id).payload, rich.payload);
    }

    void textMergeProtectsDraftsAndRollsBackOnFailure() {
        auto plain = item("A");
        QVERIFY(db->captureClipboardItem(&plain, 100, &error));
        QMimeData mime;
        mime.setText("A");
        mime.setHtml("<b>A</b>");
        AppDatabase::ClipboardItem rich;
        QVERIFY(ClipboardContent::read(&mime, &rich, &error));
        QVERIFY(sql("CREATE TRIGGER fail_merge BEFORE UPDATE OF payload ON clipboard_items "
                    "BEGIN SELECT RAISE(ABORT,'test merge failure'); END"));
        QVERIFY(!db->captureClipboardItem(&rich, 100, &error));
        QCOMPARE(rich.id, 0);
        QCOMPARE(db->clipboardItems(1, 0).size(), 1);
        QCOMPARE(db->clipboardItem(plain.id).payload, plain.payload);

        QVERIFY2(db->captureClipboardItem(&rich, 100, &error, {plain.id}), qPrintable(error));
        QVERIFY(rich.id != plain.id);
        QCOMPARE(db->clipboardItems(1, 0).size(), 2);
        QCOMPARE(db->clipboardItem(plain.id).payload, plain.payload);
    }

    void automaticRetentionProtectsCuratedContent() {
        auto curated=item("手动保留"); QVERIFY(db->saveClipboardItem(&curated,&error));
        int customTab=0; QVERIFY(db->addClipboardTab("长期保留",&customTab,&error));
        auto custom=item("其他标签页的内容",customTab); QVERIFY(db->saveClipboardItem(&custom,&error));
        auto transferred=item("从历史整理到其他页"); QVERIFY(db->captureClipboardItem(&transferred,100,&error));
        QVERIFY(db->moveClipboardItems({transferred.id},customTab,0,&error));
        auto moved=item("已整理"); QVERIFY(db->captureClipboardItem(&moved,100,&error));
        int folder=0; QVERIFY(db->addClipboardFolder(1,"保留",&folder,&error)); QVERIFY(db->moveClipboardItems({moved.id},1,folder,&error));
        for (int i=0;i<106;++i) { auto entry=item(QString("自动 %1").arg(i)); QVERIFY2(db->captureClipboardItem(&entry,100,&error),qPrintable(error)); }
        QCOMPARE(db->clipboardItems(1,0).size(),101); QCOMPARE(db->clipboardItems(1,folder).size(),1);
        QVERIFY(db->clipboardItem(curated.id).id); QVERIFY(db->clipboardItem(moved.id).id);
        QCOMPARE(db->clipboardItems(customTab,0).size(),2);
        QCOMPARE(db->clipboardItem(custom.id).text,custom.text); QCOMPARE(db->clipboardItem(transferred.id).text,transferred.text);
        auto same=item("自动 105"); QVERIFY(db->captureClipboardItem(&same,100,&error)); QCOMPARE(db->clipboardItems(1,0).size(),101);
        auto manualCopy = item(curated.text);
        QVERIFY(db->captureClipboardItem(&manualCopy, 100, &error));
        QCOMPARE(manualCopy.id, curated.id);
        QVERIFY(!manualCopy.automatic);
        QCOMPARE(db->clipboardItems(1,0).last().id, curated.id);
        for (const auto &archived : {moved, transferred}) {
            auto recopy = item(archived.text);
            QVERIFY(db->captureClipboardItem(&recopy, 100, &error));
            QVERIFY(recopy.id != archived.id);
            QCOMPARE(db->clipboardItems(1, 0).first().id, recopy.id);
            QVERIFY(!db->clipboardItem(archived.id).automatic);
        }
        auto failed=item("失败应回滚");
        QVERIFY(sql("CREATE TRIGGER fail_clipboard BEFORE INSERT ON clipboard_items BEGIN SELECT RAISE(ABORT,'test failure'); END"));
        QVERIFY(!db->captureClipboardItem(&failed,100,&error)); QCOMPARE(failed.id,0); QCOMPARE(db->clipboardItems(1,0).size(),101);
    }

    void retentionFollowsVisibleOrderAndRollsBack() {
        QList<int> created;
        for (int i=0;i<100;++i) { auto entry=item(QString("ordered %1").arg(i)); QVERIFY(db->captureClipboardItem(&entry,100,&error)); created.append(entry.id); }
        QList<int> order{created.first()};
        for (const auto &entry:db->clipboardItems(1,0)) if (entry.id!=created.first()) order.append(entry.id);
        QVERIFY(db->reorderClipboardItems(1,0,order,&error));
        db.reset(); db=std::make_unique<AppDatabase>(); QVERIFY(db->open(&error));
        auto next=item("after moving oldest to top"); QVERIFY(db->captureClipboardItem(&next,100,&error));
        QVERIFY(db->clipboardItem(created.first()).id); QVERIFY(!db->clipboardItem(created.at(1)).id);
        order.clear(); for (const auto &entry:db->clipboardItems(1,0)) if (entry.id!=next.id) order.append(entry.id);
        order.append(next.id); QVERIFY(db->reorderClipboardItems(1,0,order,&error));
        auto later=item("after moving newest to bottom"); QVERIFY(db->captureClipboardItem(&later,100,&error));
        QVERIFY(!db->clipboardItem(next.id).id); QVERIFY(db->clipboardItem(created.first()).id);
        const auto before=db->clipboardItems(1,0); order.clear(); for(const auto &entry:before) order.append(entry.id);
        QVERIFY(sql("CREATE TRIGGER fail_trim BEFORE DELETE ON clipboard_items BEGIN SELECT RAISE(ABORT,'cleanup failure'); END"));
        auto failed=item("must roll back insertion too"); QVERIFY(!db->captureClipboardItem(&failed,100,&error)); QCOMPARE(failed.id,0);
        QList<int> after; for(const auto &entry:db->clipboardItems(1,0)) after.append(entry.id); QCOMPARE(after,order);
    }

    void captureSurvivesWhenAllOlderRowsAreBeingEdited() {
        QList<int> editing;
        for (int i = 0; i < 100; ++i) {
            auto entry = item(QStringLiteral("draft %1").arg(i));
            QVERIFY(db->captureClipboardItem(&entry, 100, &error));
            editing.append(entry.id);
        }
        QVERIFY(sql("CREATE TRIGGER reject_extra_move BEFORE UPDATE OF position ON clipboard_items "
                    "BEGIN SELECT RAISE(ABORT,'new items should already be at the front'); END"));
        auto newest = item("new while all older items are protected");
        QVERIFY2(db->captureClipboardItem(&newest, 100, &error, editing), qPrintable(error));
        const auto history = db->clipboardItems(1, 0);
        QCOMPARE(history.size(), 101);
        QCOMPARE(history.first().id, newest.id);

        const int released = editing.takeFirst();
        auto next = item("new after one editor closes");
        QVERIFY2(db->captureClipboardItem(&next, 100, &error, editing), qPrintable(error));
        QCOMPARE(db->clipboardItems(1, 0).size(), 100);
        QCOMPARE(db->clipboardItems(1, 0).first().id, next.id);
        QVERIFY(!db->clipboardItem(released).id);
        QVERIFY(!db->clipboardItem(newest.id).id);
    }

    void editorSurvivesRetentionAndReleasesProtection() {
        QList<int> ids;
        for(int i=0;i<100;++i) { auto entry=item(QString("editing history %1").arg(i)); QVERIFY(db->captureClipboardItem(&entry,100,&error)); ids.append(entry.id); }
        ClipboardPage page(db.get()); page.resize(900,650); page.show();
        auto *monitor=page.findChild<ClipboardMonitor *>(); QVERIFY(monitor->setHistoryLimit(100,&error)); QVERIFY(monitor->setEnabled(true,&error));
        auto *tree=page.findChild<QTreeWidget *>("clipboardItems"); select(tree,ids.first());
        bool survived=false,failedSaveProtected=false,saved=false;
        QTimer::singleShot(0,[&] {
            auto *dialog=page.findChild<QDialog *>("clipboardEditor"); if(!dialog) return;
            auto *body=dialog->findChild<QPlainTextEdit *>("clipboardItemBody"); body->setPlainText("edited and retained");
            QApplication::clipboard()->setText("copy during edit one"); QTest::qWait(180);
            survived=db->clipboardItem(ids.first()).id && !db->clipboardItem(ids.at(1)).id && db->clipboardItems(1,0).size()==100;
            QApplication::clipboard()->setText("editing history 0"); QTest::qWait(180);
            survived = survived && db->clipboardItems(1,0).first().id == ids.first()
                && body->toPlainText() == QStringLiteral("edited and retained");
            sql("CREATE TRIGGER fail_edit_capacity BEFORE UPDATE ON clipboard_items WHEN OLD.id="+QString::number(ids.first())+" BEGIN SELECT RAISE(ABORT,'save failure'); END");
            saveEditor(dialog);
            QApplication::clipboard()->setText("copy during failed edit"); QTest::qWait(180);
            failedSaveProtected=dialog->isVisible() && body->toPlainText()=="edited and retained" && db->clipboardItem(ids.first()).id
                && dialog->findChild<QLabel *>("clipboardEditorError")->text()==QStringLiteral("保存失败");
            sql("DROP TRIGGER fail_edit_capacity"); saveEditor(dialog);
            saved=dialog->isVisible() && dialog->findChild<QLabel *>("clipboardEditorError")->text()==QStringLiteral("已保存");
            if(dialog->isVisible()) dialog->done(QDialog::Rejected);
        });
        page.findChild<QPushButton *>("clipboardEditItem")->click(); QVERIFY(survived); QVERIFY(failedSaveProtected); QVERIFY(saved);
        QCOMPARE(db->clipboardItem(ids.first()).text,QString("edited and retained")); QVERIFY(!db->clipboardItem(ids.first()).automatic);
        // Fill the free automatic slot created by saving the edited item.
        QApplication::clipboard()->setText("fill after save"); QTest::qWait(180);
        int cancelled=0; for(const auto &entry:db->clipboardItems(1,0)) if(entry.automatic) cancelled=entry.id;
        QVERIFY(cancelled); select(tree,cancelled); bool cancelProtected=false;
        QTimer::singleShot(0,[&] {
            auto *dialog=page.findChild<QDialog *>("clipboardEditor"); if(!dialog) return;
            QApplication::clipboard()->setText("copy during unchanged edit"); QTest::qWait(180);
            cancelProtected=db->clipboardItem(cancelled).id!=0; dialog->reject();
        });
        page.findChild<QPushButton *>("clipboardEditItem")->click(); QVERIFY(cancelProtected); QVERIFY(db->clipboardItem(cancelled).automatic);
        QApplication::clipboard()->setText("copy after cancelling edit"); QTest::qWait(180);
        QVERIFY(!db->clipboardItem(cancelled).id); QCOMPARE(db->clipboardItem(ids.first()).text,QString("edited and retained"));
    }

    void bitmapAndPlainTextShareOneItemWithoutLosingDifferentImages() {
        AppDatabase::ClipboardItem red, blue;
        for (auto *imageItem : {&red, &blue}) {
            QImage image(8, 8, QImage::Format_RGB32);
            image.fill(imageItem==&red ? Qt::red : Qt::blue);
            QMimeData mime; mime.setImageData(image); mime.setText("same caption");
            QVERIFY(ClipboardContent::read(&mime,imageItem,&error));
        }
        auto plain = item("same caption");
        QVERIFY(db->captureClipboardItem(&plain,100,&error));
        QVERIFY(db->captureClipboardItem(&red,100,&error));
        QCOMPARE(red.id,plain.id);
        auto repeat = item("same caption");
        QVERIFY(db->captureClipboardItem(&repeat,100,&error));
        QCOMPARE(repeat.id,red.id);
        QCOMPARE(repeat.fingerprint,red.fingerprint);
        QCOMPARE(db->clipboardItems(1,0).size(),1);
        ClipboardMonitor monitor(db.get());
        QVERIFY(monitor.copyItem(red.id,&error));
        QCOMPARE(QApplication::clipboard()->text(),plain.text);
        QCOMPARE(qvariant_cast<QImage>(QApplication::clipboard()->mimeData()->imageData()).pixelColor(0,0),QColor(Qt::red));
        QVERIFY(db->captureClipboardItem(&blue,100,&error));
        QVERIFY(blue.id!=red.id);
        QCOMPARE(db->clipboardItems(1,0).size(),2);
        QCOMPARE(db->clipboardItem(red.id).fingerprint,red.fingerprint);
    }

    void automaticCaptureFiltersPrivateAndBlankButCanRecopyAfterDeletion() {
        ClipboardMonitor monitor(db.get());
        QVERIFY(monitor.setEnabled(true,&error));
        QSignalSpy recorded(&monitor,&ClipboardMonitor::recorded);
        const QString history = QStringLiteral("application/x-qt-windows-mime;value=\"CanIncludeInClipboardHistory\"");
        auto copy = [&](const QString &text, const QString &format = {}, QByteArray value = {}) {
            auto *mime = new QMimeData;
            mime->setText(text);
            if (!format.isEmpty()) mime->setData(format,value);
            QApplication::clipboard()->setMimeData(mime);
        };
        copy("synthetic test",history,QByteArray(4,'\0'));
        QTest::qWait(90);
        copy("synthetic test","Clipboard Viewer Ignore");
        QTest::qWait(90);
        copy(" \n\t ");
        QTest::qWait(90);
        QCOMPARE(recorded.size(),0);
        QCOMPARE(db->clipboardItems(1,0).size(),0);
        const QString code = "  x()\n";
        copy(code,history,QByteArray::fromHex("01000000"));
        QTRY_COMPARE(recorded.size(),1);
        const int old = db->clipboardItems(1,0).first().id;
        QCOMPARE(db->clipboardItem(old).text,code);
        QVERIFY(db->deleteClipboardItems({old},&error));
        copy(code,history,QByteArray::fromHex("01000000"));
        QTRY_COMPARE(recorded.size(),2);
        QCOMPARE(db->clipboardItems(1,0).size(),1);
        QVERIFY(db->clipboardItems(1,0).first().id!=old);
    }

    void takeHistoryItemRespectsSelectionOrderAndPastePreference() {
        auto a=item("A"), b=item("B"), manual=item("M");
        QVERIFY(db->saveClipboardItem(&manual,&error));
        QVERIFY(db->captureClipboardItem(&a,100,&error));
        QVERIFY(db->captureClipboardItem(&b,100,&error));
        ClipboardPage page(db.get()); page.show();
        auto *tree=page.findChild<QTreeWidget *>("clipboardItems");
        auto *paste=page.findChild<QAction *>("clipboardPasteOnUse");
        QVERIFY(tree && paste && !paste->isChecked());
        QSignalSpy hidden(&page,&ClipboardPage::hideRequested);
        select(tree,a.id); node(tree,b.id)->setSelected(true);
        tree->setFocus(); QTest::keyClick(tree,Qt::Key_Return);
        QCOMPARE(hidden.size(),0);
        QCOMPARE(db->clipboardItems(1,0).first().id,b.id);
        select(tree,a.id); QTest::keyClick(tree,Qt::Key_Return);
        QCOMPARE(hidden.size(),1); QCOMPARE(hidden.takeFirst().first().toBool(),false);
        QCOMPARE(db->clipboardItems(1,0).first().id,a.id);
        QCOMPARE(QApplication::clipboard()->text(),a.text);
        paste->trigger();
        QVERIFY(ClipboardPage(db.get()).findChild<QAction *>("clipboardPasteOnUse")->isChecked());
        select(tree,manual.id); QTest::keyClick(tree,Qt::Key_Return);
        QCOMPARE(hidden.size(),1); QCOMPARE(hidden.takeFirst().first().toBool(),true);
        QCOMPARE(db->clipboardItems(1,0).last().id,manual.id);
        QCOMPARE(QApplication::clipboard()->text(),manual.text);
    }

    void monitorToggleSelfCopyAndFailure() {
        ClipboardMonitor monitor(db.get()); QSignalSpy recorded(&monitor,&ClipboardMonitor::recorded); QSignalSpy failures(&monitor,&ClipboardMonitor::captureError);
        QCOMPARE(monitor.historyLimit(),500);
        QVERIFY(!monitor.enabled()); QApplication::clipboard()->setText("暂停时不记录"); QTest::qWait(60); QCOMPARE(recorded.size(),0);
        QVERIFY(monitor.setEnabled(true,&error)); QApplication::clipboard()->setText("监听正常"); QTRY_COMPARE(recorded.size(),1);
        auto entry=db->clipboardItems(1,0).first(); QVERIFY(monitor.copyItem(entry.id,&error)); QTest::qWait(80); QCOMPARE(recorded.size(),1);
        QCOMPARE(QApplication::clipboard()->text(),QString("监听正常")); QCOMPARE(db->clipboardItems(1,0).size(),1);
        QVERIFY(monitor.setEnabled(false,&error)); QApplication::clipboard()->setText("不进入历史"); QTest::qWait(60); QCOMPARE(recorded.size(),1);
        QVERIFY(monitor.setEnabled(true,&error));
        auto *secret=new QMimeData; secret->setText("excluded synthetic test"); secret->setData("application/x-qt-windows-mime;value=\"ExcludeClipboardContentFromMonitorProcessing\"","1");
        QApplication::clipboard()->setMimeData(secret); QTest::qWait(70); QCOMPARE(recorded.size(),1);
        QVERIFY(sql("CREATE TRIGGER fail_monitor BEFORE INSERT ON clipboard_items BEGIN SELECT RAISE(ABORT,'test write failure'); END"));
        QApplication::clipboard()->setText("保存失败反馈"); QTRY_COMPARE(failures.size(),1); QCOMPARE(db->clipboardItems(1,0).size(),1);
        QVERIFY(!monitor.copyItem(99999,&error));
        QVERIFY(monitor.setEnabled(false,&error)); ClipboardMonitor reopened(db.get()); QVERIFY(!reopened.enabled());
    }

    void settingsMenuPersistsMonitoringAndLimit() {
        ClipboardPage page(db.get());
        page.resize(900, 650); page.show();
        auto *settings = page.findChild<QPushButton *>("clipboardSettings");
        auto *listening = page.findChild<QAction *>("clipboardListening");
        auto *create = page.findChild<QPushButton *>("clipboardNewItem");
        QVERIFY(settings && listening && create);
        QVERIFY(settings->isVisible());
        QCOMPARE(settings->geometry().top(), create->geometry().top());
        QCOMPARE(settings->parentWidget()->width() - settings->geometry().right() - 1, 24);
        for (auto *button : settings->parentWidget()->findChildren<QPushButton *>())
            QVERIFY(button->text() != QStringLiteral("复制并收起"));
        auto *menu = settings->menu();
        QVERIFY(menu && menu->actions().first() == listening);
        QVERIFY(!listening->isChecked());
        QTimer::singleShot(0, menu, [=] {
            menu->setActiveAction(listening);
            QTest::keyClick(menu, Qt::Key_Return);
        });
        settings->setFocus(); QTest::keyClick(settings, Qt::Key_Space);
        QTRY_COMPARE(db->setting("clipboard.monitor"), "true");
        {
            ClipboardPage restored(db.get());
            QVERIFY(restored.findChild<QAction *>("clipboardListening")->isChecked());
        }
        QTimer::singleShot(0, menu, [=] {
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(listening).center());
        });
        settings->click();
        QTRY_COMPARE(db->setting("clipboard.monitor"), "false");
        QTimer::singleShot(0, menu, [&] {
            menu->setActiveAction(menu->actions().last());
            QTimer::singleShot(0, &page, [] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog) return;
                auto *limit = dialog->findChild<QSpinBox *>();
                auto *buttons = dialog->findChild<QDialogButtonBox *>();
                if (!limit || !buttons) { dialog->reject(); return; }
                limit->setValue(700);
                buttons->button(QDialogButtonBox::Save)->click();
            });
            QTest::keyClick(menu, Qt::Key_Return);
        });
        settings->click();
        QTRY_COMPARE(db->setting("clipboard.limit"), "700");
    }

    void editorPreservesContentAndProtectsDrafts_data() {
        QTest::addColumn<bool>("withHtml");
        QTest::newRow("plain") << false;
        QTest::newRow("html-wrapped-text") << true;
    }

    void editorPreservesContentAndProtectsDrafts() {
        QFETCH(bool, withHtml);
        auto entry=item("line one\r\nline two\r\n");
        if (withHtml) {
            QMimeData mime; mime.setText(entry.text); mime.setHtml("<div>line one<br>line two<br></div>");
            QVERIFY(ClipboardContent::read(&mime,&entry,&error));
        }
        QVERIFY(db->saveClipboardItem(&entry,&error));
        ClipboardPage page(db.get()); page.resize(900,650); page.show();
        auto *items=page.findChild<QTreeWidget *>("clipboardItems"); select(items,entry.id);
        bool saved=false;
        QTimer::singleShot(0,[&] {
            auto *dialog=page.findChild<QDialog *>("clipboardEditor"); if (!dialog) return;
            dialog->findChild<QLineEdit *>("clipboardItemTitle")->setText("只改标题");
            saveEditor(dialog); saved=dialog->isVisible(); dialog->reject();
        });
        page.findChild<QPushButton *>("clipboardEditItem")->click(); QVERIFY(saved);
        QCOMPARE(db->clipboardItem(entry.id).payload,entry.payload); QCOMPARE(db->clipboardItem(entry.id).text,entry.text);
        bool protectedDraft=false, failedSavePreserved=false;
        QTimer::singleShot(0,[&] {
            auto *dialog=page.findChild<QDialog *>("clipboardEditor"); if (!dialog) return;
            auto *body=dialog->findChild<QPlainTextEdit *>("clipboardItemBody"); body->setPlainText("draft to preserve");
            QTimer::singleShot(0,[&] { auto *message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (message) message->button(QMessageBox::Cancel)->click(); });
            dialog->reject(); protectedDraft=dialog->isVisible() && body->toPlainText()=="draft to preserve";
            sql("CREATE TRIGGER fail_edit BEFORE UPDATE ON clipboard_items BEGIN SELECT RAISE(ABORT,'test save failure'); END");
            saveEditor(dialog);
            failedSavePreserved=dialog->isVisible() && body->toPlainText()=="draft to preserve" && dialog->findChild<QLabel *>("clipboardEditorError")->text()==QStringLiteral("保存失败");
            sql("DROP TRIGGER fail_edit");
            QTimer::singleShot(0,[&] { auto *message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (message) message->button(QMessageBox::Discard)->click(); });
            dialog->reject();
        });
        page.findChild<QPushButton *>("clipboardEditItem")->click(); QVERIFY(protectedDraft); QVERIFY(failedSavePreserved);
        QCOMPARE(db->clipboardItem(entry.id).payload,entry.payload);

        bool writable=false;
        QTimer::singleShot(0,[&] {
            auto *dialog=page.findChild<QDialog *>("clipboardEditor");
            auto *body=dialog->findChild<QPlainTextEdit *>("clipboardItemBody");
            writable=!body->isReadOnly();
            body->setPlainText("  edited\nline two"); saveEditor(dialog); dialog->reject();
        });
        page.findChild<QPushButton *>("clipboardEditItem")->click(); QVERIFY(writable);
        QCOMPARE(db->clipboardItems(1,0).size(),1);
        const auto updated=db->clipboardItem(entry.id);
        QCOMPARE(updated.createdAt,entry.createdAt);
        QCOMPARE(updated.title,QString("只改标题"));
        QCOMPARE(updated.kind,QString("text"));
        ClipboardMonitor monitor(db.get()); QVERIFY(monitor.copyItem(entry.id,&error));
        QCOMPARE(QApplication::clipboard()->text(),QString("  edited\nline two"));
        QVERIFY(!QApplication::clipboard()->mimeData()->hasHtml());
    }

    void attachmentEditorKeepsOriginalPayload() {
        QMimeData imageMime, fileMime;
        QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::green);
        imageMime.setImageData(image); imageMime.setText("caption");
        fileMime.setUrls({QUrl::fromLocalFile("C:/fixture.txt")});
        for (const auto *mime : {&imageMime,&fileMime}) {
            AppDatabase::ClipboardItem entry;
            QVERIFY(ClipboardContent::read(mime,&entry,&error));
            QVERIFY(db->saveClipboardItem(&entry,&error));
            ClipboardPage page(db.get()); page.show();
            select(page.findChild<QTreeWidget *>("clipboardItems"),entry.id);
            auto *picture = page.findChild<QLabel *>("clipboardImage");
            QVERIFY(picture);
            QCOMPARE(picture->isVisible(), entry.kind == "image");
            if (entry.kind == "image") QCOMPARE(picture->pixmap().size(), image.size());
            bool readOnly=false;
            QTimer::singleShot(0,&page,[&] {
                auto *dialog=page.findChild<QDialog *>("clipboardEditor");
                readOnly=dialog->findChild<QPlainTextEdit *>("clipboardItemBody")->isReadOnly();
                dialog->findChild<QLineEdit *>("clipboardItemTitle")->setText("retained");
                saveEditor(dialog); dialog->reject();
            });
            page.findChild<QPushButton *>("clipboardEditItem")->click();
            QVERIFY(readOnly);
            const auto saved=db->clipboardItem(entry.id);
            QCOMPARE(saved.title,QString("retained"));
            QCOMPARE(saved.payload,entry.payload);
        }
    }

    void dragTransfersAndRollback() {
        int folder=0,other=0,tab=0,target=0;
        QVERIFY(db->addClipboardFolder(1,"A",&folder,&error));
        QVERIFY(db->addClipboardFolder(1,"B",&other,&error));
        QVERIFY(db->addClipboardTab("第二页",&tab,&error));
        QVERIFY(db->addClipboardFolder(tab,"跨页文件夹",&target,&error));
        auto a=item("Alpha",1,folder),b=item("Beta",1,folder),c=item("Gamma",1,other);
        QVERIFY(db->saveClipboardItem(&a,&error)); QVERIFY(db->saveClipboardItem(&b,&error)); QVERIFY(db->saveClipboardItem(&c,&error));
        ClipboardPage page(db.get()); page.resize(900,650); page.show();
        auto *tree=static_cast<SiblingOrderTree *>(page.findChild<QTreeWidget *>("clipboardItems"));
        QCoreApplication::processEvents();
        select(tree,a.id); node(tree,b.id)->setSelected(true); node(tree,other,true)->setExpanded(false);
        QVERIFY(sql("CREATE TRIGGER fail_move BEFORE UPDATE OF folder_id ON clipboard_items WHEN OLD.text='Beta' BEGIN SELECT RAISE(ABORT,'move blocked'); END"));
        QVERIFY(!tree->dropSelectionAt(tree->visualItemRect(node(tree,other,true)).center()));
        QCOMPARE(db->clipboardItem(a.id).folderId,folder); QCOMPARE(db->clipboardItem(b.id).folderId,folder);
        QCOMPARE(node(tree,a.id)->parent(),node(tree,folder,true)); QCOMPARE(tree->selectedItems().size(),2);
        QVERIFY(sql("DROP TRIGGER fail_move"));
        QVERIFY(tree->dropSelectionAt(tree->visualItemRect(node(tree,other,true)).center()));
        QCOMPARE(db->clipboardItem(a.id).folderId,other); QCOMPARE(db->clipboardItem(b.id).folderId,other);
        QVERIFY(node(tree,other,true)->isExpanded()); QCOMPARE(tree->selectedItems().size(),2);
        const auto moved=db->clipboardItems(1,other); QCOMPARE(moved.at(1).id,a.id); QCOMPARE(moved.at(2).id,b.id);
        QVERIFY(tree->dropSelectionAt(QPoint(tree->viewport()->width()/2,tree->viewport()->height()-4)));
        QCOMPARE(db->clipboardItems(1,0).size(),2); QVERIFY(!node(tree,a.id)->parent());
        select(tree,a.id);
        node(tree,other,true)->setExpanded(true);
        QCoreApplication::processEvents();
        QCOMPARE(tree->itemAt(tree->visualItemRect(node(tree,c.id)).center()),node(tree,c.id));
        QVERIFY(tree->dropSelectionAt(tree->visualItemRect(node(tree,c.id)).center()));
        QCOMPARE(db->clipboardItem(a.id).folderId,other);
        // Same-container leaf drops retain the existing sorting semantics.
        QVERIFY(tree->dropSelectionAt(tree->visualItemRect(node(tree,c.id)).topLeft()+QPoint(30,2)));
        QCOMPARE(db->clipboardItems(1,other).first().id,a.id);
        select(tree,folder,true); QVERIFY(!tree->canTransferSelectionTo(node(tree,other,true)));
        select(tree,a.id);
        QTreeWidgetItem destination;
        destination.setData(0,Qt::UserRole+4,true); destination.setData(0,Qt::UserRole+2,tab);
        QVERIFY(tree->transferSelectionTo(&destination));
        QCOMPARE(db->clipboardItem(a.id).tabId,tab); QCOMPARE(db->clipboardItem(a.id).folderId,0);
        QCoreApplication::processEvents();
        QVERIFY(tree->dropSelectionAt(tree->visualItemRect(node(tree,target,true)).center()));
        QCOMPARE(db->clipboardItem(a.id).tabId,tab); QCOMPARE(db->clipboardItem(a.id).folderId,target);
        QCOMPARE(node(tree,a.id)->parent(),node(tree,target,true));
        auto *search=page.findChild<QLineEdit *>("clipboardSearch"); search->setText("Alpha");
        QTRY_VERIFY(!tree->dragEnabled()); QVERIFY(!tree->canTransferSelectionTo(node(tree,target,true)));
        AppDatabase reopened; QVERIFY(reopened.open(&error)); QCOMPARE(reopened.clipboardItem(a.id).folderId,target);
    }

    void deletionConfirmationByTab_data() {
        QTest::addColumn<bool>("history");
        QTest::addColumn<bool>("inFolder");
        QTest::addColumn<bool>("filtered");
        QTest::addColumn<bool>("contextMenu");
        // 0: approve, 1: cancel, 2: Esc, 3: close, 4: Enter (default cancel).
        QTest::addColumn<int>("response");
        QTest::newRow("history-manual-and-automatic") << true << false << false << false << 0;
        QTest::newRow("history-folder-filtered-menu") << true << true << true << true << 0;
        QTest::newRow("custom-single-confirm") << false << false << false << false << 0;
        QTest::newRow("custom-folder-filtered-menu") << false << true << true << true << 0;
        QTest::newRow("custom-cancel") << false << false << false << false << 1;
        QTest::newRow("custom-folder-cancel") << false << true << false << false << 1;
        QTest::newRow("custom-escape") << false << false << true << true << 2;
        QTest::newRow("custom-close") << false << false << false << false << 3;
        QTest::newRow("custom-enter-default-cancels") << false << false << false << false << 4;
    }

    void deletionConfirmationByTab() {
        QFETCH(bool,history); QFETCH(bool,inFolder); QFETCH(bool,filtered);
        QFETCH(bool,contextMenu); QFETCH(int,response);
        const QString customName=QStringLiteral("<b>保存的命令 & 提示词 %2</b>");
        int custom=0,folder=0; QVERIFY(db->addClipboardTab(customName,&custom,&error));
        // History can be dragged away from index zero; policy must follow identity.
        QVERIFY(db->reorderClipboardTabs({custom,1},&error));
        const int target=history ? 1 : custom;
        if (inFolder) QVERIFY(db->addClipboardFolder(target,"文件夹",&folder,&error));
        auto a=item("待删除 alpha",target,folder),b=item("待删除 beta",target,folder);
        auto keep=item("同页保留",target,folder),elsewhere=item("其他页保留",history ? custom : 1);
        QVERIFY(db->saveClipboardItem(&a,&error));
        if (history && !inFolder) QVERIFY(db->captureClipboardItem(&b,500,&error));
        else QVERIFY(db->saveClipboardItem(&b,&error));
        QVERIFY(db->saveClipboardItem(&keep,&error)); QVERIFY(db->saveClipboardItem(&elsewhere,&error));
        QVERIFY(db->setSetting("clipboard.tab",QString::number(target),&error));
        ClipboardPage page(db.get()); page.resize(980,640); page.show();
        auto *tree=page.findChild<QTreeWidget *>("clipboardItems");
        if (filtered) {
            page.findChild<QLineEdit *>("clipboardSearch")->setText("待删除");
            QTRY_COMPARE(entryCount(tree),2);
        }
        select(tree,a.id); node(tree,b.id)->setSelected(true);
        QCOMPARE(tree->selectedItems().size(),2);
        int confirmations=0; bool menuSeen=false,labelsOk=true,defaultsOk=true,unchangedBeforeConfirm=true;
        int incomingId=0;
        QTimer responder; responder.setInterval(10);
        connect(&responder,&QTimer::timeout,&page,[&] {
            if (auto *menu=qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                menuSeen=true;
                labelsOk &= menu->actions().last()->text()==(history ? QStringLiteral("删除") : QStringLiteral("删除…"));
                QTest::keyClick(menu,Qt::Key_End); QTest::keyClick(menu,Qt::Key_Return); return;
            }
            auto *message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!message) return;
            responder.stop();
            ++confirmations;
            unchangedBeforeConfirm &= db->clipboardItem(a.id).id==a.id && db->clipboardItem(b.id).id==b.id;
            defaultsOk &= message->defaultButton()==message->button(QMessageBox::No)
                && message->escapeButton()==message->button(QMessageBox::No) && message->textFormat()==Qt::PlainText;
            labelsOk &= message->text().contains(customName) && message->text().contains("2 条")
                && message->button(QMessageBox::No)->text()=="取消"
                && message->button(QMessageBox::Yes)->text()=="确认删除";
            if (!history && response==0 && !inFolder && !contextMenu) {
                TestSupport::capture(*message, "delete-single-confirm");
                // A real monitoring event during the modal must not broaden deletion.
                if (confirmations==1) {
                    auto *monitor=page.findChild<ClipboardMonitor *>(); monitor->setEnabled(true,&error);
                    QApplication::clipboard()->setText("确认期间新复制的内容"); QTest::qWait(250);
                    const auto arrivals=db->clipboardItems(1,0);
                    for (const auto &entry:arrivals) if (entry.text=="确认期间新复制的内容") incomingId=entry.id;
                }
            }
            responder.start();
            if (history || confirmations>1 || response==1)
                message->button(QMessageBox::No)->click();
            else if (response==2) QTest::keyClick(message,Qt::Key_Escape);
            else if (response==3) message->close();
            else if (response==4) QTest::keyClick(message,Qt::Key_Return);
            else message->button(QMessageBox::Yes)->click();
        });
        responder.start();
        if (contextMenu) {
            const auto point=tree->visualItemRect(node(tree,a.id)).center();
            QVERIFY(QMetaObject::invokeMethod(tree,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,point)));
        } else page.findChild<QPushButton *>("clipboardDeleteItems")->click();
        responder.stop();
        QCOMPARE(confirmations,history ? 0 : 1);
        QCOMPARE(menuSeen,contextMenu); QVERIFY(labelsOk); QVERIFY(defaultsOk); QVERIFY(unchangedBeforeConfirm);
        const bool deleted=history || response==0;
        QCOMPARE(db->clipboardItem(a.id).id,deleted ? 0 : a.id); QCOMPARE(db->clipboardItem(b.id).id,deleted ? 0 : b.id);
        QCOMPARE(db->clipboardItem(keep.id).text,keep.text); QCOMPARE(db->clipboardItem(elsewhere.id).text,elsewhere.text);
        if (!history && response==0 && !inFolder && !contextMenu) {
            QVERIFY(incomingId>0); QCOMPARE(db->clipboardItem(incomingId).text,QString("确认期间新复制的内容"));
        }
        if (inFolder) QCOMPARE(db->clipboardFolders(target).size(),1);
        AppDatabase reopened; QVERIFY(reopened.open(&error)); QCOMPARE(reopened.clipboardItem(a.id).id,deleted ? 0 : a.id);
    }

    void deletionFailureRollsBackWholeSelection() {
        auto a=item("删除失败 alpha"),b=item("删除失败 beta");
        QVERIFY(db->saveClipboardItem(&a,&error)); QVERIFY(db->saveClipboardItem(&b,&error));
        ClipboardPage page(db.get()); page.show(); auto *tree=page.findChild<QTreeWidget *>("clipboardItems");
        select(tree,a.id); node(tree,b.id)->setSelected(true);
        QVERIFY(sql(QString("CREATE TRIGGER fail_delete BEFORE DELETE ON clipboard_items WHEN OLD.id=%1 BEGIN SELECT RAISE(ABORT,'test delete failure'); END").arg(b.id)));
        page.findChild<QPushButton *>("clipboardDeleteItems")->click();
        QCOMPARE(db->clipboardItem(a.id).text,a.text); QCOMPARE(db->clipboardItem(b.id).text,b.text);
        QCOMPARE(entryCount(tree),2); QVERIFY(page.findChild<QLabel *>("clipboardStatus")->isVisible());
        QVERIFY(page.findChild<QLabel *>("clipboardStatus")->text().contains("test delete failure"));
        tree->clearSelection(); QVERIFY(!page.findChild<QPushButton *>("clipboardDeleteItems")->isEnabled());
    }

    void pageAndMainWindowIntegration() {
        QVERIFY(db->setSetting("global_shortcut_enabled","false"));
        int tab=0,folder=0,folder2=0; QVERIFY(db->addClipboardTab("开发素材",&tab,&error)); QVERIFY(db->addClipboardFolder(tab,"提示词",&folder,&error)); QVERIFY(db->addClipboardFolder(tab,"命令",&folder2,&error));
        auto a=item("请梳理这个模块的职责与交互边界。\n保留已有数据，给出清晰的验证步骤。",tab,folder);
        auto b=item("git status --short",tab,folder); auto c=item("复制后在目标应用中手动粘贴。",tab,folder);
        auto root=item("临时复制的内容直接显示在标签页顶层",tab);
        a.title="模块开发提示词"; b.title="检查工作区"; c.title="操作说明";
        QVERIFY(db->saveClipboardItem(&a,&error)); QVERIFY(db->saveClipboardItem(&b,&error)); QVERIFY(db->saveClipboardItem(&c,&error));
        QVERIFY(db->saveClipboardItem(&root,&error));
        MainWindow window; QVERIFY(window.ready()); window.show();
        auto *page=window.findChild<ClipboardPage *>(); QVERIFY(page);
        auto *stack=window.findChild<QStackedWidget *>("pageStack"); QCOMPARE(stack->count(),5); QVERIFY(stack->widget(3)->isAncestorOf(page));
        QPushButton *nav=nullptr;
        for (auto *button : window.findChildren<QPushButton *>("navButton")) if (button->text()=="剪贴板") nav=button;
        QVERIFY(nav); QTest::mouseClick(nav,Qt::LeftButton); QVERIFY(stack->currentWidget()->isAncestorOf(page));
        auto *emptyHint=page->findChild<QLabel *>(QStringLiteral("emptyViewHint"));
        QVERIFY(emptyHint && emptyHint->isVisibleTo(page));
        QVERIFY(emptyHint->text().contains(QStringLiteral("开启监听")));
        QVERIFY(emptyHint->testAttribute(Qt::WA_TransparentForMouseEvents));
        QVERIFY(!page->findChild<QLabel *>("clipboardStatus")->isVisible());
        QVERIFY(!page->findChild<QWidget *>("clipboardFolders"));
        TestSupport::capture(window, "clipboard-empty");
        auto *tabs=page->findChild<QTabBar *>("clipboardTabs");
        QVERIFY(!tabs->tabIcon(0).isNull()); QVERIFY(tabs->tabIcon(1).isNull());
        QVERIFY(tabs->tabToolTip(0).contains(QStringLiteral("自动收纳")));
        QVERIFY(!page->findChild<QPushButton *>("clipboardMoveItems"));
        QVERIFY(!page->findChild<QWidget *>("clipboardSearchScope"));
        tabs->setCurrentIndex(1);
        auto *items=page->findChild<QTreeWidget *>("clipboardItems"); QVERIFY(items);
        QCOMPARE(entryCount(items),4); QCOMPARE(items->topLevelItemCount(),3);
        QCOMPARE(node(items,a.id)->parent(),node(items,folder,true)); QVERIFY(!node(items,root.id)->parent());
        QCOMPARE(node(items,a.id)->text(0),a.title); QCOMPARE(node(items,root.id)->text(0),root.text);
        select(items,a.id);
        QCOMPARE(page->findChild<QPlainTextEdit *>("clipboardPreview")->toPlainText(),a.text);
        items->setFocus(); QTest::keyClick(items,Qt::Key_Down,Qt::ControlModifier); QCOMPARE(db->clipboardItems(tab,folder).at(1).id,a.id);
        select(items,folder,true); QTest::keyClick(items,Qt::Key_Down,Qt::ControlModifier); QCOMPARE(db->clipboardFolders(tab).at(1).id,folder);
        tabs->moveTab(1,0); QCOMPARE(db->clipboardTabs().first().id,tab); QCOMPARE(tabs->tabData(tabs->currentIndex()).toInt(),tab);
        auto *search=page->findChild<QLineEdit *>("clipboardSearch"); search->setText("git status"); QTRY_COMPARE(entryCount(items),1);
        const auto before=db->clipboardItems(tab,folder); items->setFocus(); QTest::keyClick(items,Qt::Key_Down,Qt::ControlModifier);
        QCOMPARE(db->clipboardItems(tab,folder).first().id,before.first().id);
        search->clear(); QTRY_COMPARE(entryCount(items),4);
        node(items,folder,true)->setExpanded(false);
        search->setText("提示词"); QTRY_COMPARE(entryCount(items),3); QVERIFY(node(items,folder,true)->isExpanded());
        search->clear(); QTRY_COMPARE(entryCount(items),4); QVERIFY(!node(items,folder,true)->isExpanded());
        ClipboardPage restored(db.get());
        auto *restoredTree=restored.findChild<QTreeWidget *>("clipboardItems");
        QVERIFY(node(restoredTree,folder,true)); QVERIFY(!node(restoredTree,folder,true)->isExpanded());
        select(items,folder,true); items->setFocus(); QSignalSpy folderCopy(page,&ClipboardPage::hideRequested);
        QTest::keyClick(items,Qt::Key_Return); QVERIFY(node(items,folder,true)->isExpanded()); QCOMPARE(folderCopy.size(),0);
        select(items,a.id);
        // Sorting failure preserves the displayed sibling order and data.
        const int originalFirst=node(items,folder,true)->child(0)->data(0,Qt::UserRole).toInt();
        QVERIFY(sql("CREATE TRIGGER fail_tree_order BEFORE UPDATE OF position ON clipboard_items BEGIN SELECT RAISE(ABORT,'test ordering failure'); END"));
        QTest::keyClick(items,Qt::Key_Down,Qt::ControlModifier);
        QCOMPARE(node(items,folder,true)->child(0)->data(0,Qt::UserRole).toInt(),originalFirst);
        QCOMPARE(db->clipboardItems(tab,folder).first().id,originalFirst);
        QVERIFY(sql("DROP TRIGGER fail_tree_order"));
        // A successful monitoring toggle clears the transient error, without status copy.
        auto *listening=window.findChild<QAction *>("clipboardListening"); QVERIFY(listening); listening->setChecked(true); listening->setChecked(false);
        QVERIFY(!page->findChild<QLabel *>("clipboardStatus")->isVisible());
        auto *split = page->findChild<QSplitter *>("workspaceSplit");
        auto *preview = page->findChild<QPlainTextEdit *>("clipboardPreview");
        auto *density = window.findChild<QComboBox *>("appearanceLayout");
        auto *sidebarToggle = window.findChild<QPushButton *>("sidebarToggle");
        QVERIFY(split && preview && density && sidebarToggle);
        QCOMPARE(split->orientation(), Qt::Horizontal);
        QVERIFY(split->sizes().at(1) > split->width() / 4);
        QCOMPARE(items->font().pointSizeF(), 11.0);
        QCOMPARE(preview->font().pointSizeF(), 12.0);
        const auto *current = items->currentItem();
        QVERIFY(current);
        const int standardHeight = items->visualItemRect(current).height();
        TestSupport::capture(window, "clipboard-desktop");
        window.resize(1020, 640);
        QTRY_COMPARE(split->orientation(), Qt::Vertical);
        TestSupport::capture(window, "clipboard-compact");

        // The same window width straddles the preview breakpoint when the sidebar changes width.
        const int navHeight = nav->height();
        QTest::mouseClick(sidebarToggle, Qt::LeftButton);
        QTRY_COMPARE(split->orientation(), Qt::Horizontal);
        QCOMPARE(items->currentItem(), current);
        QCOMPARE(preview->toPlainText(), a.text);
        QVERIFY(nav->isChecked() && nav->text().isEmpty());
        QCOMPARE(nav->height(), navHeight);
        QCOMPARE(nav->accessibleName(), QStringLiteral("剪贴板"));
        QCOMPARE(sidebarToggle->accessibleName(), QStringLiteral("展开侧边栏"));

        QTest::mouseClick(sidebarToggle, Qt::LeftButton);
        QTRY_COMPARE(split->orientation(), Qt::Vertical);
        QCOMPARE(nav->height(), navHeight);
        QCOMPARE(nav->text(), QStringLiteral("剪贴板"));
        QCOMPARE(sidebarToggle->accessibleName(), QStringLiteral("收起侧边栏"));
        density->setCurrentIndex(density->findData("compact"));
        QTRY_VERIFY(items->visualItemRect(current).height() < standardHeight);
        QCOMPARE(items->font().pointSizeF(), 10.0);
        QCOMPARE(preview->font().pointSizeF(), 12.0);
        QVERIFY(preview->isVisible());
        QCOMPARE(preview->toPlainText(), a.text);
        QCOMPARE(items->currentItem(), current);
        if (qEnvironmentVariableIsSet("ORCHESTRATE_CLIPBOARD_LAYOUT_REVIEW")) {
            qInfo() << "Clipboard layout: list point size" << items->font().pointSizeF()
                << "preview point size" << page->findChild<QPlainTextEdit *>("clipboardPreview")->font().pointSizeF()
                << "row height" << items->visualItemRect(items->currentItem()).height()
                << "tab height" << tabs->tabRect(tabs->currentIndex()).height();
        }
        QSignalSpy hidden(page,&ClipboardPage::hideRequested);
        items->scrollToItem(items->currentItem()); QCoreApplication::processEvents();
        QTest::mouseClick(items->viewport(),Qt::LeftButton,Qt::NoModifier,items->visualItemRect(items->currentItem()).center());
        QTest::mouseDClick(items->viewport(),Qt::LeftButton,Qt::NoModifier,items->visualItemRect(items->currentItem()).center());
        QTRY_COMPARE(hidden.size(),1); QVERIFY(!window.isVisible()); QCOMPARE(QApplication::clipboard()->text(),a.text);
        window.showMainWindow();
        // New entry through the actual dialog and save handler.
        bool editorSeen=false;
        QTimer::singleShot(0,[&] {
            auto *dialog=window.findChild<QDialog *>("clipboardEditor"); if (!dialog) return;
            editorSeen=true; dialog->findChild<QLineEdit *>("clipboardItemTitle")->setText("手动创建");
            dialog->findChild<QPlainTextEdit *>("clipboardItemBody")->setPlainText("手动新建正文");
            saveEditor(dialog);
        });
        page->findChild<QPushButton *>("clipboardNewItem")->click(); QTRY_VERIFY(editorSeen);
        QCOMPARE(window.findChild<QPlainTextEdit *>("clipboardItemBody")->font().pointSizeF(), 12.0);
        QCOMPARE(window.findChild<QDialog *>("clipboardEditor")->findChild<QLabel *>("clipboardEditorError")->text(),QStringLiteral("已保存"));
        if (qEnvironmentVariableIsSet("ORCHESTRATE_CLIPBOARD_LAYOUT_REVIEW")) {
            QTest::qWait(30); window.grab().save(qApp->applicationDirPath()+"/artifacts/clipboard-editor.png");
            qInfo() << "Clipboard editor point size" << window.findChild<QPlainTextEdit *>("clipboardItemBody")->font().pointSizeF();
        }
        window.findChild<QDialog *>("clipboardEditor")->reject(); QCOMPARE(db->clipboardItems(tab,folder).size(),4);
        // Multi-selection uses the same destination handler as tab-header drops.
        select(items,a.id); node(items,b.id)->setSelected(true);
        QTreeWidgetItem history;
        history.setData(0,Qt::UserRole+4,true); history.setData(0,Qt::UserRole+2,1);
        auto *orderedTree=static_cast<SiblingOrderTree *>(items);
        QVERIFY(orderedTree->transferSelectionTo(&history)); QCOMPARE(db->clipboardItems(1,0).size(),2);
        QCOMPARE(tabs->tabData(tabs->currentIndex()).toInt(),1);
        for(int i=0;i<tabs->count();++i) if(tabs->tabData(i).toInt()==tab) tabs->setCurrentIndex(i);
        QCOMPARE(db->clipboardItem(a.id).text,a.text);
        QCOMPARE(entryCount(items),3);
        search->setText("git status"); QTRY_COMPARE(entryCount(items),0);
        for(int i=0;i<tabs->count();++i) if(tabs->tabData(i).toInt()==1) tabs->setCurrentIndex(i);
        QCOMPARE(entryCount(items),1); QVERIFY(node(items,b.id)); QVERIFY(!node(items,b.id)->parent());
        for(int i=0;i<tabs->count();++i) if(tabs->tabData(i).toInt()==tab) tabs->setCurrentIndex(i);
        QCOMPARE(entryCount(items),0);
        search->clear(); QTRY_COMPARE(entryCount(items),3); QVERIFY(items->dragEnabled());
        if (qEnvironmentVariableIsSet("ORCHESTRATE_CLIPBOARD_LAYOUT_REVIEW")) {
            for (const auto &name : {"常用命令", "待整理", "参考资料", "写作素材", "项目协作", "一个很长的中文标签页名称用于检查窄窗口省略与滚动"})
                QVERIFY(db->addClipboardTab(QString::fromUtf8(name),nullptr,&error));
            ClipboardPage overflow(db.get()); overflow.resize(764,608); overflow.show();
            auto *overflowTabs=overflow.findChild<QTabBar *>("clipboardTabs");
            overflowTabs->setCurrentIndex(0); overflowTabs->setFocus(); QTest::qWait(30);
            overflow.grab().save(qApp->applicationDirPath()+"/artifacts/clipboard-tabs-overflow-first.png");
            overflowTabs->setCurrentIndex(overflowTabs->count()-1); QTest::qWait(30);
            overflow.grab().save(qApp->applicationDirPath()+"/artifacts/clipboard-tabs-overflow-last.png");
            qInfo() << "Clipboard overflow tabs" << overflowTabs->count() << "last tab rectangle" << overflowTabs->tabRect(overflowTabs->currentIndex());
        }
    }
};

int main(int argc,char **argv) {
    QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false); QStandardPaths::setTestModeEnabled(true);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyhbd.ttc");
    app.setFont(QFont(QStringLiteral("Microsoft YaHei"),9)); app.setStyle(QStyleFactory::create("Fusion"));
    app.setOrganizationName("OrchestrateClipboardTest-"+QUuid::createUuid().toString(QUuid::Id128)); app.setApplicationName("ClipboardTest");
    ClipboardTest test; return QTest::qExec(&test,argc,argv);
}
#include "test_clipboard.moc"
