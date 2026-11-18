#include "pages/automationpage.h"
#include "cli_fixture.h"
#include "test_support.h"
#ifndef Q_MOC_RUN
#include "automation/discoverydialog.h"
#include "automation/commandform.h"
#include "automation/toolconfiguration.h"
#include "widgets/siblingorderview.h"
#endif
#include <QApplication>
#include <QTest>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QScopeGuard>
#include <QTextStream>

class ConfigurableToolsTest : public QObject {
    Q_OBJECT
    AppDatabase database_;
    QTemporaryDir cliFixture_{QDir::tempPath() + QStringLiteral("/Orchestrate CLI 中文-XXXXXX")};
    QString cliExecutable() const { return cliFixture_.filePath(".venv/Scripts/python.exe"); }
    static bool write(const QString &path,const QByteArray &bytes){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();}
    AppDatabase::AutomationTool tool(QString name="fixture") const {
        AppDatabase::AutomationTool t;t.name=name;t.externalId=name;t.targetType="windows-local";t.workingDirectory=QCoreApplication::applicationDirPath();return t;
    }
    AppDatabase::ToolCommand echo() const {
        AppDatabase::ToolCommand c;c.name=QStringLiteral("显示参数（离线验收）");c.executable=QCoreApplication::applicationFilePath();c.arguments={"--echo-args"};return c;
    }
    bool sql(const QString &statement){
        const QString name=QUuid::createUuid().toString();bool ok;
        {auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(database_.databasePath());ok=db.open()&&QSqlQuery(db).exec(statement);}
        QSqlDatabase::removeDatabase(name);return ok;
    }
private slots:
    void initTestCase(){
        QVERIFY(QCoreApplication::applicationDirPath().endsWith("configurable-tools-test"));
        QDir data(QCoreApplication::applicationDirPath()+"/data");if(data.exists())QVERIFY(data.removeRecursively());
        QString error;QVERIFY2(database_.open(&error),qPrintable(error));
        QVERIFY(cliFixture_.isValid());
        QVERIFY(QDir().mkpath(cliFixture_.filePath(".venv/Scripts")));
        QVERIFY(QDir().mkpath(cliFixture_.filePath("cannlab")));
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), cliExecutable()));
        QVERIFY(write(cliFixture_.filePath("cannlab/__main__.py"), "# Synthetic help-only fixture\n"));
        QVERIFY(write(cliFixture_.filePath("settings.json"), "{\"minutes\":20}"));
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyhbd.ttc"));
        UiTheme::install(*qApp);
    }
    void cleanupTestCase(){
        QFile log(cliFixture_.filePath("fixture-invocations.jsonl"));
        if (!log.exists()) return; // A focused run may not exercise CLI discovery.
        QVERIFY(log.open(QIODevice::ReadOnly));
        while (!log.atEnd()) {
            const auto arguments = QJsonDocument::fromJson(log.readLine()).array();
            QVERIFY(arguments.size() == 3 || arguments.size() == 4);
            QCOMPARE(arguments.first().toString(), QString("-m"));
            QCOMPARE(arguments.at(1).toString(), QString("cannlab"));
            QCOMPARE(arguments.last().toString(), QString("--help"));
        }
    }
    void optionalStateAndStableCommandEditing(){
        auto t=tool("manual");auto a=echo();auto b=a;b.name="second";QString error;
        QVERIFY(database_.saveAutomationTool(t,{a,b},&t.id,&error));
        auto cleanupTool = qScopeGuard([this, id = t.id] { database_.deleteAutomationTool(id); });
        AutomationPage page(&database_);page.resize(1180,760);page.show();page.loadAll(t.id);
        QVERIFY(page.stateSection_->isHidden());QVERIFY(!page.stateReadsInFlight_.contains(t.id));QVERIFY(!page.stateErrors_.contains(t.id));
        auto commands=database_.toolCommands(t.id);const int first=commands[0].id,second=commands[1].id;
        commands[0].enabled=false;commands.move(0,1);QVERIFY(page.saveUserCommands(t.id,commands));
        auto saved=database_.toolCommands(t.id);QCOMPARE(saved[0].id,second);QCOMPARE(saved[1].id,first);QVERIFY(!saved[1].enabled);
        AppDatabase::AutomationTool stored;QVERIFY(page.findTool(t.id,&stored));QVERIFY(stored.customized);
        page.loadToolDetails(t.id);page.runCommand(page.commandList_->item(1));QVERIFY(!page.runRecords_.contains(t.id));
        page.runCommand(page.commandList_->item(0));
        auto *toolItem = page.tree_->currentItem();
        QCOMPARE(toolItem->text(1), QStringLiteral("运行中"));
        const auto runningIcon = toolItem->icon(0).cacheKey();
        page.appendRunOutput(t.id, QStringLiteral("progress\n"));
        page.renderRunRecord();
        QCOMPARE(toolItem->icon(0).cacheKey(), runningIcon);
        QTRY_VERIFY_WITH_TIMEOUT(!page.runRecords_.value(t.id).running, 5000);
        QVERIFY(!page.runRecords_.value(t.id).failed);
        {
            const auto restoreTheme = qScopeGuard([] { UiTheme::apply(*qApp, QStringLiteral("mist")); });
            UiTheme::apply(*qApp, QStringLiteral("paper"));
            QCoreApplication::processEvents();
            QCOMPARE(page.runStatus_->font().weight(), QFont::DemiBold);
            const auto icon = toolItem->icon(0).pixmap(32,32).toImage();
            QCOMPARE(icon.pixelColor(16,16), QColor(UiTheme::Muted));
        }
        QVERIFY(sql("CREATE TRIGGER reject_custom BEFORE INSERT ON tool_commands WHEN NEW.name='reject' BEGIN SELECT RAISE(ABORT,'injected'); END"));
        saved[0].name="reject";t.name="should rollback";QVERIFY(!database_.saveAutomationTool(t,saved,nullptr,&error));
        QCOMPARE(database_.toolCommands(t.id)[0].name,QString("second"));QVERIFY(sql("DROP TRIGGER reject_custom"));
        TestSupport::capture(page, "tool-desktop");page.resize(900,600);TestSupport::capture(page, "tool-compact");
        QVERIFY(database_.deleteAutomationTool(t.id)); cleanupTool.dismiss();
    }
    void sourceParsingIsAllOrNothing(){
        QList<CommandParameters::Option> options;QString error;
        QJsonObject source{{"kind","file"},{"format","json"},{"items_path","data.rows"},{"value_key","id"},{"label_key","name"}};
        QVERIFY(OptionsSource::parse(R"({"data":{"rows":[{"id":"a","name":"账号 A"},{"id":"b","name":"账号 B"}]}})",source,&options,&error));
        QCOMPARE(options.size(),2);QCOMPARE(options[1].value,QString("b"));
        QVERIFY(!OptionsSource::parse(R"({"data":{"rows":[{"id":"a","name":"A"},{"id":"a","name":"B"}]}})",source,&options,&error));QVERIFY(options.isEmpty());
        source={{"format","delimited"},{"delimiter","|"},{"value_column",4},{"label_column",1},{"columns",4},{"value_pattern","[A-Za-z0-9_-]+"}};
        QVERIFY(OptionsSource::parse("实例 A | NPU | stopped | key-1\n",source,&options,&error));QCOMPARE(options[0].value,QString("key-1"));
        QVERIFY(!OptionsSource::parse("实例 A | NPU | stopped | key-1\nwrong\n",source,&options,&error));QVERIFY(options.isEmpty());
        source={{"format","lines"},{"empty_text","no accounts"}};QVERIFY(OptionsSource::parse("no accounts\n",source,&options,&error));QVERIFY(options.isEmpty());
    }
    void dependenciesRejectCyclesAndStaleResults(){
        auto c=echo();
        c.parameters={QJsonObject{{"key","account"},{"label",QStringLiteral("账号")},{"type","choice"},{"required",true},{"options",QJsonArray{"alpha","beta"}}},
            QJsonObject{{"key","instance"},{"label",QStringLiteral("实例")},{"type","choice"},{"required",true},{"options_source",QJsonObject{{"kind","command"},{"executable","query"},{"arguments",QJsonArray{"{account}"}}}}}};
        QString error;QVERIFY(CommandParameters::validate(c.name,c.parameters,c.arguments,&error));
        auto cyclic=c.parameters;auto first=cyclic[0].toObject();first.remove("options");first["options_source"]=QJsonObject{{"kind","file"},{"path","{instance}"}};cyclic[0]=first;
        QVERIFY(!CommandParameters::validate(c.name,cyclic,{},&error));
        QList<CommandForm::OptionsCallback> callbacks;QStringList requested;
        CommandForm form(c,{},nullptr,true,[&](QObject *,const QJsonObject &,const QHash<QString,QString> &values,CommandForm::OptionsCallback callback){requested<<values.value("account");callbacks<<callback;});
        form.show();auto *account=form.findChild<QComboBox*>("parameter_account");auto *instance=form.findChild<QComboBox*>("parameter_instance");auto *run=form.findChild<QPushButton*>("primaryButton");
        QTRY_COMPARE(callbacks.size(),1);QCOMPARE(requested[0],QString("alpha"));QVERIFY(!run->isEnabled());
        account->setCurrentIndex(1);QTRY_COMPARE(callbacks.size(),2);callbacks[0](true,{{"old","Old instance",{}}},{});
        QCOMPARE(instance->count(),0);callbacks[1](true,{{"new","New instance",{}}},{});QTRY_COMPARE(instance->count(),1);
        QCOMPARE(instance->currentIndex(),-1);instance->setCurrentIndex(0);QVERIFY(run->isEnabled());
        TestSupport::capture(form, "linked-parameters");
        account->setCurrentIndex(0);QVERIFY(instance->currentData().toString().isEmpty());QVERIFY(!run->isEnabled());
        QTRY_COMPARE(callbacks.size(),3);callbacks[2](false,{},"offline");QVERIFY(!run->isEnabled());
        TestSupport::capture(form, "linked-error");
    }
    void optionsReadFromFileAndCommand(){
        QTemporaryDir dir;auto t=tool();t.workingDirectory=dir.path();QVERIFY(write(dir.filePath("options.json"),R"(["alpha","beta"] )"));
        bool done=false,ok=false;QList<CommandParameters::Option> options;
        OptionsSource::read(this,t,{{"kind","file"},{"path","options.json"},{"format","json"}},{},[&](bool success,const auto &rows,const auto &){ok=success;options=rows;done=true;});
        QTRY_VERIFY(done);QVERIFY(ok);QCOMPARE(options.size(),2);
        done=false;
        OptionsSource::read(this,t,{{"kind","command"},{"executable",QCoreApplication::applicationFilePath()},{"arguments",QJsonArray{"--echo-args","{account}"}},{"format","json"}},{{"account","中文 ' $()"}},[&](bool success,const auto &rows,const auto &){ok=success;options=rows;done=true;});
        QTRY_VERIFY(done);QVERIFY(ok);QCOMPARE(options[0].value,QStringLiteral("中文 ' $()"));
    }
    void genericOptionDiscoveryAndPreservation(){
        const auto parameter=[](QString key){return QJsonObject{{"key",key},{"label",key},{"type","string"},{"required",true},{"flag","--"+key},{"omit_empty",false}};};
        AppDatabase::ToolCommand projects;projects.name="list-projects";projects.description="List available projects";projects.executable="orbit";projects.arguments={"list-projects"};
        auto regions=projects;regions.name="regions";regions.description="List regions for a project";regions.arguments={"regions","--project","{project}"};regions.parameters={parameter("project")};
        AppDatabase::ToolCommand deploy;deploy.name="deploy";deploy.executable="orbit";deploy.arguments={"deploy","--project","{project}","--region","{region}"};deploy.parameters={parameter("project"),parameter("region")};
        auto original=deploy;OptionsDiscovery::apply(&deploy,{projects,regions});
        QCOMPARE(deploy.parameters[0].toObject()["type"].toString(),QString("choice"));QCOMPARE(deploy.parameters[1].toObject()["type"].toString(),QString("choice"));
        QCOMPARE(CommandParameters::sourceDependencies(deploy.parameters[1].toObject()["options_source"].toObject()),(QStringList{"project"}));
        OptionsDiscovery::mergeUnchangedParameters(&original,deploy);QCOMPARE(original.parameters,deploy.parameters);
        auto edited=deploy;auto custom=parameter("project");custom["label"]="My project";edited.parameters[0]=custom;OptionsDiscovery::mergeUnchangedParameters(&edited,deploy);QCOMPARE(edited.parameters[0].toObject()["type"].toString(),QString("string"));
        auto duplicate=projects;duplicate.name="projects";auto ambiguous=deploy;ambiguous.parameters={parameter("project")};OptionsDiscovery::apply(&ambiguous,{projects,duplicate});QCOMPARE(ambiguous.parameters[0].toObject()["type"].toString(),QString("string"));
        duplicate.name="delete-projects";QVERIFY(!OptionsDiscovery::isListQuery(duplicate));
        auto models=projects;models.name="models";models.executable="other-tool";models.description="List supported models";models.arguments={"models"};
        auto render=deploy;render.name="render";render.executable="other-tool";render.arguments={"render","--model","{model}"};render.parameters={parameter("model")};OptionsDiscovery::apply(&render,{models});QCOMPARE(render.parameters[0].toObject()["type"].toString(),QString("choice"));
    }
    void sourceChangesAndDependencySafeMerge(){
        QJsonObject source{{"kind","command"},{"format","auto"},{"executable","fixture"},{"arguments",QJsonArray{"list","{account}"}}};
        QTimer::singleShot(0,this,[&]{auto *dialog=QApplication::activeModalWidget();QVERIFY(dialog);auto combos=dialog->findChildren<QComboBox*>();QComboBox *kind=nullptr;for(auto *combo:combos)if(combo->findData("file")>=0&&combo->findData("command")>=0)kind=combo;QVERIFY(kind);kind->setCurrentIndex(kind->findData("file"));dialog->findChild<QLineEdit*>("sourcePath")->setText("list.json");dialog->findChild<QPushButton*>("saveConfiguration")->click();});
        QVERIFY(ToolEditor::editSource(&source,tool(),nullptr));QVERIFY(!source.contains("arguments"));QVERIFY(!source.contains("executable"));QVERIFY(CommandParameters::sourceDependencies(source).isEmpty());
        source["arguments"]=QJsonArray{"{obsolete}"};QVERIFY(CommandParameters::sourceDependencies(source).isEmpty());source["kind"]="command";source["path"]="{hidden}";QCOMPARE(CommandParameters::sourceDependencies(source),(QStringList{"obsolete"}));
        QJsonObject base{{"key","region"},{"label","region"},{"type","string"},{"required",true},{"flag","--region"},{"omit_empty",false}};
        QJsonObject project{{"key","project"},{"type","choice"},{"options_source",QJsonObject{{"kind","command"},{"executable","fixture"},{"arguments",QJsonArray{"projects","{region}"}}}}};
        auto existing=echo();existing.parameters={project,base};auto proposed=base;proposed["type"]="choice";proposed["discovery_base"]=base;proposed["options_source"]=QJsonObject{{"kind","command"},{"executable","fixture"},{"arguments",QJsonArray{"regions","{project}"}}};auto candidate=existing;candidate.parameters={project,proposed};
        const auto original=existing.parameters;OptionsDiscovery::mergeUnchangedParameters(&existing,candidate);QCOMPARE(existing.parameters,original);
        existing.parameters={base};OptionsDiscovery::mergeUnchangedParameters(&existing,candidate);QCOMPARE(existing.parameters,(QJsonArray{base}));
    }
    void explicitJsonFileAcrossTargets(){
        AutomationPage page(&database_);
        auto parser=[&](const QByteArray &bytes,const QString &path,const AppDatabase::AutomationTool &source,AppDatabase::AutomationTool *tool,QList<AppDatabase::ToolCommand> *commands,QString *error){AutomationPage::ParsedManifest parsed;if(!page.parseManifest(bytes,path,source.sshHost,&parsed,error,source.wslDistribution,source.wslUser,source.sshUser))return false;*tool=parsed.tool;*commands=parsed.commands;return true;};
        QJsonObject manifest{{"schema","orchestrate-tool/v1"},{"id","json-file-fixture"},{"name","JSON fixture"},{"target_type","windows-local"},{"commands",QJsonArray{QJsonObject{{"name","list"},{"description","List fixtures"},{"executable","fixture"}}}}};
        const auto file=QCoreApplication::applicationDirPath()+"/custom-name.json";QVERIFY(write(file,QJsonDocument(manifest).toJson()));
        auto t=tool();t.workingDirectory.clear();DiscoveryDialog local(t,nullptr,parser);local.show();local.findChild<QComboBox*>("toolSourceType")->setCurrentIndex(1);local.findChild<QLineEdit*>("directoryPath")->setText(file);local.findChild<QPushButton*>("readToolDirectory")->click();
        QTRY_VERIFY(local.findChild<QLabel*>("discoveryRecommendation")->text().contains(QStringLiteral("已读取 1")));TestSupport::capture(local, "json-file-import");local.save->click();QCOMPARE(local.result(),int(QDialog::Accepted));QCOMPARE(local.configuredTool().registrationPath,file);QCOMPARE(local.selected().size(),1);
        for(const auto &target:QStringList{"wsl","ssh"}){
            manifest["target_type"]=target;t.targetType=target;t.sshHost=target=="ssh"?"fixture-host":"";t.wslDistribution=target=="wsl"?"Fixture Linux":"";
            bool readFile=false;
            auto directoryReader=[&](QObject *context,const AppDatabase::AutomationTool &request,ToolDirectory::Callback callback){QCOMPARE(request.workingDirectory,QString("~/config"));QTimer::singleShot(0,context,[callback]{callback(true,{{"directory","/home/alice/config"},{"user","alice"},{"files",QJsonArray()}},{});});};
            auto fileReader=[&](QObject *context,const AppDatabase::AutomationTool &request,const QString &path,OptionsSource::RawCallback callback){QCOMPARE(path,QString("/home/alice/config/custom-name.json"));QCOMPARE(request.targetType=="wsl"?request.wslUser:request.sshUser,QString("alice"));readFile=true;QTimer::singleShot(0,context,[callback,manifest]{callback(true,QJsonDocument(manifest).toJson(),{});});};
            DiscoveryDialog remote(t,nullptr,parser,directoryReader,fileReader);remote.show();remote.findChild<QComboBox*>("toolSourceType")->setCurrentIndex(1);remote.findChild<QLineEdit*>("directoryPath")->setText("~/config/custom-name.json");remote.save->click();QTRY_COMPARE(remote.result(),int(QDialog::Accepted));QVERIFY(readFile);QCOMPARE(remote.configuredTool().registrationPath,QString("/home/alice/config/custom-name.json"));
        }
        QVERIFY(write(file,"{ invalid JSON"));t=tool();t.workingDirectory.clear();DiscoveryDialog broken(t,nullptr,parser);broken.show();broken.findChild<QComboBox*>("toolSourceType")->setCurrentIndex(1);broken.findChild<QLineEdit*>("directoryPath")->setText(file);broken.save->click();QTRY_VERIFY(!broken.error->isHidden());QVERIFY(!broken.save->isEnabled());QVERIFY(broken.selected().isEmpty());broken.reject();
    }
    void jsonRereadFailureDiscardsCache(){
        auto t=tool();t.workingDirectory.clear();int reads=0;
        auto reader=[&](QObject *context,const AppDatabase::AutomationTool &request,ToolDirectory::Callback callback){const bool ok=++reads==1;QTimer::singleShot(0,context,[callback,request,ok]{callback(ok,{{"directory",request.workingDirectory},{"files",QJsonArray()}},ok?QString():QStringLiteral("目录暂不可访问"));});};
        auto files=[](QObject *context,const AppDatabase::AutomationTool &,const QString &,OptionsSource::RawCallback callback){QTimer::singleShot(0,context,[callback]{callback(true,"{}",{});});};
        auto parser=[](const QByteArray &,const QString &path,const AppDatabase::AutomationTool &source,AppDatabase::AutomationTool *tool,QList<AppDatabase::ToolCommand> *commands,QString *){*tool=source;tool->registrationPath=path;AppDatabase::ToolCommand command;command.name="list";command.executable="fixture";commands->append(command);return true;};
        DiscoveryDialog dialog(t,nullptr,parser,reader,files);dialog.show();dialog.findChild<QComboBox*>("toolSourceType")->setCurrentIndex(1);dialog.findChild<QLineEdit*>("directoryPath")->setText(QCoreApplication::applicationDirPath()+"/fixture.json");auto *read=dialog.findChild<QPushButton*>("readToolDirectory");auto *list=dialog.findChild<QTreeWidget*>("discoveredCommands");read->click();QTRY_COMPARE(list->topLevelItemCount(),1);QVERIFY(dialog.save->isEnabled());
        read->click();QTRY_VERIFY(!dialog.error->isHidden());QCOMPARE(list->topLevelItemCount(),0);QVERIFY(dialog.selected().isEmpty());QVERIFY(!dialog.save->isEnabled());dialog.save->click();QCOMPARE(dialog.result(),int(QDialog::Rejected));TestSupport::capture(dialog, "json-file-reread-error");dialog.reject();
    }
    void automaticOptionFormats(){
        QJsonObject config{{"format","auto"},{"value_hint","instance_key"}};QList<CommandParameters::Option> options;QString error;
        QVERIFY2(OptionsSource::parse(R"({"data":{"items":[{"instance_key":"full-key-1","name":"First","status":"ready"}]}})",config,&options,&error),qPrintable(error));QCOMPARE(options[0].value,QString("full-key-1"));QCOMPARE(options[0].label,QString("First"));
        QVERIFY(OptionsSource::parse("id | name\na-1 | Alpha\nb-2 | Beta\n",config,&options,&error));QCOMPARE(options.size(),2);QCOMPARE(options[1].value,QString("b-2"));
        QVERIFY(OptionsSource::parse("Alice\nBob\n",config,&options,&error));QCOMPARE(options.size(),2);
        QVERIFY(OptionsSource::parse(QStringLiteral("还没有账号，请先导入。").toUtf8(),config,&options,&error));QVERIFY(options.isEmpty());
        QVERIFY(!OptionsSource::parse("Alpha | GPU | ready | full-key-1\n",config,&options,&error));QVERIFY(options.isEmpty());QVERIFY(error.contains(QStringLiteral("列名")));
        QVERIFY(!OptionsSource::parse(R"({"a":[1],"b":[2]})",config,&options,&error));
        QVERIFY(!OptionsSource::parse("id | name\na... | Alpha\n",config,&options,&error));QVERIFY(options.isEmpty());
        QVERIFY(!OptionsSource::parse("id | name\na | Alpha\nb\n",config,&options,&error));QVERIFY(options.isEmpty());
        QVERIFY(!OptionsSource::parse("Warning: stale credentials\nAlice\n",config,&options,&error));
    }
    void chooseOutputColumnsOnce(){
        const QString path=QCoreApplication::applicationDirPath()+"/output-columns.txt";const auto bytes=QByteArray("Alpha | GPU | stopped | full-key-1\nBeta | CPU | running | full-key-2\n");QVERIFY(write(path,bytes));
        QJsonObject source{{"kind","file"},{"path",path},{"format","auto"}};bool handled=false;
        QTimer poll;connect(&poll,&QTimer::timeout,this,[&]{auto *dialog=QApplication::activeModalWidget();if(!dialog||handled)return;
            auto *test=dialog->findChild<QPushButton*>("testOptionsSource");auto *actual=dialog->findChild<QComboBox*>("sourceActualColumn");
            if(actual->count()==0){if(test->isEnabled())test->click();return;}
            handled=true;QCOMPARE(actual->count(),4);QCOMPARE(actual->currentIndex(),-1);TestSupport::capture(*dialog, "options-column-mapping");actual->setCurrentIndex(3);dialog->findChild<QPushButton*>("saveConfiguration")->click();
        });poll.start(20);QVERIFY(ToolEditor::editSource(&source,tool(),nullptr));poll.stop();QVERIFY(handled);QCOMPARE(source["value_column"].toInt(),4);QCOMPARE(source["label_column"].toInt(),1);
        QList<CommandParameters::Option> options;QString error;QVERIFY(OptionsSource::parse(bytes,source,&options,&error));QCOMPARE(options[0].value,QString("full-key-1"));QCOMPARE(options[0].label,QString("Alpha"));
    }
    void discoveredDropdownDependencies(){
        auto c=echo();c.name=QStringLiteral("通用工具 · 选择项目和区域");c.parameters={QJsonObject{{"key","project"},{"label",QStringLiteral("项目")},{"type","choice"},{"required",true},{"options_source",QJsonObject{{"kind","command"},{"format","auto"},{"executable","fixture"},{"arguments",QJsonArray{"projects"}}}}},QJsonObject{{"key","region"},{"label",QStringLiteral("区域")},{"type","choice"},{"required",true},{"options_source",QJsonObject{{"kind","command"},{"format","auto"},{"executable","fixture"},{"arguments",QJsonArray{"regions","--project","{project}"}}}}}};
        int regionReads=0,edits=0;QString editedKey;
        CommandForm form(c,{},nullptr,true,[&](QObject *context,const QJsonObject &source,const QHash<QString,QString> &values,CommandForm::OptionsCallback callback){
            const bool dependent=CommandParameters::sourceDependencies(source).contains("project");if(dependent){++regionReads;QVERIFY(!values.value("project").isEmpty());}
            QList<CommandParameters::Option> rows=dependent?QList<CommandParameters::Option>{{values.value("project")+"-region",QStringLiteral("区域 1"),{}}}:QList<CommandParameters::Option>{{"alpha",QStringLiteral("项目 A"),{}},{"beta",QStringLiteral("项目 B"),{}}};QTimer::singleShot(0,context,[callback,rows]{callback(true,rows,{});});
        },[&](const QString &key,QJsonObject *source,const QHash<QString,QString> &,QWidget *){editedKey=key;++edits;(*source)["format"]="json";return true;});
        form.show();auto *project=form.findChild<QComboBox*>("parameter_project");auto *region=form.findChild<QComboBox*>("parameter_region");QTRY_COMPARE(project->count(),2);QCOMPARE(regionReads,0);project->setCurrentIndex(0);QTRY_COMPARE(region->count(),1);region->setCurrentIndex(0);QCOMPARE(form.values()["region"],QString("alpha-region"));
        TestSupport::capture(form, "discovered-dropdowns");project->setCurrentIndex(1);QTRY_COMPARE(region->count(),1);QCOMPARE(region->currentIndex(),-1);region->setCurrentIndex(0);QCOMPARE(form.values()["region"],QString("beta-region"));
        form.findChild<QPushButton*>("configure_options_region")->click();QCOMPARE(edits,1);QCOMPARE(editedKey,QString("region"));QTRY_VERIFY(regionReads>=3);form.reject();
    }
    void helpDiscoveryAndOptionalArguments(){
        const QString root="usage: tool {run,list}\n\npositional arguments:\n  {run,list}\n    run       Run a job\n    list      List choices\n\noptions:\n  --help     help\n";
        auto commands=CliDiscovery::commands(root,{"tool",{},"fixture"});QCOMPARE(commands.size(),2);QCOMPARE(commands[0].description,QString("Run a job"));
        QStringList args={"run"};
        const QString help="usage: tool run --account ACCOUNT [--minutes MINUTES] (--clipboard | --file FILE)\n\noptions:\n  --account ACCOUNT\n  --minutes MINUTES\n  --clipboard\n  --file FILE\n";
        auto parameters=CliDiscovery::parameters(help,{{"minutes",20}},&args);QCOMPARE(parameters.size(),4);
        QCOMPARE(parameters[0].toObject()["required"].toBool(),true);QCOMPARE(parameters[1].toObject()["type"].toString(),QString("number"));
        QCOMPARE(parameters[2].toObject()["exclusive_required"].toBool(),true);
        const auto expanded=CommandArguments::expand(args,{{"account","A"},{"minutes",""},{"clipboard","--clipboard"},{"file",""}},parameters);
        QCOMPARE(expanded,(QStringList{"run","--account","A","--clipboard"}));
        QStringList pasted;QString error;QVERIFY(CommandLine::split("python -m package start --account \"Account A\" --minutes 20",&pasted,&error));
        auto found=CliDiscovery::parametersFromArguments(&pasted);QCOMPARE(found.size(),2);QCOMPARE(found[0].toObject()["default"].toString(),QString("Account A"));
    }
    void discoveryPrefersLocalToolAndInterpreter(){
        const QJsonObject inventory{{"files",QJsonArray{".venv/Scripts/python.exe","worker/__main__.py"}},{"documents",QJsonArray{QJsonObject{{"name","README.md"},{"text","python -m pip install example\npython -m venv .venv\npython -m worker"}}}}};
        const auto entries=CliDiscovery::entries(inventory,"D:/fixture");QCOMPARE(entries.size(),1);QCOMPARE(entries.first().program,QString("D:/fixture/.venv/Scripts/python.exe"));QCOMPARE(entries.first().arguments,(QStringList{"-m","worker"}));
    }
    void commandLineRoundTrip(){
        const QStringList expected{"python", "", QStringLiteral("C:\\中文 路径\\"), "a\"b", "quoted\\\"value", "one\ttwo", "x'y"};
        QStringList actual;QString error;QVERIFY2(CommandLine::split(CommandLine::join(expected),&actual,&error),qPrintable(error));QCOMPARE(actual,expected);
    }
    void windowsTerminalKeepsLiteralArguments(){
        auto t=tool();auto c=echo();const QStringList payload{"",QStringLiteral("中文路径 C:\\目录\\"),"a\"b","$(nothing); & | >","'quoted'","two words"};
        auto spec=CommandLaunch::build(t,c,c.arguments+payload);spec.environment={{"ORCHESTRATE_TEST_ENV","hello"}};
        const QString script=CommandLaunch::terminalScript(spec);QByteArray bytes;for(auto character:script){bytes+=char(character.unicode()&255);bytes+=char(character.unicode()>>8);}
        QProcess ps;ps.start("powershell.exe",{"-NoProfile","-EncodedCommand",QString::fromLatin1(bytes.toBase64())});QVERIFY(ps.waitForFinished(15000));
        const auto output=ps.readAllStandardOutput();QVERIFY2(output.contains("Exit code: 0"),output.constData());
        QCOMPARE(QJsonDocument::fromJson(output.split('\n').first()).array(),QJsonArray::fromStringList(payload));
        QCOMPARE(ps.exitCode(),0);
    }
    void builtinCustomizationSurvivesSync(){
        const QString directory=QCoreApplication::applicationDirPath()+"/tools/synthetic";QDir().mkpath(directory);
        QJsonObject manifest{{"schema","orchestrate-tool/v1"},{"id","builtin-test"},{"name","Builtin fixture"},{"target_type","windows-local"},
            {"commands",QJsonArray{QJsonObject{{"name","default"},{"executable","fixture.exe"}}}}};
        QVERIFY(write(directory+"/orchestrate-tool.json",QJsonDocument(manifest).toJson()));
        AutomationPage page(&database_);AppDatabase::AutomationTool t;for(const auto &candidate:database_.automationTools())if(candidate.externalId=="builtin-test")t=candidate;
        QVERIFY(t.id>0);auto commands=database_.toolCommands(t.id);commands[0].name="my command";commands[0].enabled=false;QVERIFY(page.saveUserCommands(t.id,commands));
        page.syncBuiltinTools();auto saved=database_.toolCommands(t.id);QCOMPARE(saved[0].name,QString("my command"));QVERIFY(!saved[0].enabled);QCOMPARE(saved[0].id,commands[0].id);
        AutomationPage::ParsedManifest parsed;QString error;QVERIFY(page.parseManifest(QJsonDocument(manifest).toJson(),directory+"/orchestrate-tool.json",{},&parsed,&error));
        QTimer::singleShot(0,&page,[&]{auto *preview=page.findChild<QDialog*>("manifestUpdatePreview");QVERIFY(preview);TestSupport::capture(*preview, "replacement-preview");preview->reject();});
        QVERIFY(!page.confirmManifestUpdate(t,parsed));QCOMPARE(database_.toolCommands(t.id)[0].name,QString("my command"));
        QVERIFY(QFile::remove(directory+"/orchestrate-tool.json"));
    }
    void cancelledEditorsPreserveConfiguration(){
        auto t=tool(QStringLiteral("文件转换工具"));
        const auto originalToolName = t.name;
        QTimer::singleShot(30,this,[&]{
            auto *dialog=QApplication::activeModalWidget();QVERIFY(dialog);
            auto *name=dialog->findChild<QLineEdit*>("toolEditorName");QVERIFY(name);name->setText("discarded tool name");
            TestSupport::capture(*dialog,"tool-editor");qobject_cast<QDialog*>(dialog)->reject();
        });
        QVERIFY(!ToolEditor::editTool(&t,nullptr));
        QCOMPARE(t.name,originalToolName);
        auto c=echo();c.arguments={"input.txt","--output","{output}"};c.parameters={QJsonObject{{"key","output"},{"label",QStringLiteral("输出目录")},{"type","directory"},{"required",true}}};
        const auto originalCommandName=c.name;
        const auto originalArguments=c.arguments;
        const auto originalParameters=c.parameters;
        QTimer::singleShot(30,this,[&]{
            auto *dialog=QApplication::activeModalWidget();QVERIFY(dialog);
            auto *name=dialog->findChild<QLineEdit*>("commandEditorName");QVERIFY(name);name->setText("discarded command name");
            TestSupport::capture(*dialog,"command-editor");qobject_cast<QDialog*>(dialog)->reject();
        });
        QVERIFY(!ToolEditor::editCommand(&c,t,nullptr));
        QCOMPARE(c.name,originalCommandName);QCOMPARE(c.arguments,originalArguments);QCOMPARE(c.parameters,originalParameters);
        QJsonObject source{{"kind","command"},{"executable","python"},{"arguments",QJsonArray{"-m","example","instances","--account","{account}"}},{"format","json"},{"items_path","items"},{"value_key","id"},{"label_key","name"}};
        const auto originalSource=source;
        QTimer::singleShot(30,this,[&]{
            auto *dialog=QApplication::activeModalWidget();QVERIFY(dialog);
            auto *command=dialog->findChild<QLineEdit*>("sourceCommand");QVERIFY(command);command->setText("discarded-query --flag");
            TestSupport::capture(*dialog,"options-source-editor");qobject_cast<QDialog*>(dialog)->reject();
        });
        QVERIFY(!ToolEditor::editSource(&source,t,nullptr));
        QCOMPARE(source,originalSource);
    }
    void remoteDirectoryProtocolAndLaunch(){
        QByteArray wire="orchestrate-directory-v1";wire+='\0';
        for(const QByteArray &part:{QByteArray("R"),QByteArray("/srv/tool with 'quotes'"),QByteArray("U"),QByteArray("alice"),QByteArray("P"),QByteArray("python3"),QByteArray("F"),QByteArray(".venv/bin/python"),QByteArray("F"),QByteArray("worker/__main__.py"),QByteArray("S"),QByteArray("{\"minutes\":20,\"secret\":\"not copied\"}")}){wire+=part;wire+='\0';}
        QJsonObject inventory;QString error;QVERIFY2(ToolDirectory::decode(wire,&inventory,&error),qPrintable(error));
        QCOMPARE(inventory["user"].toString(),QString("alice"));QVERIFY(!inventory["defaults"].toObject().contains("secret"));
        const auto entries=CliDiscovery::entries(inventory,inventory["directory"].toString(),false);QCOMPARE(entries.size(),1);QCOMPARE(entries[0].program,QString("/srv/tool with 'quotes'/.venv/bin/python"));
        auto t=tool();t.targetType="wsl";t.wslDistribution="Fixture Linux";t.wslUser="alice";t.workingDirectory="~/tool ' $(not executed)";
        const auto wsl=ToolDirectory::launch(t);QCOMPARE(wsl.arguments.last(),t.workingDirectory);QVERIFY(wsl.arguments.contains("--exec"));QVERIFY(wsl.arguments.contains("alice"));
        t.targetType="ssh";t.wslDistribution.clear();t.wslUser.clear();t.sshHost="fixture-alias";t.sshUser="alice";
        const auto ssh=ToolDirectory::launch(t);QCOMPARE(ssh.program,QString("ssh"));QVERIFY(ssh.arguments.contains("fixture-alias"));QVERIFY(ssh.arguments.last().contains(CommandLaunch::posixQuote(t.workingDirectory)));
        QVERIFY(!ToolDirectory::decode(wire.left(wire.size()-1),&inventory,&error));
    }
    void unifiedDirectoryManifestAndTarget(){
        AutomationPage page(&database_);auto t=tool("Fixture");t.targetType="ssh";t.sshHost="fixture-alias";t.workingDirectory="~/tools/fixture";
        const QJsonObject manifest{{"schema","orchestrate-tool/v1"},{"id","remote-fixture"},{"name","Remote fixture"},{"target_type","ssh"},{"commands",QJsonArray{QJsonObject{{"name","List"},{"executable","fixture"},{"arguments",QJsonArray{"list"}}}}}};
        auto parser=[&](const QByteArray &bytes,const QString &path,const AppDatabase::AutomationTool &source,AppDatabase::AutomationTool *tool,QList<AppDatabase::ToolCommand> *commands,QString *error){
            AutomationPage::ParsedManifest parsed;if(!page.parseManifest(bytes,path,source.sshHost,&parsed,error,source.wslDistribution,source.wslUser,source.sshUser))return false;*tool=parsed.tool;*commands=parsed.commands;return true;};
        bool read=false;
        auto reader=[&](QObject *context,const AppDatabase::AutomationTool &source,ToolDirectory::Callback callback){QCOMPARE(source.sshHost,QString("fixture-alias"));QCOMPARE(source.workingDirectory,QString("~/tools/fixture"));read=true;
            QTimer::singleShot(10,context,[callback,manifest]{callback(true,{{"directory","/home/alice/tools/fixture"},{"user","alice"},{"files",QJsonArray{"orchestrate-tool.json"}},{"manifest",QString::fromUtf8(QJsonDocument(manifest).toJson())}},{});});};
        DiscoveryDialog dialog(t,nullptr,parser,reader);dialog.show();QTRY_VERIFY(read);QTRY_VERIFY(dialog.save->isEnabled());
        TestSupport::capture(dialog, "directory-ssh");dialog.save->click();QCOMPARE(dialog.result(),int(QDialog::Accepted));QCOMPARE(dialog.selected().size(),1);
        QCOMPARE(dialog.configuredTool().sshUser,QString("alice"));QCOMPARE(dialog.configuredTool().registrationPath,QString("/home/alice/tools/fixture/orchestrate-tool.json"));QCOMPARE(dialog.configuredTool().workingDirectory,QString("/home/alice/tools/fixture"));
        DiscoveryDialog empty(tool());empty.show();auto *target=empty.findChild<QComboBox*>("directoryTarget");target->setCurrentIndex(1);
        empty.findChild<QComboBox*>("directoryHost")->setCurrentText("Ubuntu-24.04");empty.findChild<QLineEdit*>("directoryPath")->setText("~/tools/example");
        TestSupport::capture(empty, "directory-wsl");empty.reject();
    }
    void liveWslDirectoryReader(){
        const QString distribution=qEnvironmentVariable("ORCHESTRATE_TEST_WSL");if(distribution.isEmpty())QSKIP("Set ORCHESTRATE_TEST_WSL for isolated WSL read-only fixture validation");
        QTemporaryDir fixture(QDir::tempPath()+QStringLiteral("/orchestrate 目录 ' $value-XXXXXX"));QVERIFY(fixture.isValid());
        QDir().mkpath(fixture.path()+"/worker");
        QVERIFY(write(fixture.path()+"/worker/__main__.py","import argparse\np=argparse.ArgumentParser()\ns=p.add_subparsers()\ns.add_parser('list', help='List fixture items')\np.parse_args()\n"));
        QVERIFY(write(fixture.path()+"/settings.json","{\"minutes\":20}"));
        QString path=QDir::fromNativeSeparators(fixture.path());path="/mnt/"+path.left(1).toLower()+path.mid(2);
        auto t=tool();t.targetType="wsl";t.wslDistribution=distribution;t.workingDirectory=path;
        bool done=false,success=false;QJsonObject inventory;QString failure;
        ToolDirectory::read(this,t,[&](bool ok,const QJsonObject &value,const QString &error){done=true;success=ok;inventory=value;failure=error;});
        QTRY_VERIFY_WITH_TIMEOUT(done,35000);QVERIFY2(success,qPrintable(failure));QCOMPARE(inventory["directory"].toString(),path);QVERIFY(!inventory["user"].toString().isEmpty());
        const auto entries=CliDiscovery::entries(inventory,path,false);QVERIFY(!entries.isEmpty());QCOMPARE(entries[0].arguments,(QStringList{"-m","worker"}));
        t.wslUser=inventory["user"].toString();AppDatabase::ToolCommand command;command.executable=entries[0].program;command.arguments=entries[0].arguments;done=false;QByteArray help;
        CliDiscovery::help(this,t,command,[&](bool ok,const QByteArray &bytes,const QString &error){done=true;success=ok;help=bytes;failure=error;});
        QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(failure));QVERIFY(help.contains("List fixture items"));
    }
    void defaultDiscoveryImportsCommandsAndDisplaysDescriptions(){
        const QString directory=cliFixture_.path();
        auto t=tool("CANNLab");t.workingDirectory=directory;DiscoveryDialog dialog(t);dialog.show();
        QTRY_VERIFY(!dialog.findChild<QComboBox*>("discoveryEntry")->currentText().isEmpty());QTRY_VERIFY(dialog.save->isEnabled());TestSupport::capture(dialog, "cannlab-recommended");
        QCOMPARE(dialog.save->text(),QStringLiteral("按默认配置接入"));dialog.save->click();
        QTRY_COMPARE_WITH_TIMEOUT(dialog.result(),int(QDialog::Accepted),15000);
        QCOMPARE(dialog.selected().size(),8);AppDatabase::ToolCommand menu;for(const auto &c:dialog.selected()){if(c.name=="menu")menu=c;if(c.name=="start"){QVERIFY(c.interactive);QCOMPARE(c.parameters.size(),8);}}QCOMPARE(menu.name,QString("menu"));QVERIFY(menu.interactive);QVERIFY(menu.parameters.isEmpty());
        QCOMPARE(menu.arguments,(QStringList{"-m","cannlab","menu"}));QCOMPARE(dialog.environment().value("PYTHONUTF8").toString(),QString("1"));
        QString error;QVERIFY2(database_.saveAutomationTool(t,dialog.selected(),&t.id,&error),qPrintable(error));
        AutomationPage page(&database_);page.resize(1180,760);page.show();page.loadAll(t.id);QCoreApplication::processEvents();
        const auto saved=database_.toolCommands(t.id);
        for(int i=0;i<saved.size();++i){
            QVERIFY2(!saved[i].description.trimmed().isEmpty(),qPrintable(saved[i].name));
            auto *row=page.commandList_->itemWidget(page.commandList_->item(i));auto *description=row->findChild<QLabel*>("muted");
            QVERIFY(description);QVERIFY(!description->isHidden());QCOMPARE(description->text(),saved[i].description);
        }
        QCOMPARE(menu.description,QStringLiteral("交互菜单"));
        QTest::qWait(40);QVERIFY(page.commandList_->visualItemRect(page.commandList_->item(6)).bottom()<=page.commandList_->viewport()->height());TestSupport::capture(page, "command-descriptions-desktop");page.resize(900,600);QCoreApplication::processEvents();TestSupport::capture(page, "command-descriptions-compact");
        auto longCommands=saved;longCommands[0].description=QStringLiteral("<账号> & 实例：保留原文\n")+QStringLiteral("用于验证较长介绍在窄窗口仍不挤压命令操作。").repeated(20);longCommands[1].description.clear();
        QVERIFY(page.saveUserCommands(t.id,longCommands));page.loadToolDetails(t.id);QCoreApplication::processEvents();
        auto *longRow=page.commandList_->itemWidget(page.commandList_->item(0));auto *longDescription=longRow->findChild<QLabel*>("muted");QVERIFY(longDescription);QCOMPARE(longDescription->accessibleName(),longCommands[0].description);QVERIFY(page.commandList_->item(0)->toolTip().contains(QStringLiteral("&lt;账号&gt;")));QVERIFY(longDescription->width()<page.commandList_->width());
        QVERIFY(!page.commandList_->itemWidget(page.commandList_->item(1))->findChild<QLabel*>("muted"));
        TestSupport::capture(page, "command-description-long");QVERIFY(database_.deleteAutomationTool(t.id));
    }
    void directoryWithoutHelpAllowsManualCommand(){
        auto t=tool("Custom tool");bool ready=false;
        auto reader=[&](QObject *context,const AppDatabase::AutomationTool &source,ToolDirectory::Callback callback){QTimer::singleShot(0,context,[&,source,callback]{callback(true,{{"directory",source.workingDirectory},{"files",QJsonArray()}},{});ready=true;});};
        DiscoveryDialog dialog(t,nullptr,{},reader);dialog.show();QTRY_VERIFY(ready);QVERIFY(dialog.findChild<QPushButton*>("customizeDiscovery")->isChecked());
        QTimer::singleShot(0,this,[]{auto *editor=QApplication::activeModalWidget();QVERIFY(editor);editor->findChild<QLineEdit*>("commandEditorName")->setText("Direct command");editor->findChild<QLineEdit*>("commandEditorProgram")->setText("fixture.exe");editor->findChild<QPushButton*>("saveConfiguration")->click();});
        dialog.findChild<QPushButton*>("addDiscoveredCommand")->click();QCOMPARE(dialog.findChild<QTreeWidget*>("discoveredCommands")->topLevelItemCount(),1);QVERIFY(dialog.save->isEnabled());
        dialog.save->click();QCOMPARE(dialog.result(),int(QDialog::Accepted));QCOMPARE(dialog.selected().first().executable,QString("fixture.exe"));QVERIFY(dialog.selected().first().parameters.isEmpty());
    }
    void changingEntryDiscardsOldCommands(){
        const QString directory=cliFixture_.path();
        auto t=tool("CANNLab");t.workingDirectory=directory;DiscoveryDialog dialog(t);dialog.show();
        auto *entry=dialog.findChild<QComboBox*>("discoveryEntry");QTRY_VERIFY(!entry->currentText().isEmpty());
        auto *customize=dialog.findChild<QPushButton*>("customizeDiscovery");
        const auto closedIcon = customize->icon().pixmap(20,20).toImage();
        customize->click();
        QCOMPARE(customize->toolTip(), QStringLiteral("收起自行挑选与调整"));
        QCOMPARE(customize->accessibleName(), customize->toolTip());
        QVERIFY(closedIcon != customize->icon().pixmap(20,20).toImage());
        auto *read=dialog.findChild<QPushButton*>("readCommandHelp");read->click();
        auto *list=dialog.findChild<QTreeWidget*>("discoveredCommands");QTRY_COMPARE_WITH_TIMEOUT(list->topLevelItemCount(),8,15000);list->topLevelItem(0)->setCheckState(0,Qt::Checked);
        entry->setCurrentText("orchestrate-nonexistent-entry");QCOMPARE(list->topLevelItemCount(),0);QVERIFY(dialog.selected().isEmpty());QVERIFY(!dialog.save->isEnabled());
        read->click();QTRY_VERIFY(!dialog.error->isHidden());QVERIFY(!dialog.save->isEnabled());QCOMPARE(dialog.result(),int(QDialog::Rejected));
        TestSupport::capture(dialog, "directory-entry-error");
        customize->click();
        QCOMPARE(customize->toolTip(), QStringLiteral("展开自行挑选与调整"));
        QVERIFY(dialog.save->isEnabled());dialog.save->click();QTRY_VERIFY(!dialog.error->isHidden());QCOMPARE(dialog.result(),int(QDialog::Rejected));QVERIFY(dialog.selected().isEmpty());
        dialog.reject();
        AutomationPage page(&database_);page.resize(900,600);page.show();for(int i=0;i<page.tree_->topLevelItemCount();++i)if(page.tree_->topLevelItem(i)->data(0,Qt::UserRole).toInt()==0)page.tree_->setCurrentItem(page.tree_->topLevelItem(i));page.showEmptyDetails(0);QVERIFY(page.emptyMessage_->text().contains(QStringLiteral("接入工具")));TestSupport::capture(page, "tool-empty");
    }
    void repeatDirectoryPreservesCustomizedMenu(){
        const QString directory=cliFixture_.path();
        auto t=tool(QStringLiteral("我的 CANNLab"));t.workingDirectory=directory;t.customized=true;t.environment={{"CUSTOM_SETTING","keep"}};
        AppDatabase::ToolCommand menu;menu.name=QStringLiteral("我的菜单");menu.executable=cliExecutable();menu.arguments={"-m","cannlab","menu"};menu.interactive=true;menu.enabled=false;
        AppDatabase::ToolCommand accounts;accounts.name="accounts";accounts.executable=cliExecutable();accounts.arguments={"-m","cannlab","accounts"};accounts.description=QStringLiteral("我自己填写的介绍");
        QString error;QVERIFY2(database_.saveAutomationTool(t,{menu,accounts},&t.id,&error),qPrintable(error));const int menuId=database_.toolCommands(t.id).first().id;
        AutomationPage page(&database_);bool filled=false;QTimer timer;
        connect(&timer,&QTimer::timeout,this,[&]{auto *dialog=page.findChild<QDialog*>("toolDiscoveryDialog");if(!dialog||filled)return;filled=true;dialog->findChild<QLineEdit*>("directoryPath")->setText(directory);dialog->findChild<QPushButton*>("saveConfiguration")->click();});
        timer.start(20);page.discoverCommands();timer.stop();QVERIFY(filled);
        auto commands=database_.toolCommands(t.id);QCOMPARE(commands.size(),8);QCOMPARE(commands.first().id,menuId);QCOMPARE(commands.first().name,menu.name);QVERIFY(!commands.first().enabled);QCOMPARE(commands.first().description,QStringLiteral("交互菜单"));QCOMPARE(commands[1].description,accounts.description);
        AppDatabase::AutomationTool saved;QVERIFY(page.findTool(t.id,&saved));QCOMPARE(saved.name,t.name);QCOMPARE(saved.environment["CUSTOM_SETTING"].toString(),QString("keep"));QCOMPARE(saved.environment["PYTHONUTF8"].toString(),QString("1"));
        int sameDirectory=0;for(const auto &candidate:database_.automationTools())if(candidate.workingDirectory==directory)++sameDirectory;QCOMPARE(sameDirectory,1);
        QVERIFY(database_.deleteAutomationTool(t.id));
    }
    void customDiscoveryReadsSelectedCommandParameters(){
        const QString directory=cliFixture_.path();
        auto t=tool("CANNLab");t.workingDirectory=directory;
        DiscoveryDialog dialog(t);dialog.show();auto *entry=dialog.findChild<QComboBox*>("discoveryEntry");QTRY_VERIFY(!entry->currentText().isEmpty());
        QCOMPARE(QProcess::splitCommand(entry->currentText()).mid(1), (QStringList{"-m", "cannlab"}));
        dialog.findChild<QPushButton*>("customizeDiscovery")->click();
        dialog.findChild<QPushButton*>("readCommandHelp")->click();auto *list=dialog.findChild<QTreeWidget*>("discoveredCommands");QTRY_COMPARE_WITH_TIMEOUT(list->topLevelItemCount(),8,15000);
        TestSupport::capture(dialog, "cannlab-discovery");
        for(int i=0;i<list->topLevelItemCount();++i)if(list->topLevelItem(i)->text(0)=="start")list->topLevelItem(i)->setCheckState(0,Qt::Checked);
        dialog.save->click();QTRY_COMPARE_WITH_TIMEOUT(dialog.result(),int(QDialog::Accepted),15000);
        const auto selected=dialog.selected();QCOMPARE(selected.size(),1);QCOMPARE(selected[0].parameters.size(),8);
        QHash<QString,QJsonObject> fields;for(const auto &value:selected[0].parameters)fields[value.toObject()["key"].toString()]=value.toObject();
        QCOMPARE(fields["minutes"]["default"].toDouble(),20.);QVERIFY(fields["account"]["required"].toBool());QVERIFY(fields["resource"]["required"].toBool());
        QCOMPARE(fields["account"]["type"].toString(),QString("choice"));QCOMPARE(fields["account"]["options_source"].toObject()["arguments"].toArray(),(QJsonArray{"-m","cannlab","accounts"}));
        QCOMPARE(fields["resource"]["options_source"].toObject()["arguments"].toArray(),(QJsonArray{"-m","cannlab","environments","--account","{account}"})); // No authenticated queries during discovery.
    }
};
int main(int argc,char **argv){
    if(FileAccess::isHelper(argc,argv)){QCoreApplication app(argc,argv);return FileAccess::runHelper(app.arguments(),false);}
    if(argc>1&&QString::fromLocal8Bit(argv[1])=="-m"){
        QCoreApplication app(argc,argv);return runCliFixture(app.arguments().mid(1));
    }
    if(argc>1&&QString::fromLocal8Bit(argv[1])=="--echo-args"){
        QCoreApplication app(argc,argv);QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 2;output.write(QJsonDocument(QJsonArray::fromStringList(app.arguments().mid(2))).toJson(QJsonDocument::Compact)+"\n");return 0;
    }
    QApplication app(argc,argv);ConfigurableToolsTest test;return QTest::qExec(&test,argc,argv);
}
#include "test_configurable_tools.moc"
