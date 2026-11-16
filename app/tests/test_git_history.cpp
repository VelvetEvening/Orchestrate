#include "git/gitreader.h"
#include "pages/githistorywindow.h"
#include "pages/gitcommitdialog.h"
#include "pages/projectspage.h"
#include "pages/repositorybinding.h"
#include "data/appdatabase.h"
#include "ui/theme.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QThread>
#include <QScopeGuard>
#include <cstdio>

class GitHistoryTest final : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    QString repo, remote, first, merge, head;
    QByteArray originalPath;
    QByteArray execute(const QString &path, const QStringList &args, const QByteArray &input={}) {
        QProcess process; process.setProgram("git"); process.setArguments(QStringList{"-C",path}+args);
        process.start(); if(!process.waitForStarted())qFatal("git failed to start");
        process.write(input); process.closeWriteChannel();
        if(!process.waitForFinished(15000)||process.exitCode()!=0) qFatal("git command failed: %s / %s",qPrintable(args.join(' ')),process.readAllStandardError().constData());
        return process.readAllStandardOutput();
    }
    void write(const QString &path,const QByteArray &data) { QFile f(path);if(!f.open(QIODevice::WriteOnly))qFatal("write failed");f.write(data); }
    Git::Result read(const QStringList &args,qint64 limit=16*1024*1024,const QByteArray &input={}) {
        Git::Reader reader; Git::Result result; bool done=false;
        reader.run({"local",{},{},repo},args,[&](Git::Result r){result=r;done=true;},limit,input);
        QElapsedTimer timer;timer.start();while(!done&&timer.elapsed()<10000)QTest::qWait(10);
        if(!done)qFatal("reader did not finish");
        return result;
    }
    QMap<QString,QByteArray> repositoryState() {
        QMap<QString,QByteArray> result;
        QDirIterator it(repo,QDir::Files|QDir::Hidden,QDirIterator::Subdirectories);
        while(it.hasNext()){const auto path=it.next();QFile f(path);if(f.open(QIODevice::ReadOnly))result.insert(path,QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256));}
        return result;
    }
    bool sql(const QStringList &statements) {
        bool ok=true;const auto name=QStringLiteral("git-schema-fixture");
        {auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(QCoreApplication::applicationDirPath()+"/data/orchestrate.sqlite3");ok=db.open();
         for(const auto &statement:statements){QSqlQuery q(db);if(!q.exec(statement)){qWarning()<<q.lastError();ok=false;break;}}}
        QSqlDatabase::removeDatabase(name);return ok;
    }
private slots:
    void initTestCase() {
        QVERIFY(root.isValid());originalPath=qgetenv("PATH");
        QStandardPaths::setTestModeEnabled(true);QCoreApplication::setOrganizationName("OrchestrateGitTest");QCoreApplication::setApplicationName("GitHistoryTest");
        QVERIFY(QCoreApplication::applicationDirPath().endsWith("git-history-test"));
        repo=root.filePath(QStringLiteral("仓库 space ' $ & [x]"));remote=root.filePath("remote.git");QVERIFY(QDir().mkpath(repo));
        execute(repo,{"init","-b","main"});execute(repo,{"config","user.name",QStringLiteral("测试作者")});execute(repo,{"config","user.email","test@example.invalid"});
        write(repo+"/hello.txt","hello\n");write(repo+"/binary.dat",QByteArray("a\0b",3));execute(repo,{"add","."});execute(repo,{"commit","-m",QStringLiteral("初始提交"),"-m",QStringLiteral("完整说明\n保留第二行")});first=QString::fromLatin1(execute(repo,{"rev-parse","HEAD"})).trimmed();
        execute(repo,{"checkout","-b","feature/branch"});write(repo+"/feature.txt","feature\n");execute(repo,{"add","."});execute(repo,{"commit","-m","Feature commit"});
        execute(repo,{"checkout","main"});execute(repo,{"mv","hello.txt",QStringLiteral("改名 [a] #.txt")});execute(repo,{"commit","-m","Rename file"});
        execute(repo,{"merge","--no-ff","feature/branch","-m","Merge feature"});merge=QString::fromLatin1(execute(repo,{"rev-parse","HEAD"})).trimmed();
        execute(repo,{"tag","-a","v-test","-m","annotated tag"});
        // Generate a deep DAG without hundreds of worktree mutations.
        const auto tree=QString::fromLatin1(execute(repo,{"rev-parse","HEAD^{tree}"})).trimmed();QString parent=merge;
        QByteArray fastImport;
        for(int i=0;i<205;++i){const QByteArray message="Page commit "+QByteArray::number(i);fastImport+="commit refs/heads/main\nmark :"+QByteArray::number(i+1)+"\ncommitter Test <test@example.invalid> "+QByteArray::number(1700000000+i)+" +0000\ndata "+QByteArray::number(message.size())+"\n"+message+"\nfrom "+(i==0?merge.toLatin1():QByteArray(":")+QByteArray::number(i))+"\n\n";}
        execute(repo,{"fast-import","--quiet"},fastImport);head=QString::fromLatin1(execute(repo,{"rev-parse","HEAD"})).trimmed();
        execute(repo,{"clone","--bare",repo,remote});execute(repo,{"remote","add","origin",remote});
        execute(repo,{"branch","only-local",first});execute(remote,{"branch","only-remote",first});execute(remote,{"update-ref","refs/heads/main",merge});
        QByteArray refs;for(int i=0;i<150;++i)refs+="update refs/heads/shared-"+QByteArray::number(i)+" "+first.toLatin1()+"\n";
        execute(repo,{"update-ref","--stdin"},refs);execute(remote,{"update-ref","--stdin"},refs);
    }
    void historyAndDiffAreReadOnly() {
        const auto before=repositoryState();
        const auto result=read(Git::logArguments(0,201),16*1024*1024,head.toLatin1()+"\n"+
            first.toLatin1()+"\n");QVERIFY2(result.ok,qPrintable(result.error));
        QList<Git::Commit> commits;QVERIFY(Git::parseCommits(result.output,&commits));QCOMPARE(commits.size(),201);QCOMPARE(commits.first().id,head);
        const auto rest=read(Git::logArguments(200,201),16*1024*1024,head.toLatin1()+"\n");QVERIFY(rest.ok);QVERIFY(Git::parseCommits(rest.output,&commits));QCOMPARE(commits.size(),9);
        const auto rootFiles=read(Git::fileArguments(first,{}));QVERIFY(rootFiles.ok);QList<Git::FileChange> files;QVERIFY(Git::parseFiles(rootFiles.output,&files));QCOMPARE(files.size(),2);
        const auto mergeLog=read({"show","-s","--format=%P",merge});const auto parents=QString::fromLatin1(mergeLog.output).trimmed().split(' ');QCOMPARE(parents.size(),2);
        const auto diff=read(Git::fileArguments(merge,parents[0],true)+QStringList{"--","feature.txt"});QVERIFY(diff.ok);QVERIFY(diff.output.contains("+feature"));
        const auto binary=read(Git::fileArguments(first,{},true)+QStringList{"--","binary.dat"});QVERIFY(binary.ok);QVERIFY(binary.output.contains("Binary files"));
        const auto rename=read(Git::fileArguments(parents[0],first));QVERIFY(rename.ok);QVERIFY(Git::parseFiles(rename.output,&files));QCOMPARE(files.size(),1);QVERIFY(files[0].status.startsWith('R'));QCOMPARE(files[0].oldPath,QString("hello.txt"));QCOMPARE(files[0].path,QStringLiteral("改名 [a] #.txt"));
        const auto renameDiff=read(Git::fileArguments(parents[0],first,true)+QStringList{"--",files[0].oldPath,files[0].path});QVERIFY(renameDiff.ok);QVERIFY(renameDiff.output.contains("rename to"));
        QCOMPARE(repositoryState(),before);
    }
    void comparisonIncludesEveryBranchAndEmptyRemote() {
        const auto before=repositoryState();
        const auto result=read({"ls-remote","--heads","--refs","--","origin"},0);QVERIFY2(result.ok,qPrintable(result.error));
        QMap<QString,QString> remoteBranches;QVERIFY(Git::parseRemoteBranches(result.output,&remoteBranches));QCOMPARE(remoteBranches.size(),153);
        const auto refs=read({"for-each-ref","--format=%(refname)%00%(objectname)%00%(*objectname)%00%(objecttype)%00%(*objecttype)","refs/heads"},0);
        QList<Git::Reference> parsed;QVERIFY(refs.ok);QVERIFY(Git::parseReferences(refs.output,&parsed));QMap<QString,QString> local;for(const auto &ref:parsed)local[ref.name.mid(11)]=ref.id;
        const auto branches=Git::compareBranches(local,remoteBranches);QCOMPARE(branches.size(),154);
        QMap<Git::BranchState,int> counts;for(const auto &b:branches)++counts[b.state];
        QCOMPARE(counts[Git::BranchState::Different],1);QCOMPARE(counts[Git::BranchState::LocalOnly],1);QCOMPARE(counts[Git::BranchState::RemoteOnly],1);QCOMPARE(counts[Git::BranchState::Equal],151);
        const auto empty=root.filePath("empty.git");execute(repo,{"init","--bare",empty});const auto emptyResult=read({"ls-remote","--heads","--refs","--",empty},0);QVERIFY(emptyResult.ok);QVERIFY(emptyResult.output.isEmpty());
        QCOMPARE(repositoryState(),before);
    }
    void malformedDataIsRejected() {
        QMap<QString,QString> branches{{"keep",head}};QVERIFY(!Git::parseRemoteBranches("broken\trefs/heads/main\n",&branches));QVERIFY(branches.contains("keep"));
        QList<Git::Commit> commits;QVERIFY(!Git::parseCommits("partial\0data",&commits));
        QList<Git::FileChange> files;QVERIFY(!Git::parseFiles(QByteArray("R100\0old\0",9),&files));
        QString error;QVERIFY(!Git::Reader::validTarget({"ssh","-bad",{},"/tmp"},&error));QVERIFY(!Git::Reader::validTarget({"wsl","Ubuntu",{},"relative"},&error));
        const auto command=Git::Reader::shellCommand({"ssh","host",{},"~/a ' ; $(touch bad)"},{"log","--format=%H"});QVERIFY(command.contains("'\"'\"'"));QVERIFY(command.contains("exec git"));
        const auto capped=read({"log","--all","--format=%B"},8);QVERIFY(!capped.ok);QVERIFY(capped.error.contains(QStringLiteral("过大")));
    }
    void subfolderAndLiteralPaths() {
        const auto previous=repo;const auto nested=repo+"/nested";QVERIFY(QDir().mkpath(nested));
        const auto parents=QString::fromLatin1(execute(repo,{"show","-s","--format=%P",merge})).trimmed().split(' ');
        repo=nested;
        auto result=read(Git::fileArguments(parents[0],first,true)+QStringList{"--",Git::literalFilePath("hello.txt"),Git::literalFilePath(QStringLiteral("改名 [a] #.txt"))});
        repo=previous;QVERIFY2(result.ok,qPrintable(result.error));QVERIFY(result.output.contains("rename to"));
        const auto empty=read(Git::fileArguments(parents[0],first,true)+QStringList{"--",Git::literalFilePath("*.txt")});QVERIFY(empty.ok);QVERIFY(empty.output.isEmpty());
    }
    void windowPagingComparisonAndFailureRetention() {
        auto *window=new GitHistoryWindow(QStringLiteral("Git 功能验收"),{"local",{},{},repo});window->show();
        auto *table=window->findChild<QTableView*>("gitCommits");auto *refresh=window->findChild<QPushButton*>("gitRefresh");auto *more=window->findChild<QPushButton*>("gitLoadMore");
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(),200,10000);QVERIFY(more->isEnabled());
        QCOMPARE(window->findChild<QLabel*>("gitStatus")->text(),QStringLiteral("读取成功"));
        QVERIFY(!window->findChild<QPlainTextEdit*>("gitCommitMessage"));
        QTest::mouseClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualRect(table->model()->index(1,1)).center());
        QVERIFY(!window->findChild<GitCommitDialog*>());
        more->click();QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(),209,10000);QVERIFY(!more->isEnabled());
        auto *combo=window->findChild<QComboBox*>("gitBranch");combo->setCurrentIndex(combo->findData("refs/heads/feature/branch"));QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(),2,10000);
        combo->setCurrentIndex(0);QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(),200,10000);
        auto *tabs=window->findChild<QTabWidget*>();tabs->setCurrentIndex(1);
        auto *branches=window->findChild<QTableWidget*>("gitBranchComparison");QTRY_COMPARE_WITH_TIMEOUT(branches->rowCount(),3,10000);
        for(auto *check:window->findChildren<QCheckBox*>())if(check->text()==QStringLiteral("只看差异"))check->setChecked(false);
        QCOMPARE(branches->rowCount(),154);
        window->grab().save(QCoreApplication::applicationDirPath()+"/comparison.png");
        execute(repo,{"remote","set-url","origin",root.filePath("nonexistent-remote.git")});window->findChild<QPushButton*>("gitCompareRefresh")->click();
        QTRY_VERIFY_WITH_TIMEOUT(window->findChild<QLabel*>("gitCompareStatus")->text().contains(QStringLiteral("保留")),10000);QCOMPARE(branches->rowCount(),154);
        execute(repo,{"remote","set-url","origin",remote});
        tabs->setCurrentIndex(0);
        auto *tags=window->findChild<QComboBox*>("gitTag");QVERIFY(tags);QCOMPARE(combo->findData("refs/tags/v-test"),-1);QCOMPARE(tags->findData("refs/heads/main"),-1);
        tags->setCurrentIndex(tags->findData("refs/tags/v-test"));QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(),4,10000);QCOMPARE(combo->currentIndex(),-1);
        combo->setCurrentIndex(0);QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(),200,10000);QCOMPARE(tags->currentIndex(),0);
        tags->setCurrentIndex(tags->findData("refs/tags/v-test"));QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(),4,10000);
        QVERIFY(!window->findChild<GitCommitDialog*>());
        table->setCurrentIndex(table->model()->index(0,1));
        QTest::mouseClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualRect(table->currentIndex()).center());
        QTest::mouseDClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualRect(table->currentIndex()).center());
        auto *detail=window->findChild<GitCommitDialog*>();QVERIFY(detail);QVERIFY(!detail->isModal());
        QTRY_VERIFY_WITH_TIMEOUT(detail->findChild<QListWidget*>("gitFiles")->count()>0,10000);
        QTRY_VERIFY_WITH_TIMEOUT(detail->findChild<QPlainTextEdit*>("gitDiff")->toPlainText().contains("+feature"),10000);
        auto *message=detail->findChild<QPlainTextEdit*>("gitCommitMessage");QTRY_VERIFY(message->toPlainText().contains(merge));
        const auto pinned=message->toPlainText();table->setCurrentIndex(table->model()->index(1,1));QCOMPARE(message->toPlainText(),pinned);
        window->activateWindow();table->setFocus();QTest::qWait(50);QTest::keyClick(table,Qt::Key_Return);
        QCOMPARE(window->findChildren<GitCommitDialog*>().size(),1);QTRY_VERIFY(message->toPlainText()!=pinned);
        QTRY_VERIFY_WITH_TIMEOUT(detail->findChild<QPlainTextEdit*>("gitDiff")->toPlainText().contains("+feature"),10000);
        QTRY_VERIFY(message->toPlainText().contains(QStringLiteral("测试作者")));
        detail->grab().save(QCoreApplication::applicationDirPath()+"/commit-details.png");
        QPointer<GitCommitDialog> detailGuard=detail;detail->close();QTRY_VERIFY(detailGuard.isNull());
        window->grab().save(QCoreApplication::applicationDirPath()+"/history.png");window->resize(800,560);QTest::qWait(50);window->grab().save(QCoreApplication::applicationDirPath()+"/history-compact.png");
        const int rows=table->model()->rowCount();QVERIFY(QDir(repo).rename(".git",".git-hidden"));refresh->click();QTRY_VERIFY_WITH_TIMEOUT(refresh->isEnabled(),10000);QCOMPARE(table->model()->rowCount(),rows);QVERIFY(QDir(repo).rename(".git-hidden",".git"));
        window->activateWindow();table->setFocus();QTest::qWait(50);QTest::keyClick(table,Qt::Key_Return);detailGuard=window->findChild<GitCommitDialog*>();QVERIFY(detailGuard);
        QPointer<GitHistoryWindow> guard=window;window->close();QTRY_VERIFY(guard.isNull());QVERIFY(detailGuard.isNull());
    }
    void emptyAndNonRepository() {
        const auto empty=root.filePath("empty-worktree");QVERIFY(QDir().mkpath(empty));execute(empty,{"init","-b","main"});
        auto *window=new GitHistoryWindow("Empty",{"local",{},{},empty});window->show();
        QTRY_VERIFY_WITH_TIMEOUT(window->findChild<QLabel*>("gitStatus")->text().contains(QStringLiteral("尚无提交")),10000);window->close();
        window=new GitHistoryWindow("Ordinary directory",{"local",{},{},root.path()});window->show();
        QTRY_VERIFY_WITH_TIMEOUT(window->findChild<QLabel*>("gitStatus")->text().contains(QStringLiteral("无法读取")),10000);window->close();
    }
    void migrationAndWindowOwnership() {
        const auto data=QCoreApplication::applicationDirPath()+"/data";QVERIFY(QDir(data).removeRecursively());
        {AppDatabase db;QString error;QVERIFY2(db.open(&error),qPrintable(error));int id;QVERIFY(db.addProject("Existing",repo,&id));AppDatabase::WorkRecord r;r.projectId=id;r.title="Keep";r.content="body";r.summary="summary";QVERIFY(db.saveWorkRecord(&r));}
        QVERIFY(sql({"ALTER TABLE projects ADD COLUMN outline TEXT NOT NULL DEFAULT ''","UPDATE projects SET outline='discard by design'","ALTER TABLE projects DROP COLUMN target_type","ALTER TABLE projects DROP COLUMN target_name","ALTER TABLE projects DROP COLUMN target_user","ALTER TABLE automation_tools DROP COLUMN customized","ALTER TABLE automation_tools DROP COLUMN environment","ALTER TABLE tool_commands DROP COLUMN enabled","ALTER TABLE tool_commands DROP COLUMN position","PRAGMA user_version=7"}));
        AppDatabase db;QString error;QVERIFY2(db.open(&error),qPrintable(error));QCOMPARE(db.projects().size(),1);auto project=db.projects().first();QCOMPARE(project.targetType,QString("local"));QCOMPARE(db.workRecords(project.id).size(),1);
        QVERIFY(sql({"SELECT target_type,target_name,target_user FROM projects"}));
        const auto backups=QDir(data+"/backups").entryList({"schema-7-*.sqlite3"});QCOMPARE(backups.size(),1);
        project.targetType="ssh";project.targetName="example-alias";project.targetUser="alice";project.directory="/srv/project";QVERIFY(db.updateProject(project));
        {AppDatabase reopened;QVERIFY(reopened.open());QCOMPARE(reopened.projects().first().repository(),project.repository());}
        project.targetType="local";project.targetName.clear();project.targetUser.clear();project.directory=repo;QVERIFY(db.updateProject(project));
        int second=0;QVERIFY(db.addProject("Second",root.path(),&second));ProjectsPage page(&db);page.show();
        auto *list=page.findChild<QListWidget*>("projectList");for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole).toInt()==project.id)list->setCurrentRow(i);
        auto *button=page.findChild<QPushButton*>("gitHistoryButton");QVERIFY(button);QTRY_VERIFY(button->isEnabled());button->click();button->click();QCOMPARE(page.findChildren<GitHistoryWindow*>().size(),1);auto *window=page.findChild<GitHistoryWindow*>();
        for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole).toInt()==second)list->setCurrentRow(i);
        QCOMPARE(window->target().directory,repo);QVERIFY(window->windowTitle().contains("Existing"));QVERIFY(!page.findChild<QWidget*>("outlineWindow"));
        QTRY_VERIFY(button->toolTip().contains(QStringLiteral("不是 Git 仓库")));
        button->click();QCOMPARE(page.findChildren<GitHistoryWindow*>().size(),1);
        QVERIFY(db.updateProject(second,"Second",remote));
        list->currentRowChanged(list->currentRow());
        QTRY_VERIFY(button->isEnabled());button->click();QCOMPARE(page.findChildren<GitHistoryWindow*>().size(),2);
        RepositoryBinding binding(project.repository());binding.resize(600,330);binding.show();QTest::qWait(20);binding.grab().save(QCoreApplication::applicationDirPath()+"/binding.png");
    }
    void projectEntryAvailability() {
        AppDatabase db; QVERIFY(db.open());
        QList<int> ids;
        const auto clean=qScopeGuard([&]{qputenv("PATH",originalPath);for(int id:ids)db.deleteProject(id);});
        const auto ordinary=root.filePath("ordinary-project");QVERIFY(QDir().mkpath(ordinary));
        const auto nested=repo+"/entry-subdirectory";QVERIFY(QDir().mkpath(nested));
        const auto empty=root.filePath("entry-empty-repo");execute(repo,{"init",empty});
        const auto worktree=root.filePath("entry-worktree");execute(repo,{"worktree","add","--detach",worktree,"HEAD"});
        QVERIFY(QFileInfo(worktree+"/.git").isFile());
        for(const auto &path:QStringList{repo,ordinary,root.filePath("missing-project"),QString(),nested,empty,worktree,remote}) {
            int id=0;QVERIFY(db.addProject(QStringLiteral("入口验收 %1").arg(ids.size()),path,&id));ids.append(id);
        }
        QVERIFY(db.setSetting("projects.last_selected",QString::number(ids.first())));
        ProjectsPage page(&db);page.resize(1180,760);page.show();
        auto *list=page.findChild<QListWidget*>("projectList");auto *button=page.findChild<QPushButton*>("gitHistoryButton");
        QVERIFY(list&&button);
        const auto select=[&](int index) {
            for(int row=0;row<list->count();++row)if(list->item(row)->data(Qt::UserRole).toInt()==ids[index]) {list->setCurrentRow(row);return;}
            qFatal("missing project fixture");
        };
        QTRY_VERIFY(button->isEnabled());
        QVERIFY(button->toolTip().contains(QStringLiteral("已确认 Git 仓库")));
        page.grab().save(QCoreApplication::applicationDirPath()+"/v3.6.3-git-available.png");
        select(1);QVERIFY(!button->isEnabled());QTRY_VERIFY(button->toolTip().contains(QStringLiteral("不是 Git 仓库")));
        page.grab().save(QCoreApplication::applicationDirPath()+"/v3.6.3-git-directory.png");
        button->click();QVERIFY(page.findChildren<GitHistoryWindow*>().isEmpty());
        select(2);QTRY_VERIFY(button->toolTip().contains(QStringLiteral("暂时无法确认")));QVERIFY(!button->isEnabled());
        QVERIFY(!button->toolTip().contains(QStringLiteral("当前目录不是")));
        select(3);QVERIFY(!button->isEnabled());QVERIFY(button->toolTip().contains(QStringLiteral("尚未设置")));
        for(int index:{4,5,6,7}){select(index);QTRY_VERIFY(button->isEnabled());}
        // Binding edits must invalidate the old availability even on the same row.
        QVERIFY(db.updateProject(ids[7],"Changed binding",ordinary));
        list->currentRowChanged(list->currentRow());QVERIFY(!button->isEnabled());
        QTRY_VERIFY(button->toolTip().contains(QStringLiteral("不是 Git 仓库")));
        QVERIFY(db.updateProject(ids[7],"Changed binding",remote));
        list->currentRowChanged(list->currentRow());QTRY_VERIFY(button->isEnabled());
        // Missing Git is unknown, not an ordinary directory; clicking the same
        // selected row retries after the environment has recovered.
        qputenv("PATH",QDir::toNativeSeparators(root.path()).toUtf8());
        select(0);QTRY_VERIFY(button->toolTip().contains(QStringLiteral("暂时无法确认")));QVERIFY(!button->isEnabled());
        QVERIFY(!button->toolTip().contains(QStringLiteral("当前目录不是")));
        qputenv("PATH",originalPath);list->itemClicked(list->currentItem());QTRY_VERIFY(button->isEnabled());
        select(1);QTRY_VERIFY(button->toolTip().contains(QStringLiteral("不是 Git 仓库")));
        execute(repo,{"init",ordinary});list->itemClicked(list->currentItem());QTRY_VERIFY(button->isEnabled());
        // A delayed successful result must not re-enable a different/empty project.
        const auto helper=root.filePath("availability-helper");QVERIFY(QDir().mkpath(helper));
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),helper+"/git.exe"));
        qputenv("PATH",QDir::toNativeSeparators(helper).toUtf8()+";"+originalPath);
        select(0);QTest::qWait(50);QVERIFY(!button->isEnabled());
        select(3);QTest::qWait(350);QVERIFY(!button->isEnabled());
        QVERIFY(button->toolTip().contains(QStringLiteral("尚未设置")));
        select(0);QTest::qWait(50);page.hide();QTest::qWait(350);QVERIFY(!button->isEnabled());
        qputenv("PATH",originalPath);page.show();QTRY_VERIFY(button->isEnabled());
        // The same asynchronous command is used over SSH; the transport fixture
        // fails without making any real network connection.
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),helper+"/ssh.exe"));
        qputenv("PATH",QDir::toNativeSeparators(helper).toUtf8()+";"+originalPath);
        auto project=db.projects().first();for(const auto &p:db.projects())if(p.id==ids[0])project=p;
        project.targetType="ssh";project.targetName="availability-fixture";project.directory="/ordinary";QVERIFY(db.updateProject(project));
        list->currentRowChanged(list->currentRow());QTRY_VERIFY(button->toolTip().contains(QStringLiteral("不是 Git 仓库")));
        project.directory="/offline";QVERIFY(db.updateProject(project));list->currentRowChanged(list->currentRow());
        QTRY_VERIFY(button->toolTip().contains(QStringLiteral("暂时无法确认")));QVERIFY(!button->isEnabled());
        QVERIFY(!button->toolTip().contains(QStringLiteral("当前目录不是")));
        project.directory="/repository";QVERIFY(db.updateProject(project));list->currentRowChanged(list->currentRow());QTRY_VERIFY(button->isEnabled());
    }
    void cancellationNeverCallsDestroyedOwner() {
        const auto helper=root.filePath("git-reader-test-fixture");QVERIFY(QDir().mkpath(helper));QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),helper+"/git.exe"));
        qputenv("PATH",QDir::toNativeSeparators(helper).toUtf8()+";"+originalPath);
        bool called=false;auto *owner=new QWidget;auto *reader=new Git::Reader(owner);
        reader->run({"local",{},{},repo},{"slow-test"},[&](Git::Result){called=true;});QTest::qWait(100);delete owner;QTest::qWait(100);QVERIFY(!called);
        Git::Reader replacement;replacement.run({"local",{},{},repo},{"slow-test"},[&](Git::Result){called=true;});QTest::qWait(50);replacement.cancel();QTest::qWait(50);QVERIFY(!called);
        Git::Result result;bool done=false;
        replacement.run({"local",{},{},repo},{"large-test"},[&](Git::Result r){result=r;done=true;},0);
        QTRY_VERIFY_WITH_TIMEOUT(done,10000);QVERIFY(result.ok);QVERIFY(result.output.size()>1024*1024);
        QMap<QString,QString> branches;QVERIFY(Git::parseRemoteBranches(result.output,&branches));QCOMPARE(branches.size(),18000);
        qputenv("PATH",originalPath);
    }
    void sshTransportQuotesPathsAndResolvesHome() {
        const auto shell=QStringLiteral("E:/Tools/Git/bin/bash.exe");
        if(!QFileInfo::exists(shell))QSKIP("Git Bash unavailable");
        const auto helper=root.filePath("ssh-reader-test-fixture");QVERIFY(QDir().mkpath(helper));QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),helper+"/ssh.exe"));
        qputenv("PATH",QDir::toNativeSeparators(helper).toUtf8()+";"+originalPath);qputenv("ORCHESTRATE_GIT_TEST_SHELL",shell.toUtf8());
        auto path=QDir::fromNativeSeparators(repo);path="/"+path.left(1).toLower()+path.mid(2);
        RepositoryTarget target{"ssh","fixture-alias",{},path};Git::Reader reader;bool done=false;Git::Result result;
        reader.probe(target,[&](Git::Result r){result=r;done=true;});QTRY_VERIFY_WITH_TIMEOUT(done,10000);QVERIFY2(result.ok,qPrintable(result.error));QCOMPARE(result.output.split('\0').size(),3);QVERIFY(result.output.endsWith("git"));
        done=false;reader.run(target,Git::logArguments(0,2),[&](Git::Result r){result=r;done=true;},16*1024*1024,head.toLatin1()+"\n");QTRY_VERIFY_WITH_TIMEOUT(done,10000);QVERIFY2(result.ok,qPrintable(result.error));QList<Git::Commit> commits;QVERIFY(Git::parseCommits(result.output,&commits));QCOMPARE(commits.size(),2);QCOMPARE(commits[0].id,head);
        done=false;reader.run(target,{"ls-remote","--heads","--refs","--","origin"},[&](Git::Result r){result=r;done=true;},0);QTRY_VERIFY_WITH_TIMEOUT(done,10000);QVERIFY2(result.ok,qPrintable(result.error));
        QMap<QString,QString> branches;QVERIFY(Git::parseRemoteBranches(result.output,&branches));QCOMPARE(branches.size(),153);
        qputenv("PATH",originalPath);qunsetenv("ORCHESTRATE_GIT_TEST_SHELL");
    }
    void cleanupTestCase(){qputenv("PATH",originalPath);}
};

int main(int argc,char **argv) {
    const auto executable=QFileInfo(QString::fromLocal8Bit(argv[0])).completeBaseName();
    if(executable=="git") {
        if(QString::fromLocal8Bit(argv[argc-1])=="--absolute-git-dir") {
            QThread::msleep(300);std::fputs("/fixture/.git\n",stdout);return 0;
        }
        if(QString::fromLocal8Bit(argv[argc-1])=="large-test") {
            QByteArray output;for(int i=0;i<18000;++i)output+=QByteArray(40,'a')+"\trefs/heads/a-long-branch-name-for-complete-output-"+QByteArray::number(i)+"\n";
            std::fwrite(output.constData(),1,size_t(output.size()),stdout);return 0;
        }
        QThread::sleep(15);return 0;
    }
    if(executable=="ssh") {
        QCoreApplication app(argc,argv);const auto args=app.arguments();
        if(!args.contains("BatchMode=yes")||!args.contains("StrictHostKeyChecking=yes"))return 3;
        if(args.contains("availability-fixture")) {
            if(args.last().contains("/ordinary")){std::fputs("fatal: not a git repository (or any of the parent directories): .git\n",stderr);return 128;}
            if(args.last().contains("/offline")){std::fputs("ssh: Could not resolve hostname availability-fixture\n",stderr);return 255;}
            std::fputs("/repository/.git\n",stdout);return 0;
        }
        QProcess shell;shell.setProgram(qEnvironmentVariable("ORCHESTRATE_GIT_TEST_SHELL"));shell.setArguments({"--noprofile","--norc","-c",args.last()});
        shell.setProcessChannelMode(QProcess::ForwardedChannels);shell.setInputChannelMode(QProcess::ForwardedInputChannel);shell.start();if(!shell.waitForFinished(20000))return 4;return shell.exitCode();
    }
    QApplication app(argc,argv);
    for(const auto &font:{"msyh.ttc","msyhbd.ttc","segoeui.ttf","consola.ttf"})QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/")+font);
    app.setFont(QFont(QStringLiteral("Microsoft YaHei"),10));app.setStyle("Fusion");
    UiTheme::install(app);
    if(app.arguments().contains("--ui-review")) {
        auto *window=new GitHistoryWindow(QStringLiteral("Orchestrate 界面验收"),{"local",{},{},app.arguments().last()});window->show();
        QTimer::singleShot(200,window,[window]{ window->hide(); window->show(); window->raise(); window->activateWindow(); });return app.exec();
    }
    GitHistoryTest test;return QTest::qExec(&test,argc,argv);
}
#include "test_git_history.moc"
