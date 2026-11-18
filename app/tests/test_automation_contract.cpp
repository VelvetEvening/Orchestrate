#include "pages/automationpage.h"
#include "automation/commandarguments.h"
#include "automation/commandform.h"
#include "automation/toolstate.h"
#ifndef Q_MOC_RUN
#include "automation/toolstatereader.h"
#include "automation/wslprocess.h"
#include "automation/sshconnection.h"
#endif
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QScopeGuard>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QPlainTextEdit>
#include <QTimer>
#include <QFontDatabase>
#include <QInputDialog>
#include <QListWidget>
#include <QProcess>
#include <QThread>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

class AutomationContractTest : public QObject
{
    Q_OBJECT
    AppDatabase database_;
    bool registerFromDirectory(AutomationPage &page, const QString &target, const QString &host,
                               const QString &user, const QString &path, bool expectUpdate = false)
    {
        auto *button = page.findChild<QPushButton *>("registerTool");
        if (!button) return false;
        bool submitted = false, confirmed = false, expired = false;
        QElapsedTimer elapsed;
        QTimer drive;
        connect(&drive, &QTimer::timeout, &page, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            if (elapsed.elapsed() > 35000) {
                expired = true;
                dialog->reject();
                return;
            }
            if (expectUpdate && dialog->objectName() == "manifestUpdatePreview") {
                confirmed = true;
                dialog->accept();
                return;
            }
            if (submitted || dialog->objectName() != "toolDiscoveryDialog") return;
            auto *environment = dialog->findChild<QComboBox *>("directoryTarget");
            auto *location = dialog->findChild<QComboBox *>("directoryHost");
            auto *login = dialog->findChild<QLineEdit *>("directoryUser");
            auto *directory = dialog->findChild<QLineEdit *>("directoryPath");
            auto *save = dialog->findChild<QPushButton *>("saveConfiguration");
            if (!environment || !location || !login || !directory || !save) {
                dialog->reject();
                return;
            }
            environment->setCurrentIndex(environment->findData(target));
            location->setCurrentText(host);
            login->setText(user);
            directory->setText(path);
            submitted = true;
            save->click();
        });
        elapsed.start();
        drive.start(20);
        button->click();
        return submitted && !expired && confirmed == expectUpdate;
    }
    bool execSql(const QString &statement)
    {
        const QString name = QUuid::createUuid().toString();
        bool success = false;
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE", name);
            sql.setDatabaseName(database_.databasePath());
            if (sql.open()) {
                QSqlQuery query(sql);
                success = query.exec(statement);
                if (!success) qWarning() << query.lastError();
            }
        }
        QSqlDatabase::removeDatabase(name);
        return success;
    }
    QJsonObject validState(const QString &id = "example-adapter") const
    {
        return {{"schema", "orchestrate-state/v1"}, {"tool_id", id}, {"result", "success"},
                {"summary", "中文状态"}, {"updated_at", "2026-09-29T10:00:00+08:00"}, {"current_date", "2026-09-29"}};
    }
    QJsonObject basic() const
    {
        return {{"schema", "orchestrate-tool/v1"}, {"id", "example-adapter"}, {"name", "Example"},
                {"target_type", "windows-local"}, {"state_path", "state/current.json"}, {"commands", QJsonArray()}};
    }
    AppDatabase::ToolCommand accountCommand() const
    {
        AppDatabase::ToolCommand command;
        command.name = QStringLiteral("切换账号");
        command.description = QStringLiteral("选择已登记账号，恢复其绑定的环境。");
        command.executable = QCoreApplication::applicationFilePath();
        command.arguments = {"--helper-arguments", "--account={account}"};
        command.parameters = {
            QJsonObject{{"key", "current"}, {"label", QStringLiteral("当前账号")}, {"type", "readonly"}, {"state_key", "active_account"}},
            QJsonObject{{"key", "environment"}, {"label", QStringLiteral("绑定环境")}, {"type", "readonly"}, {"state_key", "environment.name"}},
            QJsonObject{{"key", "account"}, {"label", QStringLiteral("目标账号")}, {"type", "choice"}, {"required", true},
                {"options_state_key", "accounts"}, {"state_key", "active_account"}},
            QJsonObject{{"key", "note"}, {"label", QStringLiteral("备注")}, {"type", "string"}, {"state_key", "note"}}
        };
        return command;
    }
    QJsonObject accountState(const QString &active = "beta") const
    {
        auto state = validState();
        state["active_account"] = active;
        state["environment"] = QJsonObject{{"name", QStringLiteral("开发环境")}};
        state["note"] = "from state";
        state["accounts"] = QJsonArray{
            QJsonObject{{"value", "alpha"}, {"label", QStringLiteral("Alpha · 开发账号")}, {"description", QStringLiteral("绑定开发环境")}},
            QJsonObject{{"value", "beta"}, {"label", QStringLiteral("Beta · 测试账号")}, {"description", QStringLiteral("绑定测试环境")}}
        };
        return state;
    }
    bool parse(const QJsonObject &object, const QString &host = {})
    {
        AutomationPage page(&database_);
        AutomationPage::ParsedManifest manifest;
        QString error;
        const auto result = page.parseManifest(QJsonDocument(object).toJson(),
                                              host.isEmpty() ? QStringLiteral("D:/tools/example/orchestrate-tool.json")
                                                             : QStringLiteral("/orchestrate-tool.json"),
                                              host, &manifest, &error);
        if (!result) Q_ASSERT(!error.isEmpty());
        return result;
    }
private slots:
    void initTestCase()
    {
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyhbd.ttc"));
        QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
        QString error;
        QVERIFY2(database_.open(&error), qPrintable(error));
        QVERIFY(execSql("DELETE FROM automation_tools WHERE external_id IN ('wsl-identity-fixture', 'wsl-live-fixture', 'ssh-ui-fixture')"));
    }
    void documentedManifests()
    {
        AutomationPage page(&database_);
        const QDir root(QStringLiteral(ORCHESTRATE_SOURCE_DIR));
        for (const QString &relative : {QStringLiteral("docs/examples/tool-adapter/orchestrate-tool.json"),
                                       QStringLiteral("docs/examples/tool-adapter/orchestrate-tool.monitor.json"),
                                       QStringLiteral("docs/examples/tool-adapter/orchestrate-tool.ssh.json"),
                                       QStringLiteral("app/tools/scheduled-shutdown/orchestrate-tool.json")}) {
            QFile file(root.filePath(relative));
            QVERIFY(file.open(QIODevice::ReadOnly));
            AutomationPage::ParsedManifest manifest;
            QString error;
            const bool remote = relative.endsWith(QStringLiteral(".ssh.json"));
            QVERIFY2(page.parseManifest(file.readAll(), remote ? QStringLiteral("/opt/orchestrate-example/tool.json") : file.fileName(),
                                        remote ? QStringLiteral("example-host") : QString(), &manifest, &error), qPrintable(error));
            QVERIFY(!manifest.tool.externalId.isEmpty());
        }
    }
    void registrationOpensDiscoveryDialog()
    {
        AutomationPage page(&database_);auto *button=page.findChild<QPushButton*>("registerTool");QVERIFY(button);
        QCOMPARE(button->text(),QStringLiteral("接入工具"));QVERIFY(!button->menu());
        bool opened=false;QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);QCOMPARE(dialog->objectName(),QString("toolDiscoveryDialog"));auto *target=dialog->findChild<QComboBox*>("directoryTarget");
            QVERIFY(target);QCOMPARE(target->count(),3);target->setCurrentIndex(2);QVERIFY(dialog->findChild<QComboBox*>("directoryHost")->isVisible());opened=true;dialog->reject();});
        button->click();QVERIFY(opened);
    }
    void sshConfigAndUsers()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir(directory.path()).mkpath("conf.d"));
        const auto put = [&](const QString &name, const QByteArray &data) {
            QFile file(directory.filePath(name));
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(put("config", "# mock config only\nHost office other\n User alice\nInclude conf.d/*.conf\nHost * !blocked\n User fallback\nMatch exec \"DO-NOT-EXECUTE\"\n User unwanted\n"));
        QVERIFY(put("conf.d/01.conf", "Host gpu\n User=bob\nHost local-* !local-bad\n User carol\nHost blocked\n User david\nInclude config\n"));
        const auto config = SshConnection::Config::load(directory.filePath("config"));
        QCOMPARE(config.aliases, QStringList({"office", "other"}));
        // Included directives still supply user hints, but never picker entries.
        QCOMPARE(config.userHint("office"), QStringLiteral("alice"));
        QCOMPARE(config.userHint("gpu"), QStringLiteral("bob"));
        QCOMPARE(config.userHint("local-one"), QStringLiteral("carol"));
        QCOMPARE(config.userHint("local-bad"), QStringLiteral("fallback"));
        QCOMPARE(config.userHint("blocked"), QStringLiteral("david"));
        QVERIFY(!config.warnings.isEmpty()); // recursive include was bounded
        QCOMPARE(SshConnection::Config::words(QStringLiteral("\"C:\\Users\\Some Name\\.ssh\\parts.conf\" # comment")),
                 QStringList({QStringLiteral("C:\\Users\\Some Name\\.ssh\\parts.conf")}));
        QCOMPARE(SshConnection::Config::words(QStringLiteral("Host a b # comment")), QStringList({"Host", "a", "b"}));
        const auto args = SshConnection::arguments("office", "alice", "cat -- '/tool a' ");
        QCOMPARE(args[args.indexOf("-l") + 1], QStringLiteral("alice"));
        QCOMPARE(args.last(), QStringLiteral("cat -- '/tool a' "));
        QVERIFY(args.contains("StrictHostKeyChecking=yes"));
        QVERIFY(!SshConnection::arguments("office", {}, "true").contains("-l"));
        QVERIFY(!SshConnection::validHost("--help"));
        QVERIFY(!PosixRegistration::validUser("alice@host"));
        QCOMPARE(WslProcess::users("alice\nroot:x:0:0:root:/root:/bin/bash\nalice:x:1000:1000::/home/alice:/bin/bash\nbob:x:1001:1001::/home/bob:/bin/sh\nsvc:x:999:999::/:/usr/sbin/nologin\nnobody:x:65534:65534::/:/bin/sh\n"),
                 QStringList({"alice", "root", "bob"}));
    }
    void sshPickerOnlyListsDefaultConfigHosts()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto put = [&](const QString &name, const QByteArray &data) {
            QFile file(directory.filePath(name));
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(put("managed.conf", "Host gpu short\n User managed\n"));
        QVERIFY(put("config", "Include managed.conf\nHost jdcloud\n User alice\nHost bohrium\n User bob\nHost local-wsl\n User linux\nHost personal-host-with-a-long-name\n User owner\nHost * !excluded\n User fallback\n"));
        const auto config = SshConnection::Config::load(directory.filePath("config"));
        QCOMPARE(config.aliases, QStringList({"jdcloud", "bohrium", "local-wsl", "personal-host-with-a-long-name"}));
        QVERIFY(!config.aliases.contains("gpu")); // source, not name length, decides
        QVERIFY(!config.aliases.contains("short"));
        QCOMPARE(config.userHint("local-wsl"), QStringLiteral("linux"));
        QCOMPARE(config.userHint("gpu"), QStringLiteral("managed"));
        QCOMPARE(SshConnection::Config::load(directory.filePath("managed.conf")).aliases, QStringList({"gpu", "short"}));
    }
    void sshUserPersistence()
    {
        AutomationPage page(&database_);
        auto object = basic();
        object["target_type"] = "ssh";
        AutomationPage::ParsedManifest parsed;
        QString error;
        QVERIFY(page.parseManifest(QJsonDocument(object).toJson(), "/tool.json", "office", &parsed, &error, {}, {}, "alice"));
        QCOMPARE(parsed.tool.sshUser, QStringLiteral("alice"));
        QVERIFY(database_.saveAutomationTool(parsed.tool, {}, &parsed.tool.id));
        const auto cleanup = qScopeGuard([&] { database_.deleteAutomationTool(parsed.tool.id); });
        AppDatabase::AutomationTool saved;
        QVERIFY(page.findTool(parsed.tool.id, &saved));
        QCOMPARE(saved.sshUser, QStringLiteral("alice"));
        auto other = saved;
        other.sshUser = "bob";
        QVERIFY(!saved.sameExecutionTarget(other));
        QVERIFY(database_.updateAutomationTool(other));
        QVERIFY(page.findTool(other.id, &saved));
        QCOMPARE(saved.sshUser, QStringLiteral("bob"));
        page.stateReadsInFlight_.insert(saved.id);
        page.loadToolDetails(saved.id);
        QCOMPARE(page.toolSource_->text(), QStringLiteral("SSH"));
        QVERIFY(!page.toolSource_->toolTip().contains("office"));
        QVERIFY(!page.toolSource_->toolTip().contains("/tool.json"));
        QVERIFY(!page.toolSource_->isHidden());
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
        QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
        page.resize(1100, 820);
        page.show();
        QVERIFY(page.grab().save(QCoreApplication::applicationDirPath() + "/compact-ssh-details.png"));
        bool informationVisible = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            QStringList values;
            for (auto *label : dialog->findChildren<QLabel *>()) values << label->text();
            informationVisible = dialog->objectName() == QStringLiteral("toolInformationDialog")
                && values.contains("office") && values.contains("bob") && values.contains("/tool.json");
            informationVisible = informationVisible && dialog->grab().save(
                QCoreApplication::applicationDirPath() + "/tool-information.png");
            dialog->accept();
        });
        page.showToolInformation(saved.id);
        QVERIFY(informationVisible);
        QVERIFY(!page.parseManifest(QJsonDocument(object).toJson(), "/tool.json", "office", &parsed, &error, {}, {}, "--root"));
    }
    void sshRegistrationRoundTrip()
    {
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
        QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString mock = directory.filePath("ssh.exe");
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), mock));
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", QDir::toNativeSeparators(directory.path()).toUtf8() + ";" + oldPath);
        const auto restorePath = qScopeGuard([&] { qputenv("PATH", oldPath); });
        // The helper EXE below replaces SSH for this test; no server connection.
        AutomationPage page(&database_);
        QVERIFY(registerFromDirectory(page, "ssh", "fixture.invalid", "alice", "~/tool folder"));
        AppDatabase::AutomationTool tool;
        for (auto candidate : database_.automationTools()) if (candidate.externalId == "ssh-ui-fixture") tool = candidate;
        QVERIFY(tool.id > 0);
        const auto cleanup = qScopeGuard([&] { database_.deleteAutomationTool(tool.id); });
        QCOMPARE(tool.sshHost, QStringLiteral("fixture.invalid"));
        QCOMPARE(tool.sshUser, QStringLiteral("alice"));
        QTRY_VERIFY_WITH_TIMEOUT(!page.stateReadsInFlight_.contains(tool.id), 5000);
        QCOMPARE(page.stateCache_.value(tool.id).value("summary").toString(), QStringLiteral("alice"));
        page.runCommand(page.commandList_->item(0));
        QTRY_VERIFY_WITH_TIMEOUT(!page.runRecords_.value(tool.id).running, 5000);
        QVERIFY(!page.runRecords_.value(tool.id).failed);
        QVERIFY(page.runRecords_.value(tool.id).output.contains("MOCK USER=alice"));
        QTRY_VERIFY_WITH_TIMEOUT(!page.stateReadsInFlight_.contains(tool.id), 5000);
        QVERIFY(registerFromDirectory(page, "ssh", "fixture.invalid", "alice", "~/other folder", true));
        QList<int> matchingTools;
        for (const auto &saved : database_.automationTools())
            if (saved.externalId == tool.externalId && saved.sameExecutionTarget(tool)) matchingTools.append(saved.id);
        QCOMPARE(matchingTools, QList<int>{tool.id});
        QCOMPARE(database_.toolCommands(tool.id).size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!page.stateReadsInFlight_.contains(tool.id), 5000);
        QTimer previewTimer;
        connect(&previewTimer, &QTimer::timeout, &page, [&] {
            if (auto *preview = page.findChild<QDialog *>("manifestUpdatePreview")) preview->accept();
        });
        previewTimer.start(10);
        page.reloadManifest();
        QTRY_VERIFY_WITH_TIMEOUT(!page.manifestReadsInFlight_.contains(tool.id), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!page.stateReadsInFlight_.contains(tool.id), 5000);
        QCOMPARE(page.commandList_->count(), 1);
        AppDatabase::AutomationTool reread;
        QVERIFY(page.findTool(tool.id, &reread));
        QCOMPARE(reread.sshUser, QStringLiteral("alice"));
        QCOMPARE(reread.registrationPath, QStringLiteral("/srv/moved/orchestrate-tool.json"));
    }
    void shutdownHasOnlyFourCommands()
    {
        QFile file(QStringLiteral(ORCHESTRATE_SOURCE_DIR) + "/app/tools/scheduled-shutdown/orchestrate-tool.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto commands = QJsonDocument::fromJson(file.readAll()).object().value("commands").toArray();
        QStringList names, actions;
        for (auto value : commands) {
            const auto command = value.toObject();
            names << command.value("name").toString();
            const auto args = command.value("arguments").toArray().toVariantList();
            actions << args.value(args.indexOf("-Action") + 1).toString();
        }
        QCOMPARE(names, QStringList({QStringLiteral("开启定时关机"), QStringLiteral("关闭定时关机"),
                 QStringLiteral("设置弹窗时间"), QStringLiteral("设置弹窗持续时间")}));
        QCOMPARE(actions, QStringList({"enable", "disable", "set-time", "set-countdown"}));
    }
    void wslArgumentsAndDiscovery()
    {
        const QStringList payload {"", "中文 空格", "a\"b", "a'b", "$HOME", "$(touch x); & |", "line\nnext", "\\"};
        const auto args = WslProcess::arguments("Ubuntu", "alice", "/tmp/tool name", payload, "/tmp/工作 dir");
        QCOMPARE(args.mid(0, 8), QStringList({"--distribution", "Ubuntu", "--user", "alice", "--cd", "/tmp/工作 dir", "--exec", "/tmp/tool name"}));
        QCOMPARE(args.mid(8), payload);
        QCOMPARE(WslProcess::arguments("Ubuntu", {}, "cat").mid(0, 6),
                 QStringList({"--distribution", "Ubuntu", "--cd", "/", "--exec", "cat"}));
        QByteArray utf16 = QByteArray::fromHex("fffe");
        for (const auto c : QStringLiteral("Ubuntu-24.04\r\ndocker-desktop\r\n")) {
            utf16.append(char(c.unicode() & 255)); utf16.append(char(c.unicode() >> 8));
        }
        QCOMPARE(WslProcess::distributions(utf16), QStringList({"Ubuntu-24.04", "docker-desktop"}));
        QCOMPARE(WslProcess::distributions("Ubuntu\nUbuntu\n\n"), QStringList({"Ubuntu"}));
        QVERIFY(!WslProcess::validName("--exec"));
        QVERIFY(WslProcess::validDistribution("My Ubuntu 中文"));
        QCOMPARE(WslProcess::distributions(QStringLiteral("My Ubuntu 中文\r\n").toUtf8()), QStringList({QStringLiteral("My Ubuntu 中文")}));
    }
    void wslRegistrationIdentityAndPersistence()
    {
        AutomationPage page(&database_);
        auto object = basic();
        object["id"] = "wsl-identity-fixture";
        object["target_type"] = "wsl";
        const auto bytes = QJsonDocument(object).toJson();
        QList<AppDatabase::AutomationTool> fixtures;
        for (const auto &target : QList<QStringList>{{"Ubuntu", "alice"}, {"Ubuntu", "bob"}, {"Debian", "alice"}, {"", ""}}) {
            object["target_type"] = target.first().isEmpty() ? "windows-local" : "wsl";
            AutomationPage::ParsedManifest parsed;
            QString error;
            QVERIFY2(page.parseManifest(QJsonDocument(object).toJson(),
                target.first().isEmpty() ? "D:/tool/tool.json" : "/tool/tool.json", {}, &parsed, &error,
                target.first(), target.last()), qPrintable(error));
            QVERIFY(database_.saveAutomationTool(parsed.tool, {}, &parsed.tool.id));
            // Suppress external state I/O: this test only exercises registration identity.
            page.stateReadsInFlight_.insert(parsed.tool.id);
            fixtures << parsed.tool;
        }
        const auto cleanup = qScopeGuard([&] { for (const auto &tool : fixtures) database_.deleteAutomationTool(tool.id); });
        for (int i = 0; i < fixtures.size(); ++i)
            for (int j = 0; j < fixtures.size(); ++j)
                QCOMPARE(fixtures[i].sameExecutionTarget(fixtures[j]), i == j);
        int aliceId = 0;
        for (const auto &tool : fixtures) {
            if (tool.wslDistribution == "Ubuntu" && tool.wslUser == "alice") {
                aliceId = tool.id;
                QVERIFY(database_.updateToolRefreshSettings(tool.id, true, "interval", 600, "09:00"));
            }
        }
        QVERIFY(aliceId > 0);
        // Reload the selected identity and preserve its stored settings.
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box) box->button(QMessageBox::Yes)->click();
            else if (auto *preview = qobject_cast<QDialog *>(QApplication::activeModalWidget()); preview && preview->objectName() == "manifestUpdatePreview") preview->accept();
        });
        AppDatabase::AutomationTool current;
        QVERIFY(page.findTool(aliceId, &current));
        page.finishManifestReload(current, bytes);
        AppDatabase::AutomationTool updated;
        QVERIFY(page.findTool(aliceId, &updated));
        QCOMPARE(updated.registrationPath, QStringLiteral("/tool/tool.json"));
        QCOMPARE(updated.wslUser, QStringLiteral("alice"));
        QVERIFY(updated.refreshEnabled);
        QCOMPARE(updated.refreshIntervalSeconds, 600);
        page.loadToolDetails(aliceId);
        QCOMPARE(page.toolSource_->text(), QStringLiteral("WSL"));
        QVERIFY(!page.toolSource_->toolTip().contains("/tool/tool.json"));
        QVERIFY(!page.toolSource_->isHidden());
        bool informationVisible = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            QStringList values;
            for (auto *label : dialog->findChildren<QLabel *>()) values << label->text();
            informationVisible = values.contains("Ubuntu") && values.contains("alice")
                && values.contains("/tool/tool.json");
            dialog->accept();
        });
        page.showToolInformation(aliceId);
        QVERIFY(informationVisible);

    }
    void wslAsyncCaptureIsBounded()
    {
        for (const auto &mode : {"--helper-state", "--helper-hang", "--helper-flood", "missing"}) {
            QObject context;
            auto *process = new QProcess;
            QPointer<QProcess> guard(process);
            process->setProgram(QString(mode) == "missing" ? "Z:/missing-wsl-fixture.exe" : QCoreApplication::applicationFilePath());
            process->setArguments({mode});
            int callbacks = 0;
            bool success = false;
            QString error;
            QByteArray bytes;
            WslProcess::capture(&context, {}, [&](bool ok, const QByteArray &output, const QString &message) {
                ++callbacks; success = ok; bytes = output; error = message;
            }, QString(mode) == "--helper-hang" ? 100 : 4000, process);
            QTRY_COMPARE_WITH_TIMEOUT(callbacks, 1, 5000);
            QTRY_VERIFY(guard.isNull());
            QTRY_VERIFY(context.children().isEmpty());
            QCOMPARE(success, QString(mode) == "--helper-state");
            if (success) QVERIFY(bytes.contains("orchestrate-state/v1"));
            else QVERIFY2(!error.isEmpty(), mode);
        }
    }
    void processCaptureCancelsWhenOwnerIsDestroyed_data()
    {
        QTest::addColumn<int>("ownerType");
        QTest::newRow("object") << 0;
        QTest::newRow("widget") << 1;
        QTest::newRow("dialog") << 2;
    }
    void processCaptureCancelsWhenOwnerIsDestroyed()
    {
        QFETCH(int, ownerType);
        QObject *owner = ownerType == 0 ? new QObject
            : ownerType == 1 ? static_cast<QObject *>(new QWidget) : new QDialog;
        auto *process = new QProcess;
        QPointer<QProcess> guard(process);
        process->setProgram(QCoreApplication::applicationFilePath());
        process->setArguments({"--helper-hang"});
        int callbacks = 0;
        ProcessCapture::capture(owner, process,
            [&](bool, const QByteArray &, const QString &) { ++callbacks; }, "fixture");
        QTRY_COMPARE(process->state(), QProcess::Running);
        delete owner;
        QVERIFY(guard.isNull());
        QCOMPARE(callbacks, 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(callbacks, 0);
    }
    void closeFormDuringRemoteStateRead()
    {
        QPointer<QProcess> process;
        int callbacks = 0;
        auto *form = new CommandForm(accountCommand(), [&](QObject *context, CommandForm::StateCallback callback) {
            process = new QProcess;
            process->setProgram(QCoreApplication::applicationFilePath());
            process->setArguments({"--helper-hang"});
            ProcessCapture::capture(context, process,
                [&, callback](bool success, const QByteArray &, const QString &error) {
                    ++callbacks;
                    callback(success, {}, error);
                }, "fixture");
        });
        form->show();
        QTRY_VERIFY(!process.isNull());
        QTRY_COMPARE(process->state(), QProcess::Running);
        form->reject();
        delete form;
        QVERIFY(process.isNull());
        QCOMPARE(callbacks, 0);
    }
    void closePageDuringStateRead()
    {
        auto *page = new AutomationPage(&database_);
        AppDatabase::AutomationTool tool;
        tool.id = 987654;
        tool.targetType = "wsl";
        auto *process = new QProcess;
        QPointer<QProcess> guard(process);
        process->setProgram(QCoreApplication::applicationFilePath());
        process->setArguments({"--helper-hang"});
        page->startStateReadProcess(tool, false, process, 3000);
        QTRY_COMPARE(process->state(), QProcess::Running);
        delete page;
        QVERIFY(guard.isNull());
    }
    void wslLiveRoundTrip()
    {
        const QString distro = qEnvironmentVariable("ORCHESTRATE_WSL_TEST_DISTRO");
        if (distro.isEmpty()) QSKIP("Opt in with ORCHESTRATE_WSL_TEST_DISTRO; only synthetic temporary files are touched.");
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
        QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
        QByteArray output;
        QString error;
        const auto call = [&](const QString &executable, const QStringList &args, const QString &cwd = "/") {
            QEventLoop loop;
            bool success = false;
            WslProcess::capture(&loop, WslProcess::arguments(distro, {}, executable, args, cwd),
                [&](bool ok, const QByteArray &bytes, const QString &message) {
                    success = ok; output = bytes; error = message; loop.quit();
                });
            loop.exec();
            return success;
        };
        QVERIFY2(call("sh", {"-c", "mktemp -d \"$HOME/.orchestrate-wsl-test-XXXXXX\""}), qPrintable(error));
        const QString base = QString::fromUtf8(output).trimmed();
        QVERIFY(QRegularExpression(QStringLiteral("^/[^\r\n]+/\\.orchestrate-wsl-test-[A-Za-z0-9]{6}$")).match(base).hasMatch());
        // Exact freshly-created target is validated before any recursive cleanup.
        const auto cleanupFiles = qScopeGuard([&] { call("rm", {"-rf", "--", base}); });
        const QString directory = base + QStringLiteral("/中文 tool ' $;(test)");
        const QStringList payload {"", "中文 空格", "a\"b", "a'b", "$HOME", "$(touch nope); & |", "line\nnext", "\\", "--user"};
        auto object = basic();
        object["id"] = "wsl-live-fixture";
        object["target_type"] = "wsl";
        object["working_directory"] = "worker";
        QJsonArray arguments {"../run.py"};
        for (const auto &value : payload) arguments.append(value);
        object["commands"] = QJsonArray{QJsonObject{{"name", "Probe"}, {"executable", "python3"}, {"arguments", arguments}}};
        const QString writer = QString::fromLatin1(R"PY(import pathlib, sys
p = pathlib.Path(sys.argv[1])
(p / 'worker').mkdir(parents=True)
(p / 'state').mkdir()
(p / 'orchestrate-tool.json').write_text(sys.argv[2], encoding='utf-8')
(p / 'state' / 'current.json').write_text(sys.argv[3], encoding='utf-8')
(p / 'run.py').write_text("import os,sys,json\nprint(json.dumps({'cwd':os.getcwd(),'args':sys.argv[1:]},ensure_ascii=False))\n", encoding='utf-8')
)PY");
        auto liveState = accountState();
        liveState["tool_id"] = "wsl-live-fixture";
        QVERIFY2(call("python3", {"-c", writer, directory, QString::fromUtf8(QJsonDocument(object).toJson()),
                                 QString::fromUtf8(QJsonDocument(liveState).toJson())}), qPrintable(error));
        const QString tildePath = QStringLiteral("~/") + base.section(QLatin1Char('/'), -1) + QStringLiteral("/中文 tool ' $;(test)");
        QVERIFY2(call("id", {"-un"}), qPrintable(error));
        const auto user = QString::fromUtf8(output).trimmed();
        AutomationPage page(&database_);
        QVERIFY(registerFromDirectory(page, "wsl", distro, {}, tildePath));
        AppDatabase::AutomationTool tool;
        for (const auto &candidate : database_.automationTools())
            if (candidate.externalId == "wsl-live-fixture") tool = candidate;
        QVERIFY(tool.id > 0);
        const auto cleanupTool = qScopeGuard([&] { database_.deleteAutomationTool(tool.id); });
        QCOMPARE(tool.wslDistribution, distro);
        QCOMPARE(tool.wslUser, user);
        QCOMPARE(tool.registrationPath, directory + "/orchestrate-tool.json");
        QCOMPARE(tool.workingDirectory, directory + "/worker");
        QCOMPARE(tool.statePath, directory + "/state/current.json");
        QTRY_VERIFY_WITH_TIMEOUT(!page.stateReadsInFlight_.contains(tool.id), 35000);
        QVERIFY2(page.stateErrors_.value(tool.id).isEmpty(), qPrintable(page.stateErrors_.value(tool.id)));
        QCOMPARE(page.stateCache_.value(tool.id).value("summary").toString(), QStringLiteral("中文状态"));
        {
            CommandForm form(accountCommand(), [tool](QObject *context, CommandForm::StateCallback callback) {
                ToolStateReader::read(context, tool, std::move(callback));
            });
            form.show();
            QTRY_VERIFY_WITH_TIMEOUT(form.findChild<QPushButton *>("primaryButton")->isEnabled(), 35000);
            QCOMPARE(form.values().value("account"), QStringLiteral("beta"));
            QCOMPARE(form.findChild<QLabel *>("contextValue_current")->text(), QStringLiteral("beta"));
            form.reject();
        }
        QCOMPARE(page.commandList_->count(), 1);
        page.resize(1100, 820);
        page.show();
        page.grab().save(QCoreApplication::applicationDirPath() + "/wsl-tool-details.png");
        page.runCommand(page.commandList_->item(0));
        QTRY_VERIFY_WITH_TIMEOUT(!page.runRecords_.value(tool.id).running, 35000);
        QVERIFY2(!page.runRecords_.value(tool.id).failed, qPrintable(page.runRecords_.value(tool.id).output));
        const auto probe = QJsonDocument::fromJson(page.runRecords_.value(tool.id).output.trimmed().section(QLatin1Char('\n'), -1).toUtf8()).object();
        QCOMPARE(probe.value("cwd").toString(), directory + "/worker");
        QCOMPARE(probe.value("args").toArray(), QJsonArray::fromStringList(payload));
        QTRY_VERIFY_WITH_TIMEOUT(!page.stateReadsInFlight_.contains(tool.id), 35000);
        QTimer previewTimer;
        connect(&previewTimer, &QTimer::timeout, &page, [&] {
            if (auto *preview = page.findChild<QDialog *>("manifestUpdatePreview")) preview->accept();
        });
        previewTimer.start(10);
        page.reloadManifest();
        QTRY_VERIFY_WITH_TIMEOUT(!page.manifestReadsInFlight_.contains(tool.id), 35000);
        QCOMPARE(page.commandList_->count(), 1);
        QVERIFY(page.commandHint_->text().contains(QStringLiteral("已重新读取")));
        QTRY_VERIFY_WITH_TIMEOUT(!page.stateReadsInFlight_.contains(tool.id), 35000);
        // Missing files surface a failure; restoring the file allows a clean retry.
        QVERIFY2(call("mv", {"--", tool.statePath, tool.statePath + ".bak"}), qPrintable(error));
        page.refreshStateForTool(tool.id, true);
        QTRY_VERIFY_WITH_TIMEOUT(!page.stateReadsInFlight_.contains(tool.id), 35000);
        QVERIFY(!page.stateErrors_.value(tool.id).isEmpty());
        QVERIFY2(call("mv", {"--", tool.statePath + ".bak", tool.statePath}), qPrintable(error));
        page.refreshStateForTool(tool.id, true);
        QTRY_VERIFY_WITH_TIMEOUT(!page.stateReadsInFlight_.contains(tool.id), 35000);
        QVERIFY(page.stateErrors_.value(tool.id).isEmpty());
        QVERIFY2(call("python3", {"-c", "import pathlib,sys; assert not (pathlib.Path(sys.argv[1]) / 'worker' / 'nope').exists()", directory}), qPrintable(error));
    }
    void scalarIntegersKeepPrecision()
    {
        const auto state = QJsonDocument::fromJson(R"({"id":9007199254740993,"max":9223372036854775807,"min":-9223372036854775808})").object();
        QCOMPARE(CommandParameters::scalarText(state["id"]), QStringLiteral("9007199254740993"));
        QCOMPARE(CommandParameters::scalarText(state["max"]), QStringLiteral("9223372036854775807"));
        QCOMPARE(CommandParameters::scalarText(state["min"]), QStringLiteral("-9223372036854775808"));
        QCOMPARE(CommandParameters::scalarText(QJsonValue(1.25)), QStringLiteral("1.25"));
    }
    void staticStateChoiceKeepsValidSelectionOnRefresh()
    {
        AppDatabase::ToolCommand command;
        command.name = "State-backed static choice";
        command.parameters = QJsonArray{QJsonObject{{"key", "target"}, {"type", "choice"},
            {"state_key", "active"}, {"required", true}, {"options", QJsonArray{"alpha", "beta"}}}};
        CommandForm::StateCallback pending;
        CommandForm form(command, [&](QObject *, CommandForm::StateCallback callback) { pending = std::move(callback); });
        form.show();
        QTRY_VERIFY(bool(pending));
        pending(true, {{"active", "alpha"}}, {});
        QCOMPARE(form.values().value("target"), QStringLiteral("alpha"));
        form.findChild<QPushButton *>("commandStateRefresh")->click();
        pending(true, {{"active", "beta"}}, {});
        QCOMPARE(form.values().value("target"), QStringLiteral("alpha"));
    }
    void dynamicParameterContract()
    {
        auto object = basic();
        const auto command = accountCommand();
        object["commands"] = QJsonArray{QJsonObject{{"name", command.name}, {"executable", command.executable},
            {"arguments", QJsonArray::fromStringList(command.arguments)}, {"parameters", command.parameters}}};
        QVERIFY(parse(object));
        auto definition = command.parameters[2].toObject();
        const auto check = [&](const QJsonObject &parameter, bool expected) {
            auto candidate = object;
            auto item = candidate["commands"].toArray()[0].toObject();
            item["parameters"] = QJsonArray{parameter};
            candidate["commands"] = QJsonArray{item};
            return parse(candidate) == expected;
        };
        auto invalid = definition;
        invalid["options"] = QJsonArray{"static"};
        QVERIFY(check(invalid, false));
        for (const auto &path : QJsonArray{"", "accounts..all", ".accounts", "accounts.", true, 3}) {
            invalid = definition;
            invalid["options_state_key"] = path;
            QVERIFY(check(invalid, false));
        }
        invalid = definition;
        invalid["type"] = "string";
        QVERIFY(check(invalid, false));
        invalid = command.parameters[0].toObject();
        invalid.remove("state_key");
        QVERIFY(check(invalid, false));
        invalid = command.parameters[0].toObject();
        invalid["key"] = "account"; // readonly values cannot be interpolated into argv
        QVERIFY(check(invalid, false));
        invalid = {{"key", "static"}, {"type", "choice"}, {"options", QJsonArray{"safe", QJsonObject{{"value", true}, {"label", "yes"}}, 3}}};
        QVERIFY(check(invalid, true));
        QList<CommandParameters::Option> options;
        QString error;
        for (const auto &source : QJsonArray{QJsonValue(), "wrong", QJsonArray{"alpha"},
            QJsonArray{QJsonObject{{"value", "a"}}},
            QJsonArray{QJsonObject{{"value", "a"}, {"label", "A"}, {"description", true}}},
            QJsonArray{QJsonObject{{"value", "a"}, {"label", "A"}}, QJsonObject{{"value", "a"}, {"label", "Other"}}}}) {
            QVERIFY(!CommandParameters::readOptions(source, true, &options, &error));
            QVERIFY(options.isEmpty());
            QVERIFY(!error.isEmpty());
        }
        QJsonArray tooMany;
        for (int i = 0; i < 501; ++i) tooMany.append(QJsonObject{{"value", QString::number(i)}, {"label", "item"}});
        QVERIFY(!CommandParameters::readOptions(tooMany, true, &options, &error));
        QVERIFY(CommandParameters::readOptions(QJsonArray{}, true, &options, &error));
        QVERIFY(CommandParameters::readOptions(accountState()["accounts"], true, &options, &error));
        QCOMPARE(options.size(), 2);
        definition["default"] = "alpha";
        QVERIFY(CommandParameters::initialValue(definition, {}, true).isUndefined());
        QCOMPARE(CommandParameters::initialValue(definition, {{"active_account", QJsonValue()}}, true).type(), QJsonValue::Null);
    }
    void dynamicFormSnapshotAndValidation()
    {
        AutomationPage page(&database_); // inherit the real page's form styles
        CommandForm::StateCallback pending;
        int reads = 0;
        CommandForm form(accountCommand(), [&](QObject *, CommandForm::StateCallback callback) {
            ++reads;
            pending = std::move(callback);
        }, &page);
        form.show();
        auto *run = form.findChild<QPushButton *>("primaryButton");
        auto *refresh = form.findChild<QPushButton *>("commandStateRefresh");
        auto *combo = form.findChild<QComboBox *>("parameter_account");
        auto *note = form.findChild<QLineEdit *>("parameter_note");
        QVERIFY(run && refresh && combo && note);
        QTRY_COMPARE(reads, 1);
        QVERIFY(!run->isEnabled());
        QVERIFY(!combo->isEnabled());
        QVERIFY(!refresh->isEnabled());
        form.grab().save(QCoreApplication::applicationDirPath() + "/dynamic-loading.png");
        pending(true, accountState(), {});
        QCOMPARE(combo->currentData().toString(), QStringLiteral("beta"));
        QCOMPARE(note->text(), QStringLiteral("from state"));
        QCOMPARE(form.findChild<QLabel *>("contextValue_current")->text(), QStringLiteral("beta"));
        QCOMPARE(form.findChild<QLabel *>("contextValue_environment")->text(), QStringLiteral("开发环境"));
        QVERIFY(form.findChild<QLabel *>("choiceHint_account")->text().contains(QStringLiteral("测试环境")));
        QVERIFY(!form.values().contains("current"));
        QVERIFY(run->isEnabled());
        form.grab().save(QCoreApplication::applicationDirPath() + "/dynamic-ready.png");

        combo->setCurrentIndex(combo->findData("alpha"));
        note->selectAll();
        QTest::keyClicks(note, "my note");
        refresh->click();
        QCOMPARE(reads, 2);
        QVERIFY(!run->isEnabled());
        auto state = accountState();
        auto reordered = state["accounts"].toArray();
        reordered.prepend(QJsonObject{{"value", "gamma"}, {"label", "Gamma"}});
        state["accounts"] = reordered;
        state["note"] = "new state note";
        pending(true, state, {});
        QCOMPARE(combo->currentData().toString(), QStringLiteral("alpha"));
        QCOMPARE(note->text(), QStringLiteral("my note"));

        // Removing the selected option cannot silently switch to the first row.
        state["accounts"] = QJsonArray{QJsonObject{{"value", "gamma"}, {"label", "Gamma"}}};
        refresh->click();
        pending(true, state, {});
        QCOMPARE(combo->currentIndex(), -1);
        QVERIFY(form.findChild<QLabel *>("choiceHint_account")->text().contains(QStringLiteral("已失效")));
        form.grab().save(QCoreApplication::applicationDirPath() + "/dynamic-invalidated.png");
        run->click();
        QVERIFY(form.result() != QDialog::Accepted);
        QVERIFY(!form.findChild<QLabel *>("commandParameterError")->isHidden());
        combo->addItem("Injected", "not-in-snapshot");
        combo->setCurrentIndex(combo->count() - 1);
        run->click();
        QVERIFY(form.result() != QDialog::Accepted);
        combo->setCurrentIndex(combo->findData("gamma"));
        run->click();
        QCOMPARE(form.result(), int(QDialog::Accepted));
        QCOMPARE(form.values().value("account"), QStringLiteral("gamma"));
    }
    void dynamicFormFailureEmptyAndRecovery()
    {
        AutomationPage page(&database_);
        CommandForm::StateCallback pending;
        CommandForm form(accountCommand(), [&](QObject *, CommandForm::StateCallback callback) {
            pending = std::move(callback);
        }, &page);
        form.show();
        auto *run = form.findChild<QPushButton *>("primaryButton");
        auto *refresh = form.findChild<QPushButton *>("commandStateRefresh");
        auto *combo = form.findChild<QComboBox *>("parameter_account");
        QTRY_VERIFY(bool(pending));
        pending(true, accountState("gone"), {});
        QCOMPARE(combo->currentIndex(), -1);
        run->click();
        QVERIFY(form.result() != QDialog::Accepted);
        combo->setCurrentIndex(combo->findData("alpha"));
        refresh->click();
        pending(false, {}, QStringLiteral("连接失败，请重试"));
        QVERIFY(!run->isEnabled());
        QCOMPARE(combo->count(), 0);
        QCOMPARE(refresh->text(), QStringLiteral("重试"));
        form.grab().save(QCoreApplication::applicationDirPath() + "/dynamic-read-failed.png");
        refresh->click();
        pending(true, accountState(), {});
        QCOMPARE(combo->currentData().toString(), QStringLiteral("alpha"));
        QVERIFY(run->isEnabled());
        auto state = accountState();
        state["accounts"] = QJsonArray{};
        refresh->click();
        pending(true, state, {});
        QCOMPARE(combo->currentIndex(), -1);
        QVERIFY(!run->isEnabled());
        QCOMPARE(combo->placeholderText(), QStringLiteral("暂无可选项"));
        form.grab().save(QCoreApplication::applicationDirPath() + "/dynamic-empty.png");
        state["accounts"] = QJsonArray{QJsonObject{{"value", "a"}}};
        refresh->click();
        pending(true, state, {});
        QVERIFY(!run->isEnabled());
        QCOMPARE(combo->count(), 0);
        state = accountState();
        state["expires_at"] = "2026-09-29T10:00:01+08:00";
        refresh->click();
        pending(true, state, {});
        QVERIFY(!run->isEnabled());
        QVERIFY(form.findChild<QLabel *>("commandStateStatus")->text().contains(QStringLiteral("已过期")));
        refresh->click();
        pending(true, accountState(), {});
        QCOMPARE(combo->currentIndex(), -1); // the previously removed selection stays unset
        QVERIFY(run->isEnabled());
    }
    void staticChoiceAndRequiredFieldsStayCompatible()
    {
        AppDatabase::ToolCommand command;
        command.name = "Static";
        command.parameters = {
            QJsonObject{{"key", "mode"}, {"type", "choice"}, {"options", QJsonArray{"safe", "fast"}}},
            QJsonObject{{"key", "name"}, {"type", "string"}, {"required", true}},
            QJsonObject{{"key", "count"}, {"type", "integer"}, {"default", 3}},
            QJsonObject{{"key", "enabled"}, {"type", "boolean"}, {"default", true}, {"true_value", "--enabled"}}
        };
        bool read = false;
        CommandForm form(command, [&](QObject *, CommandForm::StateCallback) { read = true; });
        form.show();
        auto *run = form.findChild<QPushButton *>("primaryButton");
        QCOMPARE(form.values().value("mode"), QStringLiteral("safe"));
        run->click();
        QVERIFY(form.result() != QDialog::Accepted);
        form.findChild<QLineEdit *>("parameter_name")->setText(QStringLiteral(" 中文 "));
        run->click();
        QCOMPARE(form.result(), int(QDialog::Accepted));
        QCOMPARE(form.values().value("name"), QStringLiteral("中文"));
        QCOMPARE(form.values().value("count"), QStringLiteral("3"));
        QCOMPARE(form.values().value("enabled"), QStringLiteral("--enabled"));
        QVERIFY(!read);

        command.parameters = QJsonArray{QJsonObject{{"key", "mode"}, {"type", "choice"}, {"state_key", "selected"},
            {"options", QJsonArray{"safe", "fast"}}, {"required", true}, {"default", "safe"}}};
        CommandForm fromState(command, [](QObject *, CommandForm::StateCallback callback) {
            QTimer::singleShot(0, [callback] { callback(true, {{"selected", "missing"}}, {}); });
        });
        fromState.show();
        auto *combo = fromState.findChild<QComboBox *>("parameter_mode");
        QTRY_VERIFY(fromState.findChild<QPushButton *>("primaryButton")->isEnabled());
        QCOMPARE(combo->currentIndex(), -1);
        fromState.findChild<QPushButton *>("primaryButton")->click();
        QVERIFY(fromState.result() != QDialog::Accepted);

        // Optional dynamic choices can be cleared after selecting a real value.
        command.parameters = QJsonArray{QJsonObject{{"key", "account"}, {"type", "choice"},
            {"options_state_key", "accounts"}}};
        CommandForm optional(command, [this](QObject *, CommandForm::StateCallback callback) {
            callback(true, accountState(), {});
        });
        optional.show();
        auto *optionalCombo = optional.findChild<QComboBox *>("parameter_account");
        QTRY_VERIFY(optional.findChild<QPushButton *>("primaryButton")->isEnabled());
        optionalCombo->setCurrentIndex(optionalCombo->findData("alpha"));
        QCOMPARE(optional.values().value("account"), QStringLiteral("alpha"));
        optionalCombo->setCurrentIndex(optionalCombo->findData(QString()));
        optional.findChild<QPushButton *>("primaryButton")->click();
        QCOMPARE(optional.result(), int(QDialog::Accepted));
        QVERIFY(optional.values().value("account").isEmpty());
    }
    void sshFormReaderUsesRegisteredTarget()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), directory.filePath("ssh.exe")));
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", QDir::toNativeSeparators(directory.path()).toUtf8() + ";" + oldPath);
        const auto restore = qScopeGuard([&] { qputenv("PATH", oldPath); });
        AppDatabase::AutomationTool tool;
        tool.externalId = "ssh-ui-fixture";
        tool.targetType = "ssh";
        tool.sshHost = "fixture-only";
        tool.sshUser = "form-owner";
        tool.statePath = "/srv/tool's folder/current.json";
        CommandForm form(accountCommand(), [tool](QObject *context, CommandForm::StateCallback callback) {
            ToolStateReader::read(context, tool, std::move(callback));
        });
        form.show();
        auto *run = form.findChild<QPushButton *>("primaryButton");
        QTRY_VERIFY_WITH_TIMEOUT(run->isEnabled(), 5000);
        QCOMPARE(form.values().value("account"), QStringLiteral("alpha"));
        form.reject();
        bool completed = false;
        QJsonObject received;
        ToolStateReader::read(&form, tool, [&](bool ok, const QJsonObject &state, const QString &) {
            completed = ok;
            received = state;
        });
        QTRY_VERIFY_WITH_TIMEOUT(completed, 5000);
        QCOMPARE(received["summary"].toString(), tool.sshUser);
        QCOMPARE(received["reader_command"].toString(), QStringLiteral("cat -- '/srv/tool'\"'\"'s folder/current.json'"));
    }
    void localFormRefreshAndCommandExecution()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("state.json");
        const auto write = [&](const QJsonObject &state) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
                && file.write(QJsonDocument(state).toJson()) > 0;
        };
        AppDatabase::AutomationTool tool;
        tool.externalId = "example-adapter";
        tool.name = "Form execution fixture";
        tool.targetType = "windows-local";
        tool.workingDirectory = directory.path();
        tool.statePath = path;
        const auto command = accountCommand();
        QVERIFY(database_.saveAutomationTool(tool, {command}, &tool.id));
        const auto cleanup = qScopeGuard([&] { database_.deleteAutomationTool(tool.id); });
        QCOMPARE(database_.toolCommands(tool.id).first().parameters, command.parameters);
        QVERIFY(write(accountState("alpha")));
        AutomationPage page(&database_);
        page.loadAll(tool.id);
        QTRY_COMPARE(page.stateCache_.value(tool.id)["active_account"].toString(), QStringLiteral("alpha"));
        QVERIFY(write(accountState("beta")));
        bool cancelled = false, submitted = false, wrongDefault = false;
        QTimer drive;
        connect(&drive, &QTimer::timeout, &page, [&] {
            auto *form = dynamic_cast<CommandForm *>(page.findChild<QDialog *>("commandParametersDialog"));
            if (!form) return;
            auto *combo = form->findChild<QComboBox *>("parameter_account");
            auto *run = form->findChild<QPushButton *>("primaryButton");
            if (!run->isEnabled()) return;
            wrongDefault |= combo->currentData().toString() != QStringLiteral("beta");
            if (!cancelled) { cancelled = true; form->reject(); }
            else if (!submitted) { submitted = true; run->click(); }
        });
        QTimer deadline;
        deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, &page, [&] {
            if (auto *dialog = page.findChild<QDialog *>("commandParametersDialog")) dialog->reject();
        });
        drive.start(10);
        deadline.start(5000);
        page.runCommand(page.commandList_->item(0));
        QVERIFY(cancelled);
        QVERIFY(!page.runRecords_.contains(tool.id));
        page.runCommand(page.commandList_->item(0));
        drive.stop();
        deadline.stop();
        QVERIFY(submitted);
        QVERIFY(!wrongDefault);
        QTRY_VERIFY_WITH_TIMEOUT(!page.runRecords_.value(tool.id).running, 5000);
        QVERIFY2(!page.runRecords_.value(tool.id).failed, qPrintable(page.runRecords_.value(tool.id).output));
        const auto output = QString(page.runRecords_.value(tool.id).output).replace("\r\n", "\n");
        QCOMPARE(QJsonDocument::fromJson(output.trimmed().section(QLatin1Char('\n'), -1).toUtf8()).array(), QJsonArray{"--account=beta"});

        // The local reader dispatches asynchronously, validates ownership and
        // ignores completions after its owning form/request has been closed.
        bool returned = false, ok = false;
        ToolStateReader::readLocal(&page, tool, [&](bool success, const QJsonObject &, const QString &) {
            returned = true; ok = success;
        });
        QVERIFY(!returned);
        QTRY_VERIFY(returned);
        QVERIFY(ok);
        QVERIFY(write(validState("wrong-tool")));
        returned = false;
        ToolStateReader::readLocal(&page, tool, [&](bool success, const QJsonObject &, const QString &) {
            returned = true; ok = success;
        });
        QTRY_VERIFY(returned);
        QVERIFY(!ok);
        auto *owner = new QObject;
        bool afterClose = false;
        ToolStateReader::readLocal(owner, tool, [&](bool, const QJsonObject &, const QString &) { afterClose = true; });
        delete owner;
        QTest::qWait(50);
        QVERIFY(!afterClose);
    }
    void argumentsAreExpandedOnce()
    {
        const QHash<QString, QString> values{{"name", "中文 {other} $() ' spaced"}, {"other", "must-not-expand"}, {"flag", ""}};
        const QStringList arguments{"{name}", "--name={name}", "{flag}", "--flag={flag}", "{undefined}", ""};
        const QStringList expected{values.value("name"), "--name=" + values.value("name"), "--flag=", "{undefined}", ""};
        QCOMPARE(CommandArguments::expand(arguments, values), expected);
    }
    void argumentsRoundTrip()
    {
        QString error;
        AppDatabase database;
        QVERIFY2(database.open(&error), qPrintable(error));
        AppDatabase::AutomationTool tool;
        tool.externalId = "contract-test";
        tool.name = "Contract";
        tool.targetType = "windows-local";
        tool.registrationPath = "fixture.json";
        tool.statePath = "state.json";
        int id = 0;
        QVERIFY(database.addAutomationTool(tool, &id, &error));
        AppDatabase::ToolCommand command;
        command.name = "Round trip";
        command.executable = "not-executed";
        command.arguments = QStringList{"", "plain", "two words", "中文", QString(QChar(0x1f)), ""};
        QVERIFY(database.replaceToolCommands(id, {command}, &error));
        QCOMPARE(database.toolCommands(id).first().arguments, command.arguments);
        // Simulate an old row with no JSON representation. Its old format remains readable.
        const QString connection = "contract-legacy-fixture";
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE", connection);
            sql.setDatabaseName(database.databasePath());
            QVERIFY(sql.open());
            QSqlQuery query(sql);
            query.prepare("UPDATE tool_commands SET arguments_json='', arguments=? WHERE tool_id=?");
            query.addBindValue(QStringLiteral("one") + QChar(0x1f) + QStringLiteral("two"));
            query.addBindValue(id);
            QVERIFY(query.exec());
        }
        QSqlDatabase::removeDatabase(connection);
        QCOMPARE(database.toolCommands(id).first().arguments, (QStringList{"one", "two"}));
        QVERIFY(database.deleteAutomationTool(id, &error));
    }
    void atomicRegistrationRollsBack()
    {
        QString error;
        AppDatabase::AutomationTool tool;
        tool.externalId = "atomic-test";
        tool.name = "Original";
        tool.targetType = "windows-local";
        tool.statePath = "old-state.json";
        tool.refreshEnabled = true;
        AppDatabase::ToolCommand original;
        original.name = "original";
        original.executable = "original-executable";
        int id = 0;
        QVERIFY(database_.saveAutomationTool(tool, {original}, &id, &error));
        const auto originalCommands = database_.toolCommands(id);
        const auto count = database_.automationTools().size();
        QVERIFY(execSql("CREATE TRIGGER reject_test_command BEFORE INSERT ON tool_commands "
                        "WHEN NEW.name = 'reject' BEGIN SELECT RAISE(ABORT, 'injected write failure'); END"));
        const auto cleanup = qScopeGuard([&] { execSql("DROP TRIGGER IF EXISTS reject_test_command"); });
        tool.id = id;
        tool.name = "Changed";
        tool.targetType = "ssh";
        tool.statePath = "/new-state.json";
        tool.builtin = true;
        AppDatabase::ToolCommand bad = original;
        bad.name = "reject";
        QVERIFY(!database_.saveAutomationTool(tool, {original, bad}, nullptr, &error));
        QVERIFY(error.contains("injected write failure"));
        for (const auto &stored : database_.automationTools()) if (stored.id == id) {
            QCOMPARE(stored.name, QString("Original"));
            QCOMPARE(stored.statePath, QString("old-state.json"));
            QCOMPARE(stored.targetType, QString("windows-local"));
            QVERIFY(!stored.builtin);
            QVERIFY(stored.refreshEnabled);
        }
        QCOMPARE(database_.toolCommands(id).size(), 1);
        QCOMPARE(database_.toolCommands(id).first().id, originalCommands.first().id);
        tool.id = 0;
        int unpublished = -1;
        QVERIFY(!database_.saveAutomationTool(tool, {bad}, &unpublished, &error));
        QCOMPARE(unpublished, -1);
        QCOMPARE(database_.automationTools().size(), count);
        tool.id = id;
        QVERIFY(database_.saveAutomationTool(tool, {}, nullptr, &error));
        QVERIFY(database_.toolCommands(id).isEmpty());
        QVERIFY(database_.deleteAutomationTool(id, &error));
    }
    void stateValidationAndExpiry()
    {
        QJsonObject result;
        QString error;
        const auto check = [&](const QJsonObject &state) {
            return ToolState::parse(QJsonDocument(state).toJson(), "example-adapter", &result, &error);
        };
        auto state = validState();
        QVERIFY(check(state));
        for (const QString &key : {QString("schema"), QString("tool_id"), QString("result"),
                                   QString("summary"), QString("updated_at"), QString("current_date")}) {
            auto invalid = state;
            invalid.remove(key);
            QVERIFY(!check(invalid));
            QVERIFY(!error.isEmpty());
            QVERIFY(result.isEmpty());
        }
        auto invalid = state;
        invalid["tool_id"] = "different-tool";
        QVERIFY(!check(invalid));
        invalid = state;
        invalid["updated_at"] = "2026-09-29T10:00:00";
        QVERIFY(!check(invalid));
        invalid = state;
        invalid["current_date"] = "2026-02-30";
        QVERIFY(!check(invalid));
        invalid = state;
        invalid["items"] = QJsonArray{7};
        QVERIFY(!check(invalid));
        state["expires_at"] = "2026-09-29T11:00:00+08:00";
        QVERIFY(check(state));
        QVERIFY(!ToolState::expired(state, ToolState::timestamp("2026-09-29T10:59:59+08:00")));
        QVERIFY(ToolState::expired(state, ToolState::timestamp("2026-09-29T11:00:00+08:00")));
        state["expires_at"] = "2026-09-29T09:00:00+08:00";
        QVERIFY(!check(state));
        QVERIFY(!ToolState::parse(QByteArray(ToolState::maxBytes + 1, ' '), "example-adapter", &result, &error));
        AutomationPage page(&database_);
        state = validState();
        state["updated_at"] = QDateTime::currentDateTimeUtc().addSecs(-60).toString(Qt::ISODate);
        state["expires_at"] = QDateTime::currentDateTimeUtc().addSecs(-1).toString(Qt::ISODate);
        page.renderState(state);
        QVERIFY(page.stateBadge_->text().contains(QStringLiteral("已过期")));
        page.stateCache_.insert(9999, state);
        page.selectedToolId_ = 9999;
        page.stateBadge_->setText(QStringLiteral("成功"));
        page.show();
        page.updateCachedStateExpiry();
        QVERIFY(page.stateBadge_->text().contains(QStringLiteral("已过期")));
        QVERIFY(!page.stateExpiryTimer_->isActive());

        // A cached state still expires without a poll or another state-file read.
        state["summary"] = QStringLiteral("缓存快照");
        state["expires_at"] = QDateTime::currentDateTimeUtc().addMSecs(300).toString(Qt::ISODateWithMs);
        page.applyStateResult(9999, true, state, {});
        QVERIFY(page.stateExpiryTimer_->isActive());
        QVERIFY(page.stateExpiryTimer_->isSingleShot());
        QTRY_VERIFY(page.stateBadge_->text().contains(QStringLiteral("已过期")));
        QVERIFY(page.stateSummary_->text().endsWith(QStringLiteral("上次摘要：缓存快照")));
        QCOMPARE(page.stateCache_.value(9999), state);
        QVERIFY(!page.stateExpiryTimer_->isActive());

        state["expires_at"] = QDateTime::currentDateTimeUtc().addSecs(60).toString(Qt::ISODate);
        page.applyStateResult(9999, true, state, {});
        QVERIFY(page.stateExpiryTimer_->isActive());
        page.applyStateResult(9999, false, {}, QStringLiteral("读取失败"));
        QVERIFY(!page.stateExpiryTimer_->isActive());

        state.remove("expires_at");
        page.applyStateResult(9999, true, state, {});
        QVERIFY(!page.stateExpiryTimer_->isActive());
    }
    void stateReaderValidatesLocalFiles()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("current.json");
        QJsonObject state;
        QString error;
        const auto read = [&](const QString &id) {
            AppDatabase::AutomationTool tool; tool.externalId=id; tool.statePath=path;
            QEventLoop loop; bool done=false, success=false;
            ToolStateReader::readLocal(&loop, tool, [&](bool ok,const QJsonObject &value,const QString &message) {
                done=true; success=ok; state=value; error=message; loop.quit();
            });
            QTimer::singleShot(5000,&loop,&QEventLoop::quit); loop.exec();
            return done && success;
        };
        QVERIFY(read("example-adapter"));
        QCOMPARE(state.value("result").toString(), QStringLiteral("none"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{}");
        file.close();
        QVERIFY(!read("example-adapter"));
        QVERIFY(state.isEmpty());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("{broken");
        file.close();
        QVERIFY(!read("example-adapter"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(validState()).toJson());
        file.close();
        QVERIFY(read("example-adapter"));
        QVERIFY(!read("another-tool"));
    }
    void selectionDuringBackgroundReadUsesCurrentSelection()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AutomationPage page(&database_);
        AppDatabase::AutomationTool tool;
        tool.externalId = "selection-fixture";
        tool.name = "Selection fixture";
        tool.targetType = "windows-local";
        tool.statePath = directory.filePath("absent.json");
        tool.refreshEnabled = true; // An old preference must not restart periodic reads.
        QVERIFY(database_.saveAutomationTool(tool, {}, &tool.id));
        const auto cleanup = qScopeGuard([&] { database_.deleteAutomationTool(tool.id); });

        page.refreshStateForTool(tool.id, false);
        QVERIFY(page.stateReadsInFlight_.contains(tool.id));
        page.loadToolDetails(tool.id);
        QVERIFY(page.stateBadge_->text().contains(QStringLiteral("读取中")));
        QTRY_VERIFY_WITH_TIMEOUT(!page.stateReadsInFlight_.contains(tool.id), 4000);
        QVERIFY(page.stateBadge_->text().contains(QStringLiteral("暂无状态")));
        QVERIFY(!page.stateExpiryTimer_->isActive());
        QFile stateFile(tool.statePath);
        QVERIFY(stateFile.open(QIODevice::WriteOnly));
        stateFile.write(QJsonDocument(validState(tool.externalId)).toJson());
        stateFile.close();
        auto *refreshButton = page.findChild<QPushButton *>(QStringLiteral("refreshStateButton"));
        QVERIFY(refreshButton);
        refreshButton->click();
        QTRY_COMPARE(page.stateSummary_->text(), QStringLiteral("中文状态"));
        QVERIFY(!page.stateExpiryTimer_->isActive());

        page.refreshStateForTool(tool.id, true);
        page.showEmptyDetails();
        page.stateBadge_->setText("selection changed");
        QTRY_VERIFY_WITH_TIMEOUT(!page.stateReadsInFlight_.contains(tool.id), 4000);
        QCOMPARE(page.stateBadge_->text(), QStringLiteral("selection changed"));
    }
    void sshReadTimeoutAndRetry()
    {
        AutomationPage page(&database_);
        AppDatabase::AutomationTool tool;
        tool.externalId = "ssh-fixture";
        tool.name = "SSH fixture";
        tool.targetType = "ssh";
        tool.sshHost = "not-a-real-host";
        tool.statePath = "/fixture.json";
        QVERIFY(database_.saveAutomationTool(tool, {}, &tool.id));
        auto *hanging = new QProcess;
        QPointer<QProcess> hangingGuard(hanging);
        hanging->setProgram(QCoreApplication::applicationFilePath());
        hanging->setArguments({"--helper-hang"});
        page.startStateReadProcess(tool, false, hanging, 100);
        QVERIFY(page.stateReadsInFlight_.contains(tool.id));
        QTRY_VERIFY_WITH_TIMEOUT(!page.stateReadsInFlight_.contains(tool.id), 3000);
        QVERIFY(page.stateErrors_.value(tool.id).contains(QStringLiteral("超时")));
        QTRY_VERIFY(hangingGuard.isNull());
        auto *retry = new QProcess;
        retry->setProgram(QCoreApplication::applicationFilePath());
        retry->setArguments({"--helper-state"});
        page.startStateReadProcess(tool, false, retry, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!page.stateReadsInFlight_.contains(tool.id), 4000);
        QVERIFY(!page.stateErrors_.contains(tool.id));
        QCOMPARE(page.stateCache_.value(tool.id).value("tool_id").toString(), tool.externalId);
        auto *wrongOwner = new QProcess;
        wrongOwner->setProgram(QCoreApplication::applicationFilePath());
        wrongOwner->setArguments({"--helper-other-state"});
        page.startStateReadProcess(tool, false, wrongOwner, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!page.stateReadsInFlight_.contains(tool.id), 4000);
        QVERIFY(page.stateErrors_.value(tool.id).contains("tool_id"));
        QVERIFY(!page.stateCache_.contains(tool.id));
        auto *missing = new QProcess;
        missing->setProgram("Z:/nonexistent-orchestrate-test.exe");
        page.startStateReadProcess(tool, false, missing, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!page.stateReadsInFlight_.contains(tool.id), 4000);
        QVERIFY(page.stateErrors_.contains(tool.id));
        QVERIFY(database_.deleteAutomationTool(tool.id));
    }
    void commandOutputIsBoundedAndDecodedAcrossChunks()
    {
        AutomationPage page(&database_);
        AppDatabase::AutomationTool tool;
        tool.externalId = "output-fixture";
        tool.name = "Output fixture";
        tool.targetType = "windows-local";
        tool.statePath = "absent-fixture.json";
        tool.workingDirectory = QCoreApplication::applicationDirPath();
        AppDatabase::ToolCommand command;
        command.name = "Output";
        command.executable = QCoreApplication::applicationFilePath();
        command.arguments = {"--helper-output"};
        QVERIFY(database_.saveAutomationTool(tool, {command}, &tool.id));
        page.loadAll(tool.id);
        page.runCommand(page.commandList_->item(0));
        QTRY_VERIFY_WITH_TIMEOUT(!page.runRecords_.value(tool.id).running, 5000);
        const auto output = QString(page.runRecords_.value(tool.id).output).replace("\r\n", "\n");
        QVERIFY2(output.contains(QString::fromUtf8("中文跨块😀\n")), qPrintable(output));
        QVERIFY(!output.contains(QChar::ReplacementCharacter));
        command.arguments = {"--helper-flood"};
        QVERIFY(database_.saveAutomationTool(tool, {command}, nullptr));
        page.loadAll(tool.id);
        page.runCommand(page.commandList_->item(0));
        QTRY_VERIFY_WITH_TIMEOUT(!page.runRecords_.value(tool.id).running, 5000);
        const auto record = page.runRecords_.value(tool.id);
        QVERIFY(record.outputTruncated);
        QVERIFY(record.output.size() <= AutomationPage::maxRunOutputChars);
        QVERIFY(QString(record.output).replace("\r\n", "\n").endsWith(QStringLiteral("末尾\n")));
        QVERIFY(database_.deleteAutomationTool(tool.id));
    }
    void manifestUpdateInvalidatesOldState()
    {
        AutomationPage page(&database_);
        AppDatabase::AutomationTool tool;
        tool.name = "Cache fixture";
        tool.externalId = "old-id";
        tool.targetType = "windows-local";
        QVERIFY(database_.saveAutomationTool(tool, {}, &tool.id));
        page.stateCache_.insert(tool.id, validState(tool.externalId));
        AutomationPage::ParsedManifest manifest;
        manifest.tool = tool;
        manifest.tool.externalId = "new-id";
        QVERIFY(page.applyManifestUpdate(tool.id, manifest));
        QVERIFY(!page.stateCache_.contains(tool.id));
        // A request for the old registration must not restore its old success.
        auto *oldRequest = new QProcess;
        oldRequest->setProgram(QCoreApplication::applicationFilePath());
        oldRequest->setArguments({"--helper-state"});
        page.startStateReadProcess(tool, false, oldRequest, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!page.stateReadsInFlight_.contains(tool.id), 4000);
        QVERIFY(!page.stateCache_.contains(tool.id));
        QVERIFY(database_.deleteAutomationTool(tool.id));
    }
    void logUpdatesAreIncrementalBoundedAndIdleWhenHidden()
    {
        AutomationPage page(&database_); page.resize(1100,800); page.show();
        page.selectedToolId_ = 999;
        page.runRecords_[999] = {};
        page.appendRunOutput(999, "Command heading\n");
        page.renderRunRecord();
        page.appendRunOutput(999, QString(250*1024, 'x'));
        QElapsedTimer timer; timer.start(); page.renderRunRecord();
        QCoreApplication::processEvents();
        QVERIFY2(timer.elapsed() < 1000, "Appending a long log line blocked the UI");
        QCOMPARE(page.runOutput_->lineWrapMode(), QPlainTextEdit::NoWrap);
        page.appendRunOutput(999, QString(6*1024, 'x'));
        page.renderRunRecord();
        timer.restart();
        for(int i=0;i<20;++i) {
            page.appendRunOutput(999, QString::fromUtf8("中文😀 tail\r"));
            page.appendRunOutput(999, "\n");
            page.renderRunRecord();
            QCOMPARE(page.runOutput_->toPlainText(),page.runRecords_[999].output);
        }
        QVERIFY2(timer.elapsed() < 1500, "Incremental log updates blocked the UI");
        QVERIFY(page.runRecords_[999].output.size()<=256*1024);
        page.hide();
        const auto previous=page.runOutput_->toPlainText();
        page.appendRunOutput(999,"hidden output"); page.renderRunRecord();
        QCOMPARE(page.runOutput_->toPlainText(),previous);
        page.show(); QCOMPARE(page.runOutput_->toPlainText(),page.runRecords_[999].output);
    }
    void fileRequestsBoundConcurrencyTimeoutAndCancellation()
    {
        QTRY_COMPARE(FileAccess::queue()->running,0);
        QObject context;
        int callbacks=0, beats=0;
        QTimer heartbeat; connect(&heartbeat,&QTimer::timeout,this,[&]{++beats;}); heartbeat.start(10);
        for(int i=0;i<40;++i) FileAccess::request(&context,"read",QString("__test_slow_file__%1").arg(i),
            [&](bool ok,const QByteArray &,const QString &error){QVERIFY(!ok); QVERIFY(!error.isEmpty()); ++callbacks;},150);
        QVERIFY(FileAccess::queue()->running<=2);
        QVERIFY(FileAccess::queue()->pending.size()<=32);
        QTRY_COMPARE_WITH_TIMEOUT(callbacks,40,3000);
        QTRY_COMPARE(FileAccess::queue()->running,0);
        QTRY_VERIFY(FileAccess::queue()->pending.isEmpty());
        QVERIFY(beats>=3);
        int cancelledCallbacks=0;
        auto *dialog=new QDialog;
        FileAccess::request(dialog,"read","__test_slow_file__cancel",[&](bool,const QByteArray &,const QString &){++cancelledCallbacks;});
        delete dialog;
        QTRY_COMPARE(FileAccess::queue()->running,0);
        QTest::qWait(50); QCOMPARE(cancelledCallbacks,0);
    }
    void fileRequestsKeepExactLimitAndReturnResources()
    {
        QTemporaryDir dir;
        const QString path=dir.filePath(QStringLiteral("中文 #100% ' file.json"));
        QByteArray expected(1024*1024,'x'); expected[99]='\n';
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(expected),expected.size()); file.close();
        int callbacks=0; bool success=false; QByteArray result;
        FileAccess::request(this,"read",path,[&](bool ok,const QByteArray &bytes,const QString &){++callbacks;success=ok;result=bytes;});
        QTRY_COMPARE(callbacks,1); QVERIFY(success); QCOMPARE(result,expected);
        QVERIFY(file.open(QIODevice::Append)); file.write("x"); file.close();
        FileAccess::request(this,"read",path,[&](bool ok,const QByteArray &,const QString &error){++callbacks;success=ok;QVERIFY(error.contains("1 MiB"));});
        QTRY_COMPARE(callbacks,2); QVERIFY(!success);
        QTRY_COMPARE(FileAccess::queue()->running,0);
        QTRY_VERIFY(FileAccess::queue()->pending.isEmpty());
    }
};

int main(int argc, char **argv)
{
    if (FileAccess::isHelper(argc, argv)) {
        QCoreApplication helper(argc, argv);
        if (helper.arguments().value(3).startsWith("__test_slow_file__")) QThread::msleep(10000);
        return FileAccess::runHelper(helper.arguments(), false);
    }
    QApplication app(argc, argv);
    if (QFileInfo(app.applicationFilePath()).baseName() == "ssh") {
        const auto args = app.arguments();
        const QString user = args.contains("-l") ? args.value(args.indexOf("-l") + 1) : "fixture-default";
        QFile output;
        if (!output.open(stdout, QIODevice::WriteOnly)) return 2;
        const auto manifest = QJsonObject{{"schema", "orchestrate-tool/v1"}, {"id", "ssh-ui-fixture"}, {"name", "SSH fixture"},
            {"target_type", "ssh"}, {"state_path", "current.json"}, {"commands", QJsonArray{QJsonObject{{"name", "Probe"}, {"executable", "probe"}}}}};
        if (args.last().contains("orchestrate-directory-v1")) {
            const QByteArray root = args.last().contains("other folder") ? "/srv/moved" : "/srv/tool";
            const QList<QByteArray> inventory{"orchestrate-directory-v1", "R", root, "U", user.toUtf8(),
                                              "M", QJsonDocument(manifest).toJson(), "F", "orchestrate-tool.json"};
            for (const auto &field : inventory) { output.write(field); output.putChar('\0'); }
        } else if (args.last().contains("orchestrate-tool.json")) output.write(QJsonDocument(manifest).toJson());
        else if (args.last().contains("current.json")) output.write(QJsonDocument(QJsonObject{
            {"schema", "orchestrate-state/v1"}, {"tool_id", "ssh-ui-fixture"}, {"result", "success"}, {"summary", user},
            {"updated_at", "2026-10-02T10:00:00+08:00"}, {"current_date", "2026-10-02"},
            {"active_account", "alpha"}, {"accounts", QJsonArray{QJsonObject{{"value", "alpha"}, {"label", "Alpha"}}}},
            {"reader_command", args.last()}}).toJson());
        else output.write(QStringLiteral("MOCK USER=%1\n").arg(user).toUtf8());
        output.flush();
        return 0;
    }
    if (app.arguments().contains("--helper-hang")) { QThread::msleep(10000); return 0; }
    if (argc >= 2 && QString::fromLocal8Bit(argv[1]).startsWith("--helper-")) {
        QFile output;
        if (!output.open(stdout, QIODevice::WriteOnly)) return 3;
        const QString mode = app.arguments().at(1);
        if (mode == "--helper-arguments") {
            output.write(QJsonDocument(QJsonArray::fromStringList(app.arguments().mid(2))).toJson(QJsonDocument::Compact));
        } else if (mode == "--helper-state" || mode == "--helper-other-state") {
            QByteArray state(R"({"schema":"orchestrate-state/v1","tool_id":"ssh-fixture","result":"success","summary":"ok","updated_at":"2026-09-29T10:00:00+08:00","current_date":"2026-09-29"})");
            if (mode == "--helper-other-state") state.replace("ssh-fixture", "wrong-owner");
            output.write(state);
        } else if (mode == "--helper-output") {
            for (const char byte : QString::fromUtf8("中文跨块😀\n").toUtf8()) {
                output.write(&byte, 1);
                output.flush();
                QThread::msleep(15);
            }
        } else if (mode == "--helper-flood") {
            for (int i = 0; i < 128; ++i) output.write(QByteArray(8192, 'x'));
            output.write(QStringLiteral("末尾\n").toUtf8());
        }
        output.flush();
        return 0;
    }
    if (QFileInfo(app.applicationDirPath()).fileName() != QStringLiteral("automation-contract-test")) return 2;
    QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName("OrchestrateContractTests");
    app.setApplicationName(QUuid::createUuid().toString(QUuid::Id128));
    AutomationContractTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_automation_contract.moc"
