#pragma once

#include "commandparameters.h"
#include "commandline.h"
#include "optionssource.h"
#include "optionsdiscovery.h"
#include "clidiscovery.h"
#include "sshconnection.h"
#include "wslprocess.h"
#include "data/appdatabase.h"
#include "ui/theme.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QInputDialog>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <limits>

namespace ToolEditor {
class Dialog : public QDialog {
public:
    QVBoxLayout *body;
    QLabel *error;
    QPushButton *save;
    explicit Dialog(const QString &title, QWidget *parent) : QDialog(parent)
    {
        setWindowTitle(title);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 20, 20, 20); layout->setSpacing(12);
        auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        auto *content = new QWidget(scroll); body = new QVBoxLayout(content); body->setContentsMargins(0, 0, 8, 0); body->setSpacing(12); body->setAlignment(Qt::AlignTop);
        scroll->setWidget(content); content->setAutoFillBackground(false); scroll->viewport()->setAutoFillBackground(false);
        layout->addWidget(scroll, 1);
        error = new QLabel(this); error->setTextFormat(Qt::PlainText); error->setWordWrap(true); error->hide(); layout->addWidget(error);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
        save = buttons->button(QDialogButtonBox::Save); save->setText(QStringLiteral("保存")); save->setObjectName("saveConfiguration");
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
        resize(620, 560);
    }
    void problem(const QString &message) { error->setText(message); error->setVisible(!message.isEmpty()); }
};
inline QLineEdit *line(QFormLayout *form, const QString &label, const QString &value, const char *name)
{
    auto *edit = new QLineEdit(value); edit->setObjectName(QString::fromLatin1(name)); edit->setAccessibleName(label);
    form->addRow(label, edit); return edit;
}
inline QLabel *hint(const QString &text)
{
    auto *label = new QLabel(text); label->setTextFormat(Qt::PlainText); label->setWordWrap(true); UiTheme::setRole(label, "muted"); label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum); return label;
}
inline void advanced(Dialog &dialog, QWidget *fields)
{
    auto *toggle = new QPushButton(QStringLiteral("高级设置"), &dialog); toggle->setCheckable(true); toggle->setAutoDefault(false);
    UiTheme::setRole(toggle, "quiet"); fields->hide(); dialog.body->addWidget(toggle, 0, Qt::AlignLeft); dialog.body->addWidget(fields);
    QObject::connect(toggle, &QPushButton::toggled, fields, &QWidget::setVisible);
}
inline bool validEnvironment(const QJsonObject &environment, QString *error)
{
    static const QRegularExpression name(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    for (auto it = environment.begin(); it != environment.end(); ++it) {
        if (!name.match(it.key()).hasMatch() || !it.value().isString() || it.value().toString().contains(QChar::Null)) {
            if (error) *error = QStringLiteral("环境变量名须为字母、数字或下划线且不能以数字开头；值须为不含 NUL 的文本。"); return false;
        }
    }
    return true;
}
inline bool editTool(AppDatabase::AutomationTool *tool, QWidget *parent)
{
    Dialog dialog(tool->id ? QStringLiteral("工具设置") : QStringLiteral("手动添加工具"), parent);
    dialog.setObjectName("toolEditor");
    auto *form = new QFormLayout; form->setVerticalSpacing(12); dialog.body->addLayout(form);
    auto *name = line(form, QStringLiteral("工具名称"), tool->name, "toolEditorName");
    auto *type = new QComboBox; type->setObjectName("toolEditorTarget");
    type->addItem(QStringLiteral("Windows 本机"), "windows-local"); type->addItem("WSL", "wsl"); type->addItem("SSH", "ssh");
    type->setCurrentIndex(qMax(0, type->findData(tool->targetType))); form->addRow(QStringLiteral("执行环境"), type);
    auto *host = new QComboBox; host->setEditable(true); host->setObjectName("toolEditorHost");
    auto *hostLabel = new QLabel(QStringLiteral("主机 / 发行版")); form->addRow(hostLabel, host);
    auto *user = line(form, QStringLiteral("登录用户"), tool->targetType == "wsl" ? tool->wslUser : tool->sshUser, "toolEditorUser");
    user->setPlaceholderText(QStringLiteral("留空使用环境的默认用户"));
    auto *directory = new QLineEdit(tool->workingDirectory); directory->setObjectName("toolEditorDirectory");
    auto *browse = new QPushButton(QStringLiteral("选择…")); browse->setAutoDefault(false);
    auto *directoryRow = new QHBoxLayout; directoryRow->addWidget(directory, 1); directoryRow->addWidget(browse);
    form->addRow(QStringLiteral("工作目录"), directoryRow);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
        const auto path = QFileDialog::getExistingDirectory(&dialog, QStringLiteral("选择工作目录"), directory->text());
        if (!path.isEmpty()) directory->setText(path);
    });
    const auto changeTarget = [&] {
        const bool local = type->currentData() == "windows-local";
        host->clear(); host->setVisible(!local); hostLabel->setVisible(!local); form->setRowVisible(user, !local); browse->setVisible(local);
        directory->setPlaceholderText(local ? QDir::homePath() : QStringLiteral("绝对 Linux 路径；留空使用默认目录"));
        if (type->currentData() == "ssh") host->addItems(SshConnection::Config::load().aliases);
        if (type->currentData() == "wsl") WslProcess::capture(&dialog, {"--list", "--quiet"}, [&, host](bool ok, const QByteArray &data, const QString &) {
            if (!ok || type->currentData() != "wsl") return;
            const auto previous = host->currentText(); host->addItems(WslProcess::distributions(data));
            if (!previous.isEmpty()) host->setCurrentText(previous);
        });
    };
    changeTarget(); host->setCurrentText(tool->targetType == "wsl" ? tool->wslDistribution : tool->sshHost);
    QObject::connect(type, &QComboBox::currentIndexChanged, &dialog, changeTarget);
    auto *extra = new QWidget; auto *extras = new QFormLayout(extra); extras->setContentsMargins(0, 0, 0, 0); extras->setVerticalSpacing(12);
    auto *description = line(extras, QStringLiteral("说明"), tool->description, "toolEditorDescription");
    auto *state = line(extras, QStringLiteral("状态文件（可选）"), tool->statePath, "toolEditorState");
    state->setPlaceholderText(QStringLiteral("orchestrate-state/v1 JSON 的绝对路径"));
    auto *environment = new QPlainTextEdit; environment->setObjectName("toolEditorEnvironment"); environment->setMaximumHeight(110);
    QStringList entries; for (auto it = tool->environment.begin(); it != tool->environment.end(); ++it) entries << it.key() + "=" + it.value().toString();
    environment->setPlainText(entries.join('\n')); environment->setPlaceholderText("PYTHONUTF8=1\nPYTHONUNBUFFERED=1");
    extras->addRow(QStringLiteral("环境变量"), environment); extras->addRow(QString(), hint(QStringLiteral("每行 NAME=值；继承目标环境，只覆盖填写的变量。配置保存在本机数据库。")));
    advanced(dialog, extra); dialog.body->addStretch();
    QObject::connect(dialog.save, &QPushButton::clicked, &dialog, [&] {
        auto updated = *tool; updated.name = name->text().trimmed(); updated.description = description->text(); updated.targetType = type->currentData().toString();
        updated.sshHost.clear(); updated.sshUser.clear(); updated.wslDistribution.clear(); updated.wslUser.clear();
        updated.workingDirectory = directory->text().trimmed(); updated.statePath = state->text().trimmed(); updated.environment = {};
        if (updated.name.isEmpty()) { dialog.problem(QStringLiteral("请填写工具名称。")); name->setFocus(); return; }
        if (updated.targetType == "windows-local") {
            if (updated.workingDirectory.isEmpty()) updated.workingDirectory = QDir::homePath();
            if (!QDir::isAbsolutePath(updated.workingDirectory)) {
                dialog.problem(QStringLiteral("请选择存在的 Windows 工作目录。")); return;
            }
            if (!updated.statePath.isEmpty() && !QDir::isAbsolutePath(updated.statePath)) { dialog.problem(QStringLiteral("状态文件请填写绝对路径。")); return; }
        } else {
            if (updated.targetType == "wsl") { updated.wslDistribution = host->currentText().trimmed(); updated.wslUser = user->text().trimmed(); }
            else { updated.sshHost = host->currentText().trimmed(); updated.sshUser = user->text().trimmed(); }
            if ((updated.targetType == "wsl" ? !WslProcess::validDistribution(updated.wslDistribution) : !SshConnection::validHost(updated.sshHost))
                || (!user->text().trimmed().isEmpty() && !PosixRegistration::validUser(user->text().trimmed()))) { dialog.problem(QStringLiteral("请填写有效的主机 / 发行版和登录用户。")); return; }
            for (const auto &path : {updated.workingDirectory, updated.statePath})
                if (!path.isEmpty() && (!path.startsWith('/') || !WslProcess::validPath(path))) { dialog.problem(QStringLiteral("工作目录与状态文件须使用以 / 开头的 Linux 绝对路径。")); return; }
        }
        for (const auto &entry : environment->toPlainText().split('\n')) {
            if (entry.trimmed().isEmpty()) continue;
            const int equal = entry.indexOf('=');
            if (equal <= 0 || updated.environment.contains(entry.left(equal).trimmed())) { dialog.problem(QStringLiteral("环境变量须逐行填写 NAME=值，名称不能重复。")); return; }
            updated.environment.insert(entry.left(equal).trimmed(), entry.mid(equal + 1));
        }
        QString error; if (!validEnvironment(updated.environment, &error)) { dialog.problem(error); return; }
        if (updated.externalId.isEmpty()) updated.externalId = "manual-" + QUuid::createUuid().toString(QUuid::Id128);
        updated.customized = true;
        if (updated.targetType == "windows-local") {
            dialog.save->setEnabled(false); dialog.body->parentWidget()->setEnabled(false);
            FileAccess::request(&dialog,"check-directory",updated.workingDirectory,[&,updated](bool ok,const QByteArray &,const QString &error){
                dialog.save->setEnabled(true);dialog.body->parentWidget()->setEnabled(true);
                if(!ok){dialog.problem(error);return;}*tool=updated;dialog.accept();
            });
        } else { *tool=updated;dialog.accept(); }
    });
    return dialog.exec() == QDialog::Accepted;
}

inline bool editSource(QJsonObject *source, const AppDatabase::AutomationTool &tool, QWidget *parent,
    const QList<AppDatabase::ToolCommand> &catalog={},const QHash<QString,QString> &currentValues={})
{
    Dialog dialog(QStringLiteral("下拉选项来源"), parent); dialog.setObjectName("optionsSourceEditor"); dialog.resize(680, 610);
    auto *form = new QFormLayout; form->setVerticalSpacing(12); dialog.body->addLayout(form);
    auto *kind = new QComboBox; kind->addItem(QStringLiteral("读取文件"), "file"); kind->addItem(QStringLiteral("执行查询命令"), "command");
    kind->setCurrentIndex(qMax(0,kind->findData(source->value("kind").toString()))); form->addRow(QStringLiteral("来源"),kind);
    auto *path = line(form,QStringLiteral("文件路径"),source->value("path").toString(),"sourcePath");
    QStringList words; if (!source->value("executable").toString().isEmpty()) words << source->value("executable").toString();
    for (const auto &arg : source->value("arguments").toArray()) words << arg.toString();
    auto *command = line(form,QStringLiteral("查询命令"),CommandLine::join(words),"sourceCommand");
    command->setPlaceholderText(QStringLiteral("例如 python -m tool instances --account {account}"));
    auto *known=new QComboBox;known->setObjectName("sourceKnownCommand");known->addItem(QStringLiteral("自定义查询命令"),-1);
    for(int i=0;i<catalog.size();++i)if(OptionsDiscovery::isListQuery(catalog[i]))known->addItem(catalog[i].name+QStringLiteral(" — ")+catalog[i].description,i);
    form->addRow(QStringLiteral("工具已有查询"),known);form->setRowVisible(known,known->count()>1);
    auto *format = new QComboBox;format->setObjectName("sourceFormat");format->addItem(QStringLiteral("自动识别"),"auto");format->addItem("JSON", "json"); format->addItem(QStringLiteral("每行一个选项"), "lines"); format->addItem(QStringLiteral("按分隔符取列"), "delimited");
    format->setCurrentIndex(qMax(0,format->findData(source->value("format").toString("auto")))); form->addRow(QStringLiteral("输出格式"),format);
    auto *items = line(form,QStringLiteral("列表路径"),source->value("items_path").toString(),"sourceItems"); items->setPlaceholderText(QStringLiteral("例如 data.items；根数组留空"));
    auto *value = line(form,QStringLiteral("值字段"),source->value("value_key").toString("value"),"sourceValue");
    auto *label = line(form,QStringLiteral("显示字段"),source->value("label_key").toString("label"),"sourceLabel");
    auto *delimiter = line(form,QStringLiteral("分隔符"),source->value("delimiter").toString("|"),"sourceDelimiter");
    auto *valueColumn = new QSpinBox; auto *labelColumn = new QSpinBox; auto *columns = new QSpinBox;
    for (auto *spin : {valueColumn,labelColumn}) spin->setRange(1,100);
    columns->setRange(0,100); columns->setSpecialValueText(QStringLiteral("不限"));
    valueColumn->setValue(source->value("value_column").toInt(1)); labelColumn->setValue(source->value("label_column").toInt(1)); columns->setValue(source->value("columns").toInt(0));
    form->addRow(QStringLiteral("值所在列"),valueColumn); form->addRow(QStringLiteral("显示所在列"),labelColumn); form->addRow(QStringLiteral("每行列数"),columns);
    auto *empty = line(form,QStringLiteral("空列表提示（可选）"),source->value("empty_text").toString(),"sourceEmpty");
    auto *pattern = line(form,QStringLiteral("值校验表达式（可选）"),source->value("value_pattern").toString(),"sourcePattern");
    auto *header=new QCheckBox(QStringLiteral("第一行是列名"));header->setChecked(source->value("header").toBool());form->addRow(QString(),header);
    auto *mapping=new QWidget;auto *mappingForm=new QFormLayout(mapping);mappingForm->setContentsMargins(0,0,0,0);
    auto *actualColumn=new QComboBox;actualColumn->setObjectName("sourceActualColumn");auto *displayColumn=new QComboBox;displayColumn->setObjectName("sourceDisplayColumn");
    actualColumn->setPlaceholderText(QStringLiteral("请选择实际参数值所在列…"));
    mappingForm->addRow(hint(QStringLiteral("选择两列，保存后自动读取。")));
    mappingForm->addRow(QStringLiteral("实际参数值所在列"),actualColumn);mappingForm->addRow(QStringLiteral("下拉显示文字所在列"),displayColumn);
    auto *table=new QTableWidget;table->setObjectName("sourceTablePreview");table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);table->setMaximumHeight(180);
    mappingForm->addRow(table);dialog.body->addWidget(mapping);mapping->hide();
    const auto showFields = [&] {
        form->setRowVisible(path,kind->currentData()=="file"); form->setRowVisible(command,kind->currentData()=="command");
        for (auto *field : {items,value,label}) form->setRowVisible(field,format->currentData()=="json");
        form->setRowVisible(delimiter,format->currentData()=="delimited"&&mapping->isHidden());
        for (auto *spin : {valueColumn,labelColumn,columns}) form->setRowVisible(spin,format->currentData()=="delimited"&&mapping->isHidden());
        form->setRowVisible(header,format->currentData()=="delimited");
        form->setRowVisible(empty,(format->currentData()=="lines"||format->currentData()=="delimited")&&mapping->isHidden()); form->setRowVisible(pattern,(format->currentData()=="lines"||format->currentData()=="delimited")&&mapping->isHidden());
    };
    QObject::connect(kind,&QComboBox::currentIndexChanged,&dialog,showFields); QObject::connect(format,&QComboBox::currentIndexChanged,&dialog,showFields); showFields();
    command->setToolTip(QStringLiteral("用 {参数名} 引用上级参数；查询沿用工具环境，请选择只查询列表的命令。"));
    const auto read = [&](QJsonObject *result) {
        *result = {{"kind",kind->currentData().toString()},{"format",format->currentData().toString()}};
        if(source->contains("value_hint"))(*result)["value_hint"]=source->value("value_hint");
        if (kind->currentData()=="file") (*result)["path"] = path->text();
        else {
            QStringList parts; QString error;
            if (!CommandLine::split(command->text(),&parts,&error)) {dialog.problem(error);return false;}
            (*result)["executable"] = parts.takeFirst(); (*result)["arguments"] = QJsonArray::fromStringList(parts);
        }
        if(format->currentData()=="json"){
            (*result)["items_path"]=items->text().trimmed(); (*result)["value_key"]=value->text().trimmed(); (*result)["label_key"]=label->text().trimmed();
        }
        if(format->currentData()=="delimited"){
            (*result)["delimiter"]=delimiter->text(); (*result)["value_column"]=valueColumn->value(); (*result)["label_column"]=labelColumn->value(); (*result)["columns"]=columns->value();(*result)["header"]=header->isChecked();
        }
        if(!mapping->isHidden()&&format->currentData()=="delimited"&&actualColumn->currentIndex()<0){dialog.problem(QStringLiteral("请先选择实际参数值所在列。"));return false;}
        if(format->currentData()=="lines"||format->currentData()=="delimited"){(*result)["empty_text"]=empty->text(); (*result)["value_pattern"]=pattern->text();}
        QString error; if (!CommandParameters::validate("选项",{QJsonObject{{"key","source"},{"type","choice"},{"options_source",*result}}},{},&error,false)) {dialog.problem(error);return false;}
        return true;
    };
    auto *preview = new QPlainTextEdit; preview->setReadOnly(true); preview->setMaximumHeight(130); preview->hide();
    auto *test = new QPushButton(QStringLiteral("测试读取")); test->setAutoDefault(false); dialog.body->addWidget(test,0,Qt::AlignLeft); dialog.body->addWidget(preview);
    test->setObjectName("testOptionsSource");
    QObject::connect(known,&QComboBox::currentIndexChanged,&dialog,[&]{
        const int index=known->currentData().toInt();if(index<0)return;
        const auto &query=catalog[index];kind->setCurrentIndex(kind->findData("command"));
        command->setText(CommandLine::join(QStringList{query.executable}+query.arguments));format->setCurrentIndex(format->findData("auto"));mapping->hide();showFields();
    });
    QObject::connect(actualColumn,&QComboBox::currentIndexChanged,&dialog,[&]{if(actualColumn->currentIndex()>=0)valueColumn->setValue(actualColumn->currentIndex()+1);});
    QObject::connect(displayColumn,&QComboBox::currentIndexChanged,&dialog,[&]{if(displayColumn->currentIndex()>=0)labelColumn->setValue(displayColumn->currentIndex()+1);});
    QObject::connect(test,&QPushButton::clicked,&dialog,[&] {
        QJsonObject config; if (!read(&config)) return;
        QHash<QString,QString> samples;
        for (const auto &dependency : CommandParameters::sourceDependencies(config)) {
            if(!currentValues.value(dependency).isEmpty()){samples[dependency]=currentValues.value(dependency);continue;}
            bool ok=false; const QString sample=QInputDialog::getText(&dialog,QStringLiteral("测试上级参数"),dependency,QLineEdit::Normal,{},&ok);
            if (!ok || sample.isEmpty()) return; samples[dependency]=sample;
        }
        test->setEnabled(false);dialog.save->setEnabled(false);mapping->setEnabled(false);for(int i=0;i<form->count();++i)if(auto *field=form->itemAt(i)->widget())field->setEnabled(false); preview->show(); preview->setPlainText(QStringLiteral("正在读取…"));
        OptionsSource::readRaw(&dialog,tool,config,samples,[&,config,test,preview](bool ok,const QByteArray &bytes,const QString &failure) {
            test->setEnabled(true);dialog.save->setEnabled(true);mapping->setEnabled(true);for(int i=0;i<form->count();++i)if(auto *field=form->itemAt(i)->widget())field->setEnabled(true);QString error=failure;QList<CommandParameters::Option> options;
            if(ok)ok=OptionsSource::parse(bytes,config,&options,&error);
            if(!ok&&config.value("format")=="auto"){
                const auto output=OptionsFormat::table(QString::fromUtf8(bytes));
                if(!output.rows.isEmpty()){
                    table->clear();table->setColumnCount(output.rows.first().size());table->setRowCount(qMin<int>(20,output.rows.size()));actualColumn->clear();displayColumn->clear();
                    for(int c=0;c<table->columnCount();++c){const auto title=QStringLiteral("第 %1 列").arg(c+1);table->setHorizontalHeaderItem(c,new QTableWidgetItem(title));
                        const auto sample=title+QStringLiteral(" · ")+output.rows.first()[c].left(40);actualColumn->addItem(sample);displayColumn->addItem(sample);}
                    for(int r=0;r<table->rowCount();++r)for(int c=0;c<table->columnCount();++c)table->setItem(r,c,new QTableWidgetItem(output.rows[r][c]));
                    actualColumn->setCurrentIndex(-1);displayColumn->setCurrentIndex(0);delimiter->setText(output.delimiter);columns->setValue(table->columnCount());header->setChecked(false);
                    mapping->show();format->setCurrentIndex(format->findData("delimited"));showFields();
                    preview->setPlainText(QStringLiteral("选一次实际参数值和显示文字所在列，再保存。请使用完整输出；若工具截断了 ID，应改用 JSON 或其他完整输出。"));return;
                }
            }
            QStringList rows;
            for (const auto &option : options) rows << option.label+"  →  "+option.value;
            preview->setPlainText(ok ? rows.isEmpty()?QStringLiteral("空列表"):rows.join('\n') : error);
        });
    });
    QObject::connect(dialog.save,&QPushButton::clicked,&dialog,[&]{QJsonObject value; if(read(&value)){*source=value;dialog.accept();}});
    return dialog.exec()==QDialog::Accepted;
}

inline bool editParameter(QJsonObject *definition, const AppDatabase::AutomationTool &tool, QWidget *parent,const QList<AppDatabase::ToolCommand> &catalog={})
{
    Dialog dialog(QStringLiteral("运行参数"), parent); dialog.setObjectName("parameterEditor");
    auto *form = new QFormLayout; form->setVerticalSpacing(12); dialog.body->addLayout(form);
    auto *key = line(form, QStringLiteral("参数名"), definition->value("key").toString(), "parameterKey"); key->setPlaceholderText("input");
    auto *label = line(form, QStringLiteral("显示名称"), definition->value("label").toString(), "parameterLabel");
    auto *type = new QComboBox; type->setObjectName("parameterType");
    const QStringList labels {QStringLiteral("文本"), QStringLiteral("整数"), QStringLiteral("时间"), QStringLiteral("选项"), QStringLiteral("开关"), QStringLiteral("只读状态"), QStringLiteral("文件"), QStringLiteral("目录"), QStringLiteral("数值（可含小数）")};
    for (int i = 0; i < CommandParameters::types.size(); ++i) type->addItem(labels[i], CommandParameters::types[i]);
    type->setCurrentIndex(qMax(0, type->findData(CommandParameters::type(*definition)))); form->addRow(QStringLiteral("类型"), type);
    auto *value = line(form, QStringLiteral("默认值（可选）"), CommandParameters::scalarText(definition->value("default")), "parameterDefault");
    auto *required = new QCheckBox(QStringLiteral("运行前必须填写")); required->setChecked(definition->value("required").toBool()); form->addRow(QString(), required);
    QJsonObject optionSource = definition->value("options_source").toObject();
    auto *sourceMode = new QComboBox; sourceMode->addItem(QStringLiteral("固定选项"),"static"); sourceMode->addItem(QStringLiteral("读取文件或查询命令"),"external"); sourceMode->addItem(QStringLiteral("工具状态文件"),"state");
    sourceMode->setCurrentIndex(!optionSource.isEmpty()?1:definition->contains("options_state_key")?2:0); form->addRow(QStringLiteral("选项来源"),sourceMode);
    auto *configureSource = new QPushButton(QStringLiteral("配置并测试来源…")); configureSource->setAutoDefault(false); form->addRow(QString(),configureSource);
    QObject::connect(configureSource,&QPushButton::clicked,&dialog,[&]{editSource(&optionSource,tool,&dialog,catalog);});
    auto *options = new QTableWidget(0, 2); options->setObjectName("parameterOptions"); options->setHorizontalHeaderLabels({QStringLiteral("参数值"), QStringLiteral("显示文字")});
    options->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); options->setMinimumHeight(130);
    QList<CommandParameters::Option> parsed; QString ignored;
    CommandParameters::readOptions(definition->value("options"), false, &parsed, &ignored);
    for (const auto &entry : parsed) { int row = options->rowCount(); options->insertRow(row); options->setItem(row, 0, new QTableWidgetItem(entry.value)); options->setItem(row, 1, new QTableWidgetItem(entry.label)); }
    auto *choices = new QWidget; auto *choicesLayout = new QVBoxLayout(choices); choicesLayout->setContentsMargins(0,0,0,0); choicesLayout->addWidget(options);
    auto *buttons = new QHBoxLayout; auto *add = new QPushButton(QStringLiteral("添加选项")); auto *remove = new QPushButton(QStringLiteral("删除选项"));
    for (auto *button : {add, remove}) button->setAutoDefault(false);
    buttons->addWidget(add); buttons->addWidget(remove); buttons->addStretch(); choicesLayout->addLayout(buttons); form->addRow(QStringLiteral("固定选项"), choices);
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] { int row = options->rowCount(); options->insertRow(row); options->setItem(row,0,new QTableWidgetItem); options->setItem(row,1,new QTableWidgetItem); options->editItem(options->item(row,0)); });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] { if(options->currentRow() >= 0) options->removeRow(options->currentRow()); });
    auto *minimum = new QDoubleSpinBox; auto *maximum = new QDoubleSpinBox;
    for (auto *spin : {minimum, maximum}) { spin->setDecimals(6); spin->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max()); }
    minimum->setValue(definition->value("min").toDouble(0)); maximum->setValue(definition->value("max").toDouble(1000000));
    form->addRow(QStringLiteral("最小值"), minimum); form->addRow(QStringLiteral("最大值"), maximum);
    const auto changeType = [&] {
        const bool choice=type->currentData()=="choice", numeric=type->currentData()=="integer"||type->currentData()=="number";
        form->setRowVisible(sourceMode,choice); form->setRowVisible(configureSource,choice&&sourceMode->currentData()=="external");
        form->setRowVisible(choices,choice&&sourceMode->currentData()=="static"); form->setRowVisible(minimum,numeric); form->setRowVisible(maximum,numeric);
    };
    QObject::connect(sourceMode,&QComboBox::currentIndexChanged,&dialog,changeType);
    QObject::connect(type, &QComboBox::currentIndexChanged, &dialog, changeType); changeType();
    auto *extra = new QWidget; auto *extras = new QFormLayout(extra); extras->setContentsMargins(0,0,0,0);
    auto *description = line(extras, QStringLiteral("说明"), definition->value("description").toString(), "parameterDescription");
    auto *stateKey = line(extras, QStringLiteral("状态取值路径"), definition->value("state_key").toString(), "parameterStateKey");
    auto *optionsKey = line(extras, QStringLiteral("动态选项路径"), definition->value("options_state_key").toString(), "parameterOptionsKey");
    advanced(dialog, extra);
    QObject::connect(dialog.save, &QPushButton::clicked, &dialog, [&] {
        auto result = *definition;
        result["key"] = key->text().trimmed(); result["label"] = label->text().trimmed().isEmpty() ? key->text().trimmed() : label->text().trimmed();
        result["type"] = type->currentData().toString(); result["required"] = required->isChecked(); result["description"] = description->text();
        if (value->text() != CommandParameters::scalarText(result.value("default"))) result["default"] = value->text();
        result.remove("options"); result.remove("options_state_key"); result.remove("options_source"); result.remove("state_key");
        if (!stateKey->text().trimmed().isEmpty()) result["state_key"] = stateKey->text().trimmed();
        if (type->currentData() == "choice") {
            if (sourceMode->currentData()=="external") result["options_source"] = optionSource;
            else if (sourceMode->currentData()=="state") result["options_state_key"] = optionsKey->text().trimmed();
            else {
                QJsonArray values;
                for (int row = 0; row < options->rowCount(); ++row) {
                    const auto optionValue = options->item(row,0) ? options->item(row,0)->text() : QString();
                    const auto optionLabel = options->item(row,1) ? options->item(row,1)->text() : QString();
                    QJsonObject entry{{"value", optionValue}, {"label", optionLabel.isEmpty() ? optionValue : optionLabel}};
                    for (const auto &original : parsed) if (original.value == optionValue && !original.description.isEmpty()) entry["description"] = original.description;
                    values.append(entry);
                }
                result["options"] = values;
            }
        }
        if (type->currentData() == "integer" || type->currentData() == "number") {
            if (minimum->value() > maximum->value()) { dialog.problem(QStringLiteral("最小值不能大于最大值。")); return; }
            result["min"] = minimum->value(); result["max"] = maximum->value();
        }
        QString error;
        if (!CommandParameters::validate(QStringLiteral("运行参数"), {result}, {}, &error,false)) { dialog.problem(error); return; }
        *definition = result; dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}

inline bool editCommand(AppDatabase::ToolCommand *command, const AppDatabase::AutomationTool &tool, QWidget *parent,const QList<AppDatabase::ToolCommand> &catalog={})
{
    Dialog dialog(command->id ? QStringLiteral("编辑命令") : QStringLiteral("添加命令"), parent); dialog.setObjectName("commandEditor"); dialog.resize(680, 720);
    auto *form = new QFormLayout; form->setVerticalSpacing(12); dialog.body->addLayout(form);
    auto *name = line(form, QStringLiteral("命令名称"), command->name, "commandEditorName");
    auto *paste = new QLineEdit; paste->setObjectName("commandEditorPaste");paste->setPlaceholderText(QStringLiteral("粘贴已有启动命令，读取程序、参数和当前值"));
    auto *parse = new QPushButton(QStringLiteral("读取"));parse->setAutoDefault(false);auto *pasteRow=new QHBoxLayout;pasteRow->addWidget(paste,1);pasteRow->addWidget(parse);form->addRow(QStringLiteral("已有命令"),pasteRow);
    auto *program = new QLineEdit(command->executable); program->setObjectName("commandEditorProgram");
    auto *programRow = new QHBoxLayout; programRow->addWidget(program, 1);
    if (tool.targetType == "windows-local") {
        auto *browse = new QPushButton(QStringLiteral("选择…")); browse->setAutoDefault(false); programRow->addWidget(browse);
        QObject::connect(browse, &QPushButton::clicked, &dialog, [&] { const auto path = QFileDialog::getOpenFileName(&dialog, QStringLiteral("选择程序或解释器"), tool.workingDirectory); if (!path.isEmpty()) program->setText(path); });
    }
    form->addRow(QStringLiteral("程序 / 解释器"), programRow); program->setPlaceholderText("python.exe");
    auto *mode = new QComboBox; mode->setObjectName("commandEditorMode"); mode->addItem(QStringLiteral("应用内执行"), false); mode->addItem(QStringLiteral("在终端打开"), true); mode->setCurrentIndex(command->interactive ? 1 : 0);
    form->addRow(QStringLiteral("执行方式"), mode);
    auto *modeHint = hint(QString()); form->addRow(QString(), modeHint);
    const auto explainMode = [&] { modeHint->setText(mode->currentIndex() ? QStringLiteral("在独立终端输入、查看结果或按 Ctrl+C；Orchestrate 只记录终端已打开。") : QStringLiteral("在下方查看输出与退出码；需要问答或 Ctrl+C 的长任务请选择终端。")); };
    QObject::connect(mode, &QComboBox::currentIndexChanged, &dialog, explainMode); explainMode();
    auto *fillBeforeRun = new QCheckBox(QStringLiteral("运行前填写参数"));fillBeforeRun->setObjectName("fillBeforeRun");fillBeforeRun->setChecked(!command->parameters.isEmpty());dialog.body->addWidget(fillBeforeRun);
    auto *tabs = new QTabWidget; tabs->setMinimumHeight(250); tabs->setMaximumHeight(280); dialog.body->addWidget(tabs);
    auto *argumentsPage = new QWidget; auto *argumentsLayout = new QVBoxLayout(argumentsPage);
    argumentsLayout->addWidget(hint(QStringLiteral("每项是一个独立参数，不加外围引号。用 {参数名} 引用运行参数。")));
    auto *arguments = new QListWidget; arguments->setObjectName("commandEditorArguments"); arguments->setMinimumHeight(120); argumentsLayout->addWidget(arguments, 1);
    const auto addArgument = [&](const QString &value) { auto *item = new QListWidgetItem(value, arguments); item->setFlags(item->flags() | Qt::ItemIsEditable); return item; };
    for (const auto &arg : command->arguments) addArgument(arg);
    auto *argButtons = new QHBoxLayout; auto *argAdd = new QPushButton(QStringLiteral("添加参数项")); auto *argRemove = new QPushButton(QStringLiteral("删除参数项"));
    for (auto *button : {argAdd, argRemove}) button->setAutoDefault(false);
    argButtons->addWidget(argAdd); argButtons->addWidget(argRemove); argButtons->addStretch(); argumentsLayout->addLayout(argButtons);
    QObject::connect(argAdd, &QPushButton::clicked, &dialog, [&] { arguments->editItem(addArgument({})); });
    QObject::connect(argRemove, &QPushButton::clicked, &dialog, [&] { delete arguments->takeItem(arguments->currentRow()); });
    arguments->setDragDropMode(QAbstractItemView::InternalMove); arguments->setDefaultDropAction(Qt::MoveAction);
    tabs->addTab(argumentsPage, QStringLiteral("命令参数"));
    auto *parameterPage = new QWidget; auto *parameterLayout = new QVBoxLayout(parameterPage);
    auto *parameters = new QListWidget; parameters->setObjectName("commandEditorParameters"); parameters->setMinimumHeight(120); parameterLayout->addWidget(parameters, 1);
    QJsonArray definitions = command->parameters;
    QSet<QString> fixedKeys;
    const auto refreshParameters = [&] {
        const QSignalBlocker blocker(parameters);parameters->clear();
        for (const auto &v : definitions) { const auto d = v.toObject();auto *item=new QListWidgetItem(d.value("label").toString(d.value("key").toString()) + "  {" + d.value("key").toString() + "}",parameters);
            item->setData(Qt::UserRole,d.value("key").toString());item->setCheckState(fixedKeys.contains(d.value("key").toString())?Qt::Unchecked:Qt::Checked);
        }
    };
    refreshParameters();
    QObject::connect(parameters,&QListWidget::itemChanged,&dialog,[&](QListWidgetItem *item){if(item->checkState()==Qt::Checked)fixedKeys.remove(item->data(Qt::UserRole).toString());else fixedKeys.insert(item->data(Qt::UserRole).toString());});
    auto *paramButtons = new QHBoxLayout; auto *paramAdd = new QPushButton(QStringLiteral("添加运行参数")); auto *paramEdit = new QPushButton(QStringLiteral("编辑")); auto *paramRemove = new QPushButton(QStringLiteral("删除"));
    for (auto *button : {paramAdd, paramEdit, paramRemove}) { button->setAutoDefault(false); paramButtons->addWidget(button); }
    paramButtons->addStretch(); parameterLayout->addLayout(paramButtons); tabs->addTab(parameterPage, QStringLiteral("运行前填写"));
    parameterLayout->addWidget(hint(QStringLiteral("勾选的参数在运行窗口填写；未勾选的使用默认值，无默认值的可选项不传入。")));
    tabs->setTabEnabled(1,fillBeforeRun->isChecked());
    QObject::connect(fillBeforeRun,&QCheckBox::toggled,&dialog,[tabs](bool enabled){tabs->setTabEnabled(1,enabled);if(enabled)tabs->setCurrentIndex(1);});
    QObject::connect(parse,&QPushButton::clicked,&dialog,[&]{
        QStringList words;QString error;if(!CommandLine::split(paste->text(),&words,&error)){dialog.problem(error);return;}
        program->setText(words.takeFirst());definitions=CliDiscovery::parametersFromArguments(&words);fixedKeys.clear();
        arguments->clear();for(const auto &word:words)addArgument(word);refreshParameters();fillBeforeRun->setChecked(!definitions.isEmpty());dialog.problem({});
    });
    auto *readHelp = new QPushButton(QStringLiteral("从帮助读取参数"));readHelp->setAutoDefault(false);argumentsLayout->addWidget(readHelp,0,Qt::AlignLeft);
    QObject::connect(readHelp,&QPushButton::clicked,&dialog,[&]{
        if(program->text().trimmed().isEmpty()){dialog.problem(QStringLiteral("先填写程序与固定入口参数，例如 -m package start，再读取帮助。"));return;}
        AppDatabase::ToolCommand request;request.executable=program->text().trimmed();
        for(int i=0;i<arguments->count();++i){const auto arg=arguments->item(i)->text();if(arg.contains(QRegularExpression("\\{[A-Za-z0-9_.-]+\\}"))){dialog.problem(QStringLiteral("读取帮助前请只保留启动入口和子命令，不含运行参数占位符。"));return;}request.arguments<<arg;}
        readHelp->setEnabled(false);
        CliDiscovery::help(&dialog,tool,request,[&,request](bool ok,const QByteArray &bytes,const QString &error){
            readHelp->setEnabled(true);if(!ok){dialog.problem(error);return;}
            auto args=request.arguments;definitions=CliDiscovery::parameters(QString::fromUtf8(bytes),{},&args);fixedKeys.clear();arguments->clear();for(const auto &arg:args)addArgument(arg);
            refreshParameters();fillBeforeRun->setChecked(!definitions.isEmpty());dialog.problem(QStringLiteral("已读取参数草稿，请核对类型、默认值和互斥关系。"));
        });
    });
    QObject::connect(paramAdd, &QPushButton::clicked, &dialog, [&] { QJsonObject value; if (editParameter(&value, tool, &dialog,catalog)) { definitions.append(value); refreshParameters(); } });
    const auto editSelected = [&] { int row = parameters->currentRow(); if (row < 0) return; auto value = definitions[row].toObject(); if (editParameter(&value, tool, &dialog,catalog)) { definitions[row] = value; refreshParameters(); parameters->setCurrentRow(row); } };
    QObject::connect(paramEdit, &QPushButton::clicked, &dialog, editSelected);
    QObject::connect(parameters, &QListWidget::itemActivated, &dialog, editSelected);
    QObject::connect(paramRemove, &QPushButton::clicked, &dialog, [&] { int row = parameters->currentRow(); if (row >= 0) { definitions.removeAt(row); refreshParameters(); } });
    auto *extra = new QWidget; auto *extras = new QFormLayout(extra); extras->setContentsMargins(0,0,0,0);
    auto *description = line(extras, QStringLiteral("说明"), command->description, "commandEditorDescription");
    auto *risk = new QCheckBox(QStringLiteral("运行前确认高风险操作")); risk->setChecked(command->highRisk); extras->addRow(QString(), risk); advanced(dialog, extra);
    QObject::connect(dialog.save, &QPushButton::clicked, &dialog, [&] {
        auto result = *command; result.name = name->text().trimmed(); result.executable = program->text().trimmed(); result.interactive = mode->currentData().toBool(); result.description = description->text(); result.highRisk = risk->isChecked(); result.parameters = {}; result.arguments.clear();
        for (int row = 0; row < arguments->count(); ++row) result.arguments << arguments->item(row)->text();
        QHash<QString,QString> fixed;
        for(const auto &value:definitions){
            const auto definition=value.toObject();const auto key=definition.value("key").toString();
            if(fillBeforeRun->isChecked()&&!fixedKeys.contains(key)){result.parameters.append(value);continue;}
            QString text=CommandParameters::scalarText(definition.value("default"));
            if(definition.value("type")=="boolean")text=definition.value((CommandParameters::scalarText(definition.value("default")) == "true")?"true_value":"false_value").toString((CommandParameters::scalarText(definition.value("default")) == "true")?"true":"false");
            if(definition.value("required").toBool()&&text.isEmpty()){dialog.problem(QStringLiteral("固定参数“%1”没有默认值，请填写默认值或保留运行前填写。").arg(key));return;}
            fixed[key]=text;
        }
        result.arguments=CommandArguments::expand(result.arguments,fixed,definitions);
        // Fixed fields can still supply values to a dependent runtime dropdown.
        for (int i = 0; i < result.parameters.size(); ++i) {
            auto definition = result.parameters[i].toObject();
            if (!definition.contains("options_source")) continue;
            auto source = definition.value("options_source").toObject();
            if (source.contains("path")) source["path"] = CommandArguments::expand({source.value("path").toString()}, fixed).value(0);
            if (source.contains("arguments")) {
                QStringList values; for (const auto &value : source.value("arguments").toArray()) values << value.toString();
                source["arguments"] = QJsonArray::fromStringList(CommandArguments::expand(values, fixed));
            }
            definition["options_source"] = source; result.parameters[i] = definition;
        }
        if (result.name.isEmpty() || result.executable.isEmpty()) { dialog.problem(QStringLiteral("请填写命令名称和程序 / 解释器。")); return; }
        if (result.executable.contains(QChar::Null) || result.executable.contains('\n') || result.executable.contains('\r')) { dialog.problem(QStringLiteral("程序路径包含无效字符。")); return; }
        for (const auto &arg : result.arguments) if (arg.contains(QChar::Null)) { dialog.problem(QStringLiteral("参数不能包含 NUL。")); return; }
        if (tool.targetType == "windows-local" && (result.executable.endsWith(".ps1", Qt::CaseInsensitive) || result.executable.endsWith(".py", Qt::CaseInsensitive)
            || result.executable.endsWith(".bat", Qt::CaseInsensitive) || result.executable.endsWith(".cmd", Qt::CaseInsensitive))) {
            dialog.problem(QStringLiteral("脚本须通过解释器运行：例如 python.exe + 脚本路径，或 powershell.exe + -File + 脚本路径；批处理使用 cmd.exe /c。")); return;
        }
        if (tool.targetType != "windows-local" && (!WslProcess::validPath(result.executable) || result.executable.startsWith('~'))) { dialog.problem(QStringLiteral("请使用 Linux 程序名或路径；程序路径不展开 ~。")); return; }
        QString error; if (!CommandParameters::validate(result.name, result.parameters, result.arguments, &error)) { dialog.problem(error); return; }
        for (const auto &value : result.parameters) {
            const auto definition = value.toObject();
            if (tool.statePath.isEmpty() && (definition.contains("state_key") || definition.contains("options_state_key"))) { dialog.problem(QStringLiteral("此参数依赖状态文件，请先在工具设置中配置状态文件。")); return; }
        }
        *command = result; dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
}
