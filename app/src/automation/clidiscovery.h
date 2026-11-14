#pragma once
#include "commandline.h"
#include "commandlaunch.h"
#include "commandparameters.h"
#include <QJsonDocument>
#include <QRegularExpression>

namespace CliDiscovery {
struct Entry { QString program; QStringList arguments; QString evidence; };
inline QList<Entry> entries(const QJsonObject &inventory, const QString &directory, bool local = true)
{
    QList<Entry> result; QSet<QString> seen;
    const auto add=[&](const QString &program,const QStringList &args,const QString &evidence){
        const QString identity=program+"\n"+args.join('\n'); if(seen.contains(identity))return;
        seen.insert(identity); result.append({program,args,evidence});
    };
    // Prefer a package that actually belongs to this directory over README
    // examples such as python -m pip/venv, and use its local interpreter.
    QString python = local ? "python" : inventory.value("python").toString("python3");
    const auto files = inventory.value("files").toArray();
    for (const auto &candidate : (local ? QStringList{".venv/Scripts/python.exe", "venv/Scripts/python.exe"} : QStringList{".venv/bin/python", "venv/bin/python"}))
        if (files.contains(candidate)) { python = QDir(directory).filePath(candidate); break; }
    for (const auto &value : files) {
        const auto path = value.toString();
        if (path.endsWith("/__main__.py")) add(python,{"-m",path.section('/',0,0)},path);
    }
    static const QRegularExpression module(QStringLiteral("(?:python(?:3(?:\\.\\d+)?)?(?:\\.exe)?|py(?:\\.exe)?)\\s+-m\\s+([A-Za-z_][A-Za-z0-9_.]*)"));
    for (const auto &document : inventory.value("documents").toArray()) {
        const auto object=document.toObject(); auto matches=module.globalMatch(object.value("text").toString());
        while(matches.hasNext()) {
            const auto name=matches.next().captured(1);
            if (QStringList{"pip","venv","ensurepip","build","pytest","unittest","compileall","http.server"}.contains(name)) continue;
            add(python,{"-m",name},object.value("name").toString());
        }
    }
    for (const auto &file : inventory.value("files").toArray()) {
        const QString path=file.toString(); const QFileInfo info(path); const QString suffix=info.suffix().toLower();
        if(path.endsWith("/__main__.py"))add(python,{"-m",path.section('/',0,0)},path);
        else if(path.contains('/'))continue;
        else if(local && suffix=="exe") add(QDir(directory).filePath(path),{},path);
        else if(suffix=="py" && path!="setup.py")add(python,{path},path);
        else if(suffix=="sh"&&!local)add("sh",{QDir(directory).filePath(path)},path);
        else if(!local&&inventory.value("executables").toArray().contains(path)&&suffix.isEmpty())add(QDir(directory).filePath(path),{},path);
        else if(local&&suffix=="ps1")add("powershell.exe",{"-NoProfile","-File",QDir(directory).filePath(path)},path);
    }
    return result;
}
inline QList<AppDatabase::ToolCommand> commands(const QString &help,const Entry &entry)
{
    QList<AppDatabase::ToolCommand> result; QSet<QString> seen; bool section=false;
    static const QRegularExpression row(QStringLiteral("^\\s{2,}([A-Za-z0-9][A-Za-z0-9_-]*)\\s{2,}(.+)$"));
    for(const auto &line:help.split('\n')) {
        const auto heading=line.trimmed().toLower();
        if(heading=="commands:" || heading=="available commands:" || heading=="positional arguments:" || heading=="subcommands:") {section=true;continue;}
        if(!line.isEmpty() && !line[0].isSpace())section=false;
        if(!section)continue;
        const auto match=row.match(line); if(!match.hasMatch()||seen.contains(match.captured(1)))continue;
        AppDatabase::ToolCommand c; c.name=match.captured(1);c.description=match.captured(2).trimmed();c.executable=entry.program;c.arguments=entry.arguments+QStringList{c.name};
        c.interactive=c.name=="menu"||(entry.arguments==QStringList{"-m","cannlab"}&&c.name=="start");result.append(c);seen.insert(c.name);if(result.size()==50)break;
    }
    if(result.isEmpty()) {
        AppDatabase::ToolCommand c;c.name=QFileInfo(entry.program).completeBaseName();c.executable=entry.program;c.arguments=entry.arguments;
        for(const auto &line:help.split('\n'))if(!line.trimmed().isEmpty()&&!line.trimmed().startsWith("usage:",Qt::CaseInsensitive)){c.description=line.trimmed();break;}
        result.append(c);
    }
    return result;
}
inline QString keyFor(QString flag) { while(flag.startsWith('-'))flag.remove(0,1); return flag.replace('-','_'); }
inline QJsonArray parameters(const QString &help,const QJsonObject &defaults,QStringList *arguments)
{
    QJsonArray result; QSet<QString> seen;
    static const QRegularExpression declaration(QStringLiteral("^\\s{2,}(-[^\\r\\n]+?)(?:\\s{2,}([^\\r\\n]*))?\\r?$"));
    static const QRegularExpression flagExpression(QStringLiteral("--[A-Za-z0-9][A-Za-z0-9_-]*"));
    static const QRegularExpression choicesExpression(QStringLiteral("\\{([^{}]+)\\}"));
    QString usage; bool usageStarted=false;
    for(const auto &line:help.split('\n')) {
        if(line.startsWith("usage:",Qt::CaseInsensitive)){usage=line;usageStarted=true;}
        else if(usageStarted && line.startsWith(' ')&&!line.trimmed().isEmpty())usage+=' '+line.trimmed();
        else if(usageStarted)break;
    }
    // Arguments inside [] are optional. Nested brackets are stripped as a unit.
    QString requiredUsage;int depth=0;for(auto c:usage){if(c=='['){++depth;continue;}if(c==']'){depth=qMax(0,depth-1);continue;}if(!depth)requiredUsage+=c;}
    QHash<QString,QString> exclusive;QSet<QString> requiredGroups;
    static const QRegularExpression groupExpression(QStringLiteral("\\(([^()]*\\|[^()]*)\\)"));
    auto groups=groupExpression.globalMatch(usage);int groupIndex=0;
    while(groups.hasNext()){
        const auto group=groups.next();const QString key=QStringLiteral("choice_%1").arg(++groupIndex);
        auto flags=flagExpression.globalMatch(group.captured(1));while(flags.hasNext())exclusive[flags.next().captured()]=key;
        if(requiredUsage.contains(group.captured()))requiredGroups.insert(key);
    }
    for(const auto &line:help.split('\n')) {
        const auto declarationMatch=declaration.match(line);if(!declarationMatch.hasMatch())continue;
        const auto spec=declarationMatch.captured(1);const auto match=flagExpression.match(spec);if(!match.hasMatch())continue;
        const QString flag=match.captured(),key=keyFor(flag),description=declarationMatch.captured(2).trimmed();
        if(flag=="--help"||flag=="--version"||seen.contains(key)||result.size()>=50)continue;
        seen.insert(key);
        const QString tail=spec.mid(match.capturedEnd()).trimmed();
        const bool toggle=tail.isEmpty();
        const bool required=QRegularExpression(QRegularExpression::escape(flag)+"(?:\\s|=|$)").match(requiredUsage).hasMatch();
        QJsonObject p{{"key",key},{"label",key},{"description",description},{"required",required},{"type","string"},{"flag",flag},{"omit_empty",!required}};
        if(exclusive.contains(flag)){p["required"]=false;p["omit_empty"]=true;p["exclusive_group"]=exclusive.value(flag);p["exclusive_required"]=requiredGroups.contains(exclusive.value(flag));}
        if(toggle){p["type"]="boolean";p["true_value"]=flag;p["false_value"]="";p["default"]=false;arguments->append("{"+key+"}");}
        else {
            const auto choices=choicesExpression.match(tail);
            if(choices.hasMatch()){p["type"]="choice";p["options"]=QJsonArray::fromStringList(choices.captured(1).split(','));}
            if(defaults.contains(key)) {
                p["default"]=defaults.value(key);
                if(defaults.value(key).isDouble())p["type"]="number";
            }
            arguments->append(flag);arguments->append("{"+key+"}");
        }
        result.append(p);
    }
    return result;
}
inline QJsonArray parametersFromArguments(QStringList *arguments)
{
    QJsonArray result;QSet<QString> keys;
    for(int i=0;i<arguments->size();++i){
        const QString flag=arguments->at(i);if(!flag.startsWith("--")||flag.contains('=')||i+1>=arguments->size()||arguments->at(i+1).startsWith("--"))continue;
        const QString key=keyFor(flag),value=arguments->at(i+1);if(keys.contains(key))continue;
        keys.insert(key);bool number=false;const double numeric=value.toDouble(&number);
        QJsonObject p{{"key",key},{"label",key},{"type",number?"number":"string"},{"default",number?QJsonValue(numeric):QJsonValue(value)},{"flag",flag},{"omit_empty",true}};
        result.append(p);(*arguments)[++i]="{"+key+"}";
    }
    return result;
}
inline void help(QObject *context,const AppDatabase::AutomationTool &tool,const AppDatabase::ToolCommand &command,ProcessCapture::CaptureCallback callback)
{
    auto request=command;request.interactive=false;
    auto *process=new QProcess;
    CommandLaunch::configure(process,CommandLaunch::build(tool,request,request.arguments+QStringList{"--help"}));
    ProcessCapture::capture(context,process,std::move(callback),QStringLiteral("命令帮助"),10000);
}
}
