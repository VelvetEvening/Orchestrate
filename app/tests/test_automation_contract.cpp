#include "pages/automationpage.h"
#include "automation/commandarguments.h"
#include "automation/toolstate.h"
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
        QString error;
        QVERIFY2(database_.open(&error), qPrintable(error));
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
    void targetsAndPaths()
    {
        auto object = basic();
        QVERIFY(parse(object));
        object["target_type"] = "wsl";
        QVERIFY(!parse(object));
        object["target_type"] = "ssh";
        QVERIFY(!parse(object));
        QVERIFY(parse(object, "host"));
        AutomationPage page(&database_);
        AutomationPage::ParsedManifest manifest;
        QString error;
        QVERIFY(page.parseManifest(QJsonDocument(object).toJson(), "/orchestrate-tool.json", "host", &manifest, &error));
        QCOMPARE(manifest.tool.workingDirectory, QStringLiteral("/"));
        QCOMPARE(manifest.tool.statePath, QStringLiteral("/state/current.json"));
        object["working_directory"] = "relative";
        QVERIFY(!parse(object, "host"));
        object["working_directory"] = "\\wrong";
        QVERIFY(!parse(object, "host"));
        object["working_directory"] = "/srv/work";
        object["state_path"] = "state\\current.json";
        QVERIFY(!parse(object, "host"));
        object = basic();
        object["working_directory"] = "worker";
        QVERIFY(page.parseManifest(QJsonDocument(object).toJson(), "D:/tools/example/tool.json", {}, &manifest, &error));
        QCOMPARE(manifest.tool.workingDirectory, QStringLiteral("D:/tools/example/worker"));
        QCOMPARE(manifest.tool.statePath, QStringLiteral("D:/tools/example/state/current.json"));
    }
    void malformedCommandsFailClosed()
    {
        auto object = basic();
        object["commands"] = "not an array";
        QVERIFY(!parse(object));
        object["commands"] = QJsonArray{42};
        QVERIFY(!parse(object));
        object["commands"] = QJsonArray{QJsonObject{{"name", "Broken"}}};
        QVERIFY(!parse(object));
        const QJsonObject valid{{"name", "Run"}, {"executable", "python"}, {"arguments", QJsonArray{"run.py"}}};
        for (const auto &key : {QStringLiteral("arguments"), QStringLiteral("parameters"), QStringLiteral("high_risk"), QStringLiteral("interactive")}) {
            auto command = valid;
            command[key] = "false";
            object["commands"] = QJsonArray{command};
            QVERIFY(!parse(object));
        }
        auto command = valid;
        command["arguments"] = QJsonArray{"run.py", 12};
        object["commands"] = QJsonArray{command};
        QVERIFY(!parse(object));
        command = valid;
        command["parameters"] = QJsonArray{QJsonObject{{"key", "bad key"}}};
        object["commands"] = QJsonArray{command};
        QVERIFY(!parse(object));
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
        page.refreshDueStates();
        QVERIFY(page.stateBadge_->text().contains(QStringLiteral("已过期")));
    }
    void stateReaderValidatesLocalFiles()
    {
        AutomationPage page(&database_);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("current.json");
        QJsonObject state;
        QString error;
        QVERIFY(page.readStateFile(path, "example-adapter", &state, &error));
        QCOMPARE(state.value("result").toString(), QStringLiteral("none"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{}");
        file.close();
        QVERIFY(!page.readStateFile(path, "example-adapter", &state, &error));
        QVERIFY(state.isEmpty());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("{broken");
        file.close();
        QVERIFY(!page.readStateFile(path, "example-adapter", &state, &error));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(validState()).toJson());
        file.close();
        QVERIFY(page.readStateFile(path, "example-adapter", &state, &error));
        QVERIFY(!page.readStateFile(path, "another-tool", &state, &error));
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
        QCOMPARE(page.stateCache_.value(tool.id).value("result").toString(), QStringLiteral("none"));
        QVERIFY(database_.deleteAutomationTool(tool.id));
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (app.arguments().contains("--helper-hang")) { QThread::msleep(10000); return 0; }
    if (argc == 2 && QString::fromLocal8Bit(argv[1]).startsWith("--helper-")) {
        QFile output;
        if (!output.open(stdout, QIODevice::WriteOnly)) return 3;
        const QString mode = app.arguments().at(1);
        if (mode == "--helper-state" || mode == "--helper-other-state") {
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
