#include "automation/manifestparser.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

class ManifestParserTest : public QObject
{
    Q_OBJECT

    static QJsonObject monitor(const QString &target = QStringLiteral("windows-local"))
    {
        return {{"schema", "orchestrate-tool/v1"}, {"id", "parser-fixture"},
                {"name", "Parser fixture"}, {"target_type", target},
                {"state_path", "state/current.json"}, {"commands", QJsonArray()}};
    }

    static QJsonObject command()
    {
        return {{"name", "Run"}, {"executable", "python"},
                {"arguments", QJsonArray{"worker.py", "{item}"}},
                {"parameters", QJsonArray{QJsonObject{{"key", "item"}, {"type", "string"},
                                                     {"required", true}}}}};
    }

private slots:
    void localPathsAndCommandDefaults()
    {
        auto object = monitor();
        object["working_directory"] = "../worker";
        object["environment"] = QJsonObject{{"ITEM_ROOT", "D:/items"}};
        object["group"] = "  Examples  ";
        object["commands"] = QJsonArray{command()};
        ManifestParser::Manifest manifest;
        QString error;
        QVERIFY2(ManifestParser::parse(QJsonDocument(object).toJson(),
                     "D:/tools/config/tool.json", {}, &manifest, &error), qPrintable(error));
        QCOMPARE(manifest.tool.workingDirectory, QStringLiteral("D:/tools/worker"));
        QCOMPARE(manifest.tool.statePath, QStringLiteral("D:/tools/config/state/current.json"));
        QCOMPARE(manifest.tool.registrationPath, QStringLiteral("D:/tools/config/tool.json"));
        QCOMPARE(manifest.tool.environment.value("ITEM_ROOT").toString(), QStringLiteral("D:/items"));
        QCOMPARE(manifest.group, QStringLiteral("Examples"));
        QCOMPARE(manifest.commands.size(), 1);
        const auto &parsed = manifest.commands.first();
        QCOMPARE(parsed.arguments, (QStringList{"worker.py", "{item}"}));
        QCOMPARE(parsed.parameters.size(), 1);
        QVERIFY(parsed.enabled);
        QVERIFY(!parsed.interactive);
        QVERIFY(!parsed.highRisk);
        object["state_path"] = "D:/shared/state.json";
        QVERIFY(ManifestParser::parse(QJsonDocument(object).toJson(),
                    "D:/tools/config/tool.json", {}, &manifest, &error));
        QCOMPARE(manifest.tool.statePath, QStringLiteral("D:/shared/state.json"));
    }

    void remotePaths_data()
    {
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("working");
        QTest::addColumn<QString>("state");
        QTest::addColumn<QString>("expectedWorking");
        QTest::addColumn<QString>("expectedState");
        QTest::newRow("ssh-root") << "ssh" << "/tool.json" << "" << "state/current.json"
                                 << "/" << "/state/current.json";
        QTest::newRow("ssh-workdir") << "ssh" << "/srv/config/tool.json" << "/opt/worker" << "state/current.json"
                                    << "/opt/worker" << "/opt/worker/state/current.json";
        QTest::newRow("ssh-absolute-state") << "ssh" << "/srv/config/tool.json" << "" << "/var/state.json"
                                           << "/srv/config" << "/var/state.json";
        QTest::newRow("wsl-relative-workdir") << "wsl" << "/home/alice/config/tool.json" << "../worker" << "state/current.json"
                                             << "/home/alice/worker" << "/home/alice/config/state/current.json";
        QTest::newRow("wsl-unicode") << "wsl" << QStringLiteral("/home/alice/中文 tools/tool.json") << "" << "../state.json"
                                    << QStringLiteral("/home/alice/中文 tools") << "/home/alice/state.json";
        QTest::newRow("wsl-absolute-state") << "wsl" << "/home/alice/tool.json" << "/opt/worker" << "/var/state.json"
                                           << "/opt/worker" << "/var/state.json";
        QTest::newRow("wsl-root") << "wsl" << "/tool.json" << "/" << "state.json" << "/" << "/state.json";
        QTest::newRow("wsl-root-relative-workdir") << "wsl" << "/tool.json" << "worker" << "state.json"
                                                 << "/worker" << "/state.json";
    }

    void remotePaths()
    {
        QFETCH(QString, target);
        QFETCH(QString, source);
        QFETCH(QString, working);
        QFETCH(QString, state);
        QFETCH(QString, expectedWorking);
        QFETCH(QString, expectedState);
        auto object = monitor(target);
        object["working_directory"] = working;
        object["state_path"] = state;
        const bool ssh = target == QStringLiteral("ssh");
        ManifestParser::Manifest manifest;
        QString error;
        QVERIFY2(ManifestParser::parse(QJsonDocument(object).toJson(), source,
                     ssh ? QStringLiteral("office") : QString(), &manifest, &error,
                     ssh ? QString() : QStringLiteral("Ubuntu"),
                     ssh ? QString() : QStringLiteral("alice"),
                     ssh ? QStringLiteral("alice") : QString()), qPrintable(error));
        QCOMPARE(manifest.tool.workingDirectory, expectedWorking);
        QCOMPARE(manifest.tool.statePath, expectedState);
        QCOMPARE(manifest.tool.sshUser, ssh ? QStringLiteral("alice") : QString());
        QCOMPARE(manifest.tool.wslUser, ssh ? QString() : QStringLiteral("alice"));
    }

    void malformedManifests_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("invalid-json") << QByteArray("{");
        QTest::newRow("array-root") << QByteArray("[]");
        const auto add = [](const char *label, const QString &key, const QJsonValue &value) {
            auto object = monitor();
            object[key] = value;
            QTest::newRow(label) << QJsonDocument(object).toJson();
        };
        add("schema", "schema", "orchestrate-tool/v2");
        add("missing-id", "id", QJsonValue(QJsonValue::Undefined));
        add("blank-name", "name", "  ");
        add("wrong-target", "target_type", "ssh");
        add("unregistered-wsl", "target_type", "wsl");
        add("state-type", "state_path", 7);
        add("commands-type", "commands", QJsonObject());
        add("command-not-object", "commands", QJsonArray{7});
        add("environment-type", "environment", QJsonArray());
        add("environment-name", "environment", QJsonObject{{"1BAD", "value"}});
        add("environment-value", "environment", QJsonObject{{"ITEM", true}});
        add("environment-nul", "environment", QJsonObject{{"ITEM", QString(QChar::Null)}});
        auto object = monitor();
        for (const auto &field : {QStringLiteral("arguments"), QStringLiteral("parameters"),
                                  QStringLiteral("interactive"), QStringLiteral("high_risk"),
                                  QStringLiteral("enabled")}) {
            auto invalid = command();
            invalid[field] = "wrong-type";
            object["commands"] = QJsonArray{invalid};
            QTest::newRow(qPrintable(field)) << QJsonDocument(object).toJson();
        }
        auto invalid = command();
        invalid["arguments"] = QJsonArray{7};
        object["commands"] = QJsonArray{invalid};
        QTest::newRow("argument-not-string") << QJsonDocument(object).toJson();
        invalid = command();
        invalid["executable"] = " ";
        object["commands"] = QJsonArray{invalid};
        QTest::newRow("blank-executable") << QJsonDocument(object).toJson();
        invalid = command();
        invalid.remove("name");
        object["commands"] = QJsonArray{invalid};
        QTest::newRow("missing-command-name") << QJsonDocument(object).toJson();
        invalid = command();
        invalid["parameters"] = QJsonArray{QJsonObject{{"key", "item"}, {"type", "unsupported"}}};
        object["commands"] = QJsonArray{invalid};
        QTest::newRow("parameter-type") << QJsonDocument(object).toJson();
        invalid["parameters"] = QJsonArray{QJsonObject{{"key", "item"}}, QJsonObject{{"key", "item"}}};
        object["commands"] = QJsonArray{invalid};
        QTest::newRow("duplicate-parameter") << QJsonDocument(object).toJson();
        invalid["parameters"] = QJsonArray{QJsonObject{{"key", "bad key"}}};
        object["commands"] = QJsonArray{invalid};
        QTest::newRow("invalid-parameter-key") << QJsonDocument(object).toJson();
    }

    void invalidWorkingDirectories_data()
    {
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("working");
        QTest::newRow("ssh-relative") << "ssh" << "relative";
        QTest::newRow("ssh-backslash") << "ssh" << "\\wrong";
        QTest::newRow("wsl-home") << "wsl" << "~/work";
        QTest::newRow("wsl-drive") << "wsl" << "C:/work";
        QTest::newRow("wsl-backslash") << "wsl" << "work\\sub";
    }

    void invalidWorkingDirectories()
    {
        QFETCH(QString, target);
        QFETCH(QString, working);
        auto object = monitor(target);
        object["working_directory"] = working;
        ManifestParser::Manifest manifest;
        QString error;
        const bool ssh = target == "ssh";
        QVERIFY(!ManifestParser::parse(QJsonDocument(object).toJson(), "/tool.json",
                    ssh ? QStringLiteral("office") : QString(), &manifest, &error,
                    ssh ? QString() : QStringLiteral("Ubuntu"),
                    ssh ? QString() : QStringLiteral("alice")));
        QVERIFY(!error.isEmpty());
    }

    void malformedManifests()
    {
        QFETCH(QByteArray, bytes);
        ManifestParser::Manifest manifest;
        QString error;
        QVERIFY(!ManifestParser::parse(bytes, "D:/tools/tool.json", {}, &manifest, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!ManifestParser::parse(bytes, "D:/tools/tool.json", {}, &manifest, nullptr));
    }

    void invalidRemoteRegistration_data()
    {
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("host");
        QTest::addColumn<QString>("distro");
        QTest::addColumn<QString>("user");
        QTest::addColumn<QString>("state");
        QTest::newRow("ssh-host") << "ssh" << "/tool.json" << "--bad" << "" << "" << "state.json";
        QTest::newRow("ssh-relative-source") << "ssh" << "tool.json" << "office" << "" << "" << "state.json";
        QTest::newRow("ssh-state-separators") << "ssh" << "/tool.json" << "office" << "" << "" << "state\\current.json";
        QTest::newRow("mixed-targets") << "wsl" << "/tool.json" << "office" << "Ubuntu" << "alice" << "state.json";
        QTest::newRow("local-with-wsl") << "windows-local" << "/tool.json" << "" << "Ubuntu" << "alice" << "state.json";
        QTest::newRow("wsl-no-user") << "wsl" << "/tool.json" << "" << "Ubuntu" << "" << "state.json";
        QTest::newRow("wsl-distro") << "wsl" << "/tool.json" << "" << "--bad" << "alice" << "state.json";
        QTest::newRow("wsl-user") << "wsl" << "/tool.json" << "" << "Ubuntu" << "two users" << "state.json";
        QTest::newRow("wsl-home-source") << "wsl" << "~/tool.json" << "" << "Ubuntu" << "alice" << "state.json";
        QTest::newRow("wsl-windows-state") << "wsl" << "/tool.json" << "" << "Ubuntu" << "alice" << "D:/state.json";
        QTest::newRow("wsl-home-state") << "wsl" << "/tool.json" << "" << "Ubuntu" << "alice" << "~/state.json";
    }

    void invalidRemoteRegistration()
    {
        QFETCH(QString, target);
        QFETCH(QString, source);
        QFETCH(QString, host);
        QFETCH(QString, distro);
        QFETCH(QString, user);
        QFETCH(QString, state);
        auto object = monitor(target);
        object["state_path"] = state;
        ManifestParser::Manifest manifest;
        QString error;
        QVERIFY(!ManifestParser::parse(QJsonDocument(object).toJson(), source, host,
                                       &manifest, &error, distro, user));
        QVERIFY(!error.isEmpty());
    }

    void readOnlyToolsAndReusedOutput()
    {
        auto object = monitor();
        object["commands"] = QJsonArray{command()};
        ManifestParser::Manifest manifest;
        QString error;
        QVERIFY(ManifestParser::parse(QJsonDocument(object).toJson(), "D:/tool.json", {}, &manifest, &error));
        QCOMPARE(manifest.commands.size(), 1);
        object["commands"] = QJsonArray();
        object["state_path"] = "";
        QVERIFY(ManifestParser::parse(QJsonDocument(object).toJson(), "D:/tool.json", {}, &manifest, &error));
        QVERIFY(manifest.commands.isEmpty());
        QVERIFY(manifest.tool.statePath.isEmpty());
    }

    void boundedFileRead()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("tool.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        constexpr qsizetype limit = 1024 * 1024;
        const QByteArray bytes(limit, ' ');
        QCOMPARE(file.write(bytes), qint64(limit));
        file.close();
        QByteArray loaded;
        QString error;
        QVERIFY2(ManifestParser::read(path, &loaded, &error), qPrintable(error));
        QCOMPARE(loaded, bytes);
        QVERIFY(file.open(QIODevice::Append));
        QCOMPARE(file.write("x"), qint64(1));
        file.close();
        QVERIFY(!ManifestParser::read(path, &loaded, &error));
        QVERIFY(error.contains("1 MiB"));
        QVERIFY(!ManifestParser::read(directory.filePath("missing.json"), &loaded, &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(ManifestParserTest)
#include "test_manifest_parser.moc"
