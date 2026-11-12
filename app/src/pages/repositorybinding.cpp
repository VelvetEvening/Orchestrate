#include "repositorybinding.h"
#include "automation/sshconnection.h"
#include "automation/wslprocess.h"
#include "ui/theme.h"
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>

RepositoryBinding::RepositoryBinding(const RepositoryTarget &target, QWidget *parent) : QWidget(parent), reader_(this)
{
    auto *form = new QFormLayout(this); form->setContentsMargins(0,0,0,0); form->setVerticalSpacing(12);
    type_ = new QComboBox(this); type_->setObjectName(QStringLiteral("projectTargetType"));
    type_->addItem(QStringLiteral("本地 Windows"),QStringLiteral("local")); type_->addItem(QStringLiteral("WSL"),QStringLiteral("wsl")); type_->addItem(QStringLiteral("SSH 服务器"),QStringLiteral("ssh"));
    form->addRow(QStringLiteral("所在环境"),type_);
    location_ = new QComboBox(this); location_->setObjectName(QStringLiteral("projectTargetName")); location_->setEditable(true); location_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    refresh_ = new QPushButton(QStringLiteral("刷新列表"),this); auto *locationRow=new QHBoxLayout; locationRow->addWidget(location_,1); locationRow->addWidget(refresh_);
    locationLabel_=new QLabel(QStringLiteral("主机 / 发行版"),this); form->addRow(locationLabel_,locationRow);
    user_ = new QComboBox(this); user_->setObjectName(QStringLiteral("projectTargetUser")); user_->setEditable(true); user_->addItem(QString()); user_->lineEdit()->setPlaceholderText(QStringLiteral("使用环境的默认用户"));
    userLabel_=new QLabel(QStringLiteral("登录用户"),this); form->addRow(userLabel_,user_);
    directory_=new QLineEdit(target.directory,this); directory_->setObjectName(QStringLiteral("projectDirectory"));
    browse_=new QPushButton(QStringLiteral("选择"),this); auto *directoryRow=new QHBoxLayout; directoryRow->addWidget(directory_,1); directoryRow->addWidget(browse_); form->addRow(QStringLiteral("项目目录（可选）"),directoryRow);
    validate_=new QPushButton(QStringLiteral("验证目录与 Git"),this); UiTheme::setRole(validate_,"quiet");
    status_=new QLabel(this); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); UiTheme::setRole(status_,"muted");
    form->addRow(QString(),validate_); form->addRow(QString(),status_);
    type_->setCurrentIndex(qMax(0,type_->findData(target.type)));
    location_->setCurrentText(target.location); user_->setCurrentText(target.user);
    connect(type_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{ location_->clear(); user_->setCurrentText(QString()); changeEnvironment(); });
    connect(refresh_,&QPushButton::clicked,this,&RepositoryBinding::changeEnvironment);
    discoveryTimer_=new QTimer(this); discoveryTimer_->setSingleShot(true); discoveryTimer_->setInterval(250);
    connect(discoveryTimer_,&QTimer::timeout,this,&RepositoryBinding::discoverUsers);
    connect(location_,&QComboBox::currentTextChanged,this,[this]{ cancelDiscovery(); discoveryTimer_->start(); });
    connect(browse_,&QPushButton::clicked,this,[this]{ const auto selected=QFileDialog::getExistingDirectory(this,QStringLiteral("选择项目目录"),directory_->text()); if(!selected.isEmpty())directory_->setText(selected); });
    connect(validate_,&QPushButton::clicked,this,[this]{
        if(directory_->text().trimmed().isEmpty()){status_->setText(QStringLiteral("先填写项目目录。"));return;}
        const auto target=value();
        if(target.type==QStringLiteral("local")) {
            setEnabled(false); reader_.probe(target,[this](Git::Result result){setEnabled(true);status_->setText(result.ok?QStringLiteral("已确认 Git 仓库。") : QStringLiteral("尚未确认 Git 仓库；仍可保存为普通项目目录。\n")+result.error);});
        } else resolve([](bool,RepositoryTarget,QString){});
    });
    QTimer::singleShot(0,this,&RepositoryBinding::changeEnvironment);
}
RepositoryBinding::~RepositoryBinding(){cancelDiscovery();}
void RepositoryBinding::cancelDiscovery(){delete discovery_;discovery_=nullptr;}
RepositoryTarget RepositoryBinding::value() const {
    const bool local=type_->currentData()==QStringLiteral("local");
    return {type_->currentData().toString(),local?QString():location_->currentText().trimmed(),local?QString():user_->currentText().trimmed(),directory_->text().trimmed()};
}
void RepositoryBinding::changeEnvironment()
{
    discoveryTimer_->stop(); cancelDiscovery();
    const auto type=type_->currentData().toString(); const bool local=type==QStringLiteral("local");
    for(auto *widget:QList<QWidget*>{location_,locationLabel_,user_,userLabel_,refresh_})widget->setVisible(!local);
    browse_->setVisible(local); directory_->setPlaceholderText(local?QStringLiteral("选择或粘贴 Windows 目录"):QStringLiteral("绝对 Linux 路径或 ~/项目目录"));
    status_->clear(); if(local)return;
    const auto previous=location_->currentText();
    if(type==QStringLiteral("ssh")) {
        const auto config=SshConnection::Config::load();
        {const QSignalBlocker block(location_);location_->clear();location_->addItems(config.aliases);if(!previous.isEmpty())location_->setCurrentText(previous);}
        status_->setText(config.warnings.isEmpty()?QStringLiteral("沿用 SSH 配置和已有认证。连接后确认实际登录用户。"):config.warnings.join('\n'));
        discoverUsers(); return;
    }
    discovery_=new QObject(this); status_->setText(QStringLiteral("正在读取 WSL 发行版…"));
    WslProcess::capture(discovery_,{QStringLiteral("--list"),QStringLiteral("--quiet")},[this,previous](bool ok,const QByteArray &bytes,const QString &error){
        if(!ok){status_->setText(error);return;}
        {const QSignalBlocker block(location_);location_->clear();location_->addItems(WslProcess::distributions(bytes));if(!previous.isEmpty())location_->setCurrentText(previous);}
        // Defer so the discovery request can finish before its owner is replaced.
        QTimer::singleShot(0,this,&RepositoryBinding::discoverUsers);
    });
}
void RepositoryBinding::discoverUsers()
{
    cancelDiscovery();
    const auto type=type_->currentData().toString();
    if(type==QStringLiteral("local"))return;
    const QString previous=user_->currentText();
    {const QSignalBlocker block(user_);user_->clear();user_->addItem(QString());user_->setCurrentText(previous);}
    if(type==QStringLiteral("ssh")) {
        const auto hint=SshConnection::Config::load().userHint(location_->currentText().trimmed());
        if(!hint.isEmpty() && user_->findText(hint)<0)user_->addItem(hint);
        user_->lineEdit()->setPlaceholderText(hint.isEmpty()?QStringLiteral("使用 SSH 配置的用户"):QStringLiteral("默认：")+hint);
        return;
    }
    const auto distro=location_->currentText().trimmed(); if(!WslProcess::validDistribution(distro))return;
    discovery_=new QObject(this);
    status_->setText(QStringLiteral("正在识别所选 WSL 的默认用户…"));
    WslProcess::capture(discovery_,WslProcess::arguments(distro,{},QStringLiteral("sh"),{QStringLiteral("-c"),QStringLiteral("id -un && cat /etc/passwd")}),
        [this](bool ok,const QByteArray &bytes,const QString &error){
            if(!ok){status_->setText(error);return;}
            const auto names=WslProcess::users(bytes);
            const QString selected=user_->currentText();
            const QSignalBlocker block(user_);user_->clear();user_->addItem(QString());user_->addItems(names);user_->setCurrentText(selected);
            user_->lineEdit()->setPlaceholderText(names.isEmpty()?QStringLiteral("使用默认用户"):QStringLiteral("默认：")+names.first());
            status_->setText(names.isEmpty()?QStringLiteral("可留空沿用默认用户。"):QStringLiteral("默认用户：%1。可直接粘贴目录。").arg(names.first()));
        });
}
void RepositoryBinding::resolve(std::function<void(bool,RepositoryTarget,QString)> callback)
{
    if(resolving_){callback(false,value(),QStringLiteral("正在验证目录，请稍后保存。"));return;}
    auto target=value();
    if(target.directory.isEmpty()){callback(true,target,{});return;}
    QString error;
    if(!Git::Reader::validTarget(target,&error)){status_->setText(error);callback(false,target,error);return;}
    if(target.type==QStringLiteral("local")){callback(true,target,{});return;}
    discoveryTimer_->stop(); cancelDiscovery(); resolving_=true; setEnabled(false);status_->setText(QStringLiteral("正在验证目录和实际登录用户…"));
    reader_.probe(target,[this,target,callback](Git::Result result) mutable {
        resolving_=false;setEnabled(true);
        const auto fields=result.output.split('\0');
        if(!result.ok || fields.size()!=3 || !PosixRegistration::validUser(QString::fromUtf8(fields.value(0))) || !fields.value(1).startsWith('/')){
            const auto error=result.ok?QStringLiteral("无法识别远程目录和用户。"):result.error;status_->setText(error);callback(false,target,error);return;
        }
        target.user=QString::fromUtf8(fields[0]);target.directory=QString::fromUtf8(fields[1]);
        user_->setCurrentText(target.user);directory_->setText(target.directory);
        status_->setText(fields[2]=="git"?QStringLiteral("已确认 Git 仓库 · 用户：")+target.user:
            fields[2]=="missing-git"?QStringLiteral("目录可用，尚未安装 Git；可先保存，安装后读取历史。"):QStringLiteral("目录可用，但不是 Git 仓库；可保存为普通项目。"));
        callback(true,target,{});
    });
}
