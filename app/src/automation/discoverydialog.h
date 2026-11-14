#pragma once
#include "optionsdiscovery.h"
#include "clidiscovery.h"
#include "tooleditor.h"
#include "tooldirectory.h"
#include <QTreeWidget>

class DiscoveryDialog final : public ToolEditor::Dialog {
public:
    using ManifestParser = std::function<bool(const QByteArray &,const QString &,
        const AppDatabase::AutomationTool &,AppDatabase::AutomationTool *,QList<AppDatabase::ToolCommand> *,QString *)>;
    using DirectoryReader = std::function<void(QObject *,const AppDatabase::AutomationTool &,ToolDirectory::Callback)>;
    using FileReader = std::function<void(QObject *,const AppDatabase::AutomationTool &,const QString &,OptionsSource::RawCallback)>;
    explicit DiscoveryDialog(const AppDatabase::AutomationTool &tool,QWidget *parent=nullptr,
                             ManifestParser parser={},DirectoryReader reader=ToolDirectory::read,FileReader fileReader={})
        : Dialog(QStringLiteral("接入工具"),parent),initial_(tool),tool_(tool),parser_(std::move(parser)),reader_(std::move(reader)),fileReader_(std::move(fileReader))
    {
        setObjectName("toolDiscoveryDialog");resize(700,400);
        if(!fileReader_)fileReader_=[](QObject *context,const AppDatabase::AutomationTool &target,const QString &path,OptionsSource::RawCallback callback){OptionsSource::readRaw(context,target,{{"kind","file"},{"path",path}},{},std::move(callback));};
        sourceFields_=new QWidget;auto *source=new QFormLayout(sourceFields_);source->setContentsMargins(0,0,0,0);source->setVerticalSpacing(12);body->addWidget(sourceFields_);
        sourceType_=new QComboBox;sourceType_->setObjectName("toolSourceType");sourceType_->addItem(QStringLiteral("工具目录"),"directory");sourceType_->addItem(QStringLiteral("JSON 配置文件"),"json");source->addRow(QStringLiteral("接入方式"),sourceType_);
        target_=new QComboBox;target_->setObjectName("directoryTarget");target_->addItem(QStringLiteral("本地 Windows"),"windows-local");target_->addItem("WSL","wsl");target_->addItem("SSH","ssh");
        target_->setCurrentIndex(qMax(0,target_->findData(tool.targetType)));source->addRow(QStringLiteral("执行环境"),target_);
        host_=new QComboBox;host_->setObjectName("directoryHost");host_->setEditable(true);source->addRow(QStringLiteral("主机 / 发行版"),host_);
        user_=ToolEditor::line(source,QStringLiteral("登录用户"),tool.targetType=="wsl"?tool.wslUser:tool.sshUser,"directoryUser");user_->setPlaceholderText(QStringLiteral("留空读取默认用户，接入后固定该用户"));
        directory_=new QLineEdit;directory_->setObjectName("directoryPath");
        auto *row=new QHBoxLayout;row->addWidget(directory_,1);
        browse_=new QPushButton(QStringLiteral("选择…"));browse_->setAutoDefault(false);row->addWidget(browse_);
        inspect_=new QPushButton(QStringLiteral("读取"));inspect_->setObjectName("readToolDirectory");inspect_->setAutoDefault(false);row->addWidget(inspect_);auto *pathLabel=new QLabel(QStringLiteral("工具目录"));source->addRow(pathLabel,row);
        recommendation_=ToolEditor::hint(QStringLiteral("默认接入全部命令。"));recommendation_->setObjectName("discoveryRecommendation");body->addWidget(recommendation_);
        customize_=new QPushButton(QStringLiteral("自行挑选与调整…"));customize_->setObjectName("customizeDiscovery");customize_->setCheckable(true);customize_->setAutoDefault(false);body->addWidget(customize_,0,Qt::AlignLeft);
        details_=new QWidget;auto *detailsLayout=new QVBoxLayout(details_);detailsLayout->setContentsMargins(0,0,0,0);detailsLayout->setSpacing(12);body->addWidget(details_);details_->hide();
        auto *form=new QFormLayout;detailsLayout->addLayout(form);
        entry_=new QComboBox;entry_->setEditable(true);entry_->setObjectName("discoveryEntry");entry_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
        form->addRow(QStringLiteral("启动入口"),entry_);entry_->lineEdit()->setPlaceholderText(QStringLiteral("例如 python -m package，或目标环境中的程序路径"));
        read_=new QPushButton(QStringLiteral("读取命令与介绍"));read_->setObjectName("readCommandHelp");read_->setAutoDefault(false);form->addRow(QString(),read_);
        status_=ToolEditor::hint(QStringLiteral("勾选需要接入的命令。"));detailsLayout->addWidget(status_);
        list_=new QTreeWidget;list_->setObjectName("discoveredCommands");list_->setRootIsDecorated(false);list_->setColumnCount(2);
        list_->setHeaderLabels({QStringLiteral("命令"),QStringLiteral("简单介绍")});list_->header()->setSectionResizeMode(0,QHeaderView::ResizeToContents);list_->header()->setSectionResizeMode(1,QHeaderView::Stretch);list_->setMinimumHeight(250);detailsLayout->addWidget(list_,1);
        edit_=new QPushButton(QStringLiteral("查看参数并编辑…"));edit_->setAutoDefault(false);
        auto *commandActions=new QHBoxLayout;commandActions->addWidget(edit_);
        add_=new QPushButton(QStringLiteral("添加命令…"));add_->setObjectName("addDiscoveredCommand");add_->setAutoDefault(false);commandActions->addWidget(add_);commandActions->addStretch();detailsLayout->addLayout(commandActions);
        help_=new QPlainTextEdit;help_->setObjectName("discoveryHelp");help_->setReadOnly(true);help_->setMaximumHeight(100);detailsLayout->addWidget(help_);
        save->setText(QStringLiteral("按默认配置接入"));
        connect(customize_,&QPushButton::toggled,this,[this](bool enabled){
            details_->setVisible(enabled);save->setText(enabled?QStringLiteral("加入所选命令"):QStringLiteral("按默认配置接入"));resize(enabled?860:700,enabled?820:400);setBusy(busy_);
        });
        connect(entry_,&QComboBox::currentTextChanged,this,[this]{
            if (!inventoryReady_ || busy_) return;
            clearCommands();manifest_=false;manifestError_=false;
            recommendation_->setText(QStringLiteral("启动入口已改变，请重新读取命令与介绍。"));setBusy(false);
        });
        connect(browse_,&QPushButton::clicked,this,[this]{
            const auto path=jsonFile()?QFileDialog::getOpenFileName(this,QStringLiteral("选择工具配置"),directory_->text(),QStringLiteral("JSON 配置 (*.json)")):QFileDialog::getExistingDirectory(this,QStringLiteral("选择工具目录"),directory_->text());
            if(!path.isEmpty()){directory_->setText(path);readDirectory(false);}
        });
        connect(inspect_,&QPushButton::clicked,this,[this]{readDirectory(false);});
        connect(read_,&QPushButton::clicked,this,[this]{if(inventoryReady_)readRoot(false);else readDirectory(false);});
        connect(add_,&QPushButton::clicked,this,[this]{
            if (busy_ || !inventoryReady_) return;
            AppDatabase::ToolCommand command;
            if (!ToolEditor::editCommand(&command,tool_,this,commands_)) return;
            const int index=commands_.size();commands_.append(command);loaded_.insert(index);customized_.insert(index);
            auto *item=new QTreeWidgetItem(list_,{command.name,command.description});item->setData(0,Qt::UserRole,index);item->setCheckState(0,Qt::Checked);list_->setCurrentItem(item);problem({});setBusy(false);
        });
        connect(edit_,&QPushButton::clicked,this,[this]{auto *item=list_->currentItem();if(item)readCandidate(item->data(0,Qt::UserRole).toInt(),true);});
        connect(save,&QPushButton::clicked,this,[this]{
            if(busy_)return;
            if(!inventoryReady_){readDirectory(!customize_->isChecked());return;}
            if(!customize_->isChecked()) { addDefault(); return; }
            pending_.clear();selected_.clear();
            for(int i=0;i<list_->topLevelItemCount();++i)if(list_->topLevelItem(i)->checkState(0)==Qt::Checked)pending_<<i;
            if(pending_.isEmpty()&&!(manifest_&&commands_.isEmpty())){problem(QStringLiteral("请勾选至少一条命令，或先读取命令帮助。"));return;}
            adding_=true;readNext();
        });
        const auto changeTarget=[this,source]{
            const bool local=target_->currentData()=="windows-local";
            source->setRowVisible(host_,!local);source->setRowVisible(user_,!local);browse_->setVisible(local);
            directory_->setPlaceholderText(local?QStringLiteral("选择本机的工具目录"):QStringLiteral("例如 ~/tools/example 或 /opt/example"));
            host_->clear();user_->clear();directory_->clear();
            const auto revision=++targetRevision_;
            if(target_->currentData()=="ssh")host_->addItems(SshConnection::Config::load().aliases);
            else if(target_->currentData()=="wsl")WslProcess::capture(this,{"--list","--quiet"},[this,revision](bool ok,const QByteArray &bytes,const QString &){
                if(!ok||revision!=targetRevision_)return;
                const auto previous=host_->currentText();const QSignalBlocker blocker(host_);host_->addItems(WslProcess::distributions(bytes));
                if(!previous.isEmpty())host_->setCurrentText(previous);
            });
            invalidate();
        };
        connect(target_,&QComboBox::currentIndexChanged,this,changeTarget);
        connect(sourceType_,&QComboBox::currentIndexChanged,this,[this,pathLabel,form]{
            directory_->clear();pathLabel->setText(jsonFile()?QStringLiteral("配置文件"):QStringLiteral("工具目录"));
            directory_->setPlaceholderText(jsonFile()?QStringLiteral("选择或填写目标环境中的 .json 文件路径"):QStringLiteral("选择或填写工具目录"));
            form->setRowVisible(entry_,!jsonFile());form->setRowVisible(read_,!jsonFile());invalidate();
        });
        connect(host_,&QComboBox::currentTextChanged,this,[this]{invalidate();});
        connect(user_,&QLineEdit::textChanged,this,[this]{invalidate();});
        connect(directory_,&QLineEdit::textChanged,this,[this]{invalidate();});
        changeTarget();
        host_->setCurrentText(tool.targetType=="wsl"?tool.wslDistribution:tool.sshHost);
        user_->setText(tool.targetType=="wsl"?tool.wslUser:tool.sshUser);
        if(!tool.registrationPath.isEmpty())sourceType_->setCurrentIndex(sourceType_->findData("json"));
        const auto directory=tool.registrationPath.isEmpty()?tool.workingDirectory:tool.registrationPath;
        directory_->setText(directory);
        sourceFields_->setEnabled(tool.id==0);inspect_->setEnabled(true);
        if(tool.id>0)customize_->setChecked(true);
        if(!directory.isEmpty())QTimer::singleShot(0,this,[this]{readDirectory(false);});
        setBusy(false);
    }
    QList<AppDatabase::ToolCommand> selected() const {return selected_;}
    AppDatabase::AutomationTool configuredTool() const {return tool_;}
    QJsonObject environment() const {return tool_.environment;}
private:
    bool jsonFile() const{return sourceType_->currentData()=="json";}
    void clearCommands(){
        commands_.clear();selected_.clear();pending_.clear();loaded_.clear();unavailable_.clear();customized_.clear();adding_=false;list_->clear();help_->clear();
    }
    void invalidate(){
        inventoryReady_=false;manifest_=false;manifestError_=false;commands_.clear();selected_.clear();pending_.clear();loaded_.clear();unavailable_.clear();customized_.clear();defaults_={};
        if(entry_)entry_->clear();if(list_)list_->clear();
        recommendation_->setText(jsonFile()?QStringLiteral("读取配置中的命令与参数。"):QStringLiteral("默认接入全部命令。"));problem({});setBusy(false);
    }
    void setBusy(bool busy){
        busy_=busy;sourceFields_->setEnabled(!busy&&initial_.id==0);customize_->setEnabled(!busy);entry_->setEnabled(!busy);
        read_->setEnabled(!busy);edit_->setEnabled(!busy);add_->setEnabled(!busy&&inventoryReady_);list_->setEnabled(!busy);
        save->setEnabled(!busy&&!(jsonFile()&&manifestError_)&&!directory_->text().trimmed().isEmpty()&&(!customize_->isChecked()||!commands_.isEmpty()||(manifest_&&inventoryReady_)));
    }
    bool sourceTool(AppDatabase::AutomationTool *result){
        auto t=initial_;t.targetType=target_->currentData().toString();t.workingDirectory=directory_->text().trimmed();
        t.sshHost.clear();t.sshUser.clear();t.wslDistribution.clear();t.wslUser.clear();
        if(t.workingDirectory.isEmpty()){problem(QStringLiteral("请先选择或填写工具目录。"));return false;}
        if(t.targetType=="windows-local"){
            if(!QDir::isAbsolutePath(t.workingDirectory)){problem(QStringLiteral("本地目录须使用 Windows 绝对路径。"));return false;}
        }else{
            const auto host=host_->currentText().trimmed(),user=user_->text().trimmed();
            if((t.targetType=="wsl"?!WslProcess::validDistribution(host):!SshConnection::validHost(host))
                ||(!user.isEmpty()&&!PosixRegistration::validUser(user))){problem(QStringLiteral("请填写有效的主机 / 发行版及登录用户。"));return false;}
            if(!PosixRegistration::validPath(t.workingDirectory)||!(t.workingDirectory.startsWith('/')||t.workingDirectory=="~"||t.workingDirectory.startsWith("~/"))){problem(QStringLiteral("目标目录须为 Linux 绝对路径或 ~/ 开头的路径。"));return false;}
            if(t.targetType=="wsl"){t.wslDistribution=host;t.wslUser=user;}else{t.sshHost=host;t.sshUser=user;}
        }
        if(jsonFile()){
            const auto path=t.targetType=="windows-local"?QDir::fromNativeSeparators(t.workingDirectory):t.workingDirectory;
            if(!path.endsWith(".json",Qt::CaseInsensitive)){problem(QStringLiteral("请选择 .json 配置文件。"));return false;}
            const int slash=path.lastIndexOf('/');t.workingDirectory=slash==0?"/":path.left(slash);
            if(t.targetType=="windows-local"&&t.workingDirectory.endsWith(':'))t.workingDirectory+='/';
        }
        *result=t;return true;
    }
    void readDirectory(bool thenAdd){
        if(busy_)return;AppDatabase::AutomationTool request;if(!sourceTool(&request))return;
        const auto fileName=jsonFile()?QDir::fromNativeSeparators(directory_->text().trimmed()).section('/',-1):QString();
        inventoryReady_=false;manifest_=false;manifestError_=false;clearCommands();entry_->clear();
        setBusy(true);problem({});recommendation_->setText(QStringLiteral("正在读取目标目录…"));
        reader_(this,request,[this,request,thenAdd,fileName](bool ok,const QJsonObject &inventory,const QString &error){
            setBusy(false);if(!ok){manifestError_=!fileName.isEmpty();problem(error);recommendation_->setText(QStringLiteral("目录读取失败，请检查执行环境和目录后重试。"));setBusy(false);return;}
            tool_=request;tool_.workingDirectory=inventory.value("directory").toString(request.workingDirectory);
            if(tool_.targetType=="wsl")tool_.wslUser=inventory.value("user").toString();
            if(tool_.targetType=="ssh")tool_.sshUser=inventory.value("user").toString();
            if(tool_.name.isEmpty())tool_.name=tool_.workingDirectory.section('/',-1);if(tool_.name.isEmpty())tool_.name=QStringLiteral("工具");
            const auto filePath=QDir(tool_.workingDirectory).filePath(fileName);
            {const QSignalBlocker directoryBlock(directory_),userBlock(user_);directory_->setText(fileName.isEmpty()?tool_.workingDirectory:filePath);user_->setText(tool_.targetType=="wsl"?tool_.wslUser:tool_.sshUser);}
            clearCommands();sourceTool_=tool_;defaults_=inventory.value("defaults").toObject();inventoryReady_=true;manifest_=false;manifestError_=false;entry_->clear();
            if(!fileName.isEmpty()){
                setBusy(true);recommendation_->setText(QStringLiteral("正在读取配置…"));
                fileReader_(this,tool_,filePath,[this,filePath,thenAdd](bool ok,const QByteArray &bytes,const QString &failure){
                    QString error=failure;auto parsed=tool_;QList<AppDatabase::ToolCommand> commands;
                    if(ok&&parser_)ok=parser_(bytes,filePath,tool_,&parsed,&commands,&error);else if(ok){ok=false;error=QStringLiteral("配置读取器不可用。");}
                    if(!ok){manifestError_=true;inventoryReady_=false;clearCommands();recommendation_->setText(QStringLiteral("配置读取失败"));problem(error);setBusy(false);return;}
                    tool_=parsed;commands_=commands;manifest_=true;populate();for(int i=0;i<commands_.size();++i)loaded_.insert(i);
                    recommendation_->setText(QStringLiteral("已读取 %1 条命令。").arg(commands_.size()));setBusy(false);if(thenAdd)addDefault();
                });return;
            }
            const auto entries=CliDiscovery::entries(inventory,tool_.workingDirectory,tool_.targetType=="windows-local");
            for(const auto &entry:entries)entry_->addItem(CommandLine::join(QStringList{entry.program}+entry.arguments),entry.evidence);
            const auto declaration=inventory.value("manifest").toString();
            if((!declaration.isEmpty()||inventory.value("files").toArray().contains("orchestrate-tool.json"))&&parser_){
                QString error;auto parsed=tool_;QList<AppDatabase::ToolCommand> commands;
                const auto path=tool_.workingDirectory+(tool_.workingDirectory.endsWith('/')?"":"/")+"orchestrate-tool.json";
                if(parser_(declaration.toUtf8(),path,tool_,&parsed,&commands,&error)){
                    tool_=parsed;commands_=commands;manifest_=true;populate();for(int i=0;i<commands_.size();++i)loaded_.insert(i);
                }else{manifestError_=true;problem(error);customize_->setChecked(true);}
            }
            if(manifest_)recommendation_->setText(QStringLiteral("已读取 %1 条命令。").arg(commands_.size()));
            else if(entries.isEmpty()){recommendation_->setText(QStringLiteral("未识别到启动入口，可展开填写目标环境中的启动命令。"));customize_->setChecked(true);}
            else recommendation_->setText(QStringLiteral("入口：%1").arg(entry_->currentText()));
            setBusy(false);if(thenAdd&&!manifestError_)addDefault();
        });
    }
    void populate(){
        list_->clear();for(int i=0;i<commands_.size();++i){auto *item=new QTreeWidgetItem(list_,{commands_[i].name,commands_[i].description});item->setData(0,Qt::UserRole,i);item->setCheckState(0,Qt::Unchecked);item->setToolTip(1,commands_[i].description);}
    }
    void addDefault(){
        if(manifestError_){problem(QStringLiteral("工具配置无效，可展开填写入口并读取命令帮助。"));return;}
        if(manifest_||!commands_.isEmpty()){pending_.clear();for(int i=0;i<commands_.size();++i)pending_<<i;adding_=true;readNext();}
        else readRoot(true);
    }
    void readRoot(bool recommended){
        if(busy_)return;QStringList words;QString error;
        if(!CommandLine::split(entry_->currentText(),&words,&error)){problem(error);customize_->setChecked(true);return;}
        clearCommands();root_={words.takeFirst(),words,entry_->currentData().toString()};manifest_=false;manifestError_=false;tool_=sourceTool_;
        if(root_.arguments==QStringList{"-m","cannlab"}&&!tool_.environment.contains("PYTHONUTF8"))tool_.environment["PYTHONUTF8"]="1";
        AppDatabase::ToolCommand request;request.executable=root_.program;request.arguments=root_.arguments;
        setBusy(true);problem({});recommendation_->setText(QStringLiteral("正在读取命令与参数…"));
        CliDiscovery::help(this,tool_,request,[this,recommended](bool ok,const QByteArray &bytes,const QString &error){
            setBusy(false);if(!ok){problem(error);recommendation_->setText(QStringLiteral("未能读取该入口，请检查后重试，或展开自行添加命令。"));return;}
            const QString text=QString::fromUtf8(bytes);help_->setPlainText(text);commands_=CliDiscovery::commands(text,root_);loaded_.clear();populate();
            if(commands_.size()==1&&commands_[0].arguments==root_.arguments){commands_[0].parameters=CliDiscovery::parameters(text,defaults_,&commands_[0].arguments);loaded_.insert(0);}
            recommendation_->setText(QStringLiteral("已发现 %1 条命令。").arg(commands_.size()));
            status_->setText(QStringLiteral("勾选需要接入的命令。"));
            setBusy(false);if(recommended)addDefault();
        });
    }
    void readCandidate(int index,bool edit,bool optional=false){
        if(busy_||index<0||index>=commands_.size())return;
        if(loaded_.contains(index)){if(edit)editCandidate(index);else readNext();return;}
        setBusy(true);recommendation_->setText(QStringLiteral("正在读取 %1 的参数…").arg(commands_[index].name));
        CliDiscovery::help(this,tool_,commands_[index],[this,index,edit,optional](bool ok,const QByteArray &bytes,const QString &error){
            setBusy(false);if(!ok){if(optional){unavailable_.insert(index);readNext();return;}adding_=false;problem(error);customize_->setChecked(true);return;}
            help_->setPlainText(QString::fromUtf8(bytes));
            commands_[index].parameters=CliDiscovery::parameters(QString::fromUtf8(bytes),defaults_,&commands_[index].arguments);loaded_.insert(index);
            if(edit)editCandidate(index);else readNext();
        });
    }
    void editCandidate(int index){
        if(ToolEditor::editCommand(&commands_[index],tool_,this,commands_)){
            customized_.insert(index);
            list_->topLevelItem(index)->setText(0,commands_[index].name);list_->topLevelItem(index)->setText(1,commands_[index].description);
        }
    }
    void readNext(){
        if(!adding_)return;
        for(int index:pending_)if(!loaded_.contains(index)){readCandidate(index,false);return;}
        if(!manifest_){
            bool needsOptions=false;for(int index:pending_)for(const auto &p:commands_[index].parameters)needsOptions|=CommandParameters::type(p.toObject())=="string";
            if(needsOptions)for(int i=0;i<commands_.size();++i)if(!loaded_.contains(i)&&!unavailable_.contains(i)&&OptionsDiscovery::isListQuery(commands_[i])){readCandidate(i,false,true);return;}
        }
        QList<AppDatabase::ToolCommand> catalog;for(int i=0;i<commands_.size();++i)if(loaded_.contains(i))catalog<<commands_[i];
        selected_.clear();for(int index:pending_){auto command=commands_[index];if(!manifest_&&!customized_.contains(index))OptionsDiscovery::apply(&command,catalog);selected_.append(command);}adding_=false;accept();
    }
    AppDatabase::AutomationTool initial_,tool_,sourceTool_;CliDiscovery::Entry root_;QJsonObject defaults_;
    ManifestParser parser_;DirectoryReader reader_;FileReader fileReader_;
    QWidget *sourceFields_,*details_;QComboBox *sourceType_,*target_,*host_,*entry_=nullptr;QLineEdit *user_,*directory_;
    QPushButton *browse_,*inspect_,*customize_,*read_,*edit_,*add_;QTreeWidget *list_=nullptr;QLabel *status_,*recommendation_;QPlainTextEdit *help_;
    QList<AppDatabase::ToolCommand> commands_,selected_;QList<int> pending_;QSet<int> loaded_,unavailable_,customized_;
    bool busy_=false,adding_=false,inventoryReady_=false,manifest_=false,manifestError_=false;quint64 targetRevision_=0;
};
