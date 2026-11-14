#pragma once
#include "commandarguments.h"
#include "commandparameters.h"
#include "data/appdatabase.h"

// Suggestions use explicit list/query help and an unambiguous relationship.
// Discovery configures a source; it never executes that query during import.
namespace OptionsDiscovery {
inline QString entity(QString name)
{
    name=name.toLower().replace('-', '_');
    for(const auto &prefix:QStringList{"list_","show_","get_","query_"}) if(name.startsWith(prefix))name.remove(0,prefix.size());
    for(const auto &suffix:QStringList{"_id","_key","_name","_list"}) if(name.endsWith(suffix))name.chop(suffix.size());
    if(name.endsWith("ies"))name=name.left(name.size()-3)+"y";
    else if(name.endsWith('s')&&!name.endsWith("ss"))name.chop(1);
    return name;
}
inline bool isListQuery(const AppDatabase::ToolCommand &command)
{
    if(command.interactive||command.highRisk)return false;
    static const QRegularExpression destructive("(?:^|[-_ ])(?:delete|remove|reset|start|stop|create|update|import|login|logout|submit|run|clear)(?:$|[-_ ])",QRegularExpression::CaseInsensitiveOption);
    if(destructive.match(command.name).hasMatch())return false;
    static const QRegularExpression listing(QStringLiteral("\\b(?:list|lists|listing|enumerate)\\b|列出|(?:查询|显示|获取).{0,16}(?:列表|清单)"),QRegularExpression::CaseInsensitiveOption);
    return listing.match(command.description).hasMatch();
}
inline bool matches(const QJsonObject &parameter,const AppDatabase::ToolCommand &query)
{
    if(entity(parameter.value("key").toString())==entity(query.name))return true;
    // A help sentence may name the list directly, including localized names.
    static const QRegularExpression noun(QStringLiteral("(?:查询|显示|获取|列出)([^，。；、\\s]{1,12}?)(?:列表|清单)"));
    const auto match=noun.match(query.description);
    if(match.hasMatch()&&parameter.value("description").toString().contains(match.captured(1)))return true;
    const auto description=parameter.value("description").toString().toLower();
    return QRegularExpression("\\b"+QRegularExpression::escape(query.name.toLower())+"\\b").match(description).hasMatch();
}
inline QJsonObject sourceFor(const AppDatabase::ToolCommand &query,const QJsonObject &parameter,const QJsonArray &targetParameters)
{
    QSet<QString> keys;for(const auto &p:targetParameters)keys.insert(p.toObject().value("key").toString());
    QHash<QString,QString> values;
    for(const auto &p:query.parameters){
        const auto field=p.toObject();const auto key=field.value("key").toString();
        if(key==parameter.value("key").toString())return {};
        if(field.contains("exclusive_group"))return {};
        if(field.value("required").toBool()){
            if(!keys.contains(key)||field.value("type")=="boolean")return {};
            values[key]="{"+key+"}";
        }else values[key]=QString();
        if(field.value("flag")=="--json"&&field.value("type")=="boolean")values[key]=field.value("true_value").toString("--json");
        if((field.value("flag")=="--format"||field.value("flag")=="--output")&&field.value("options").toArray().contains("json"))values[key]="json";
    }
    const auto args=CommandArguments::expand(query.arguments,values,query.parameters);
    QString valueHint=parameter.value("key").toString();
    static const QRegularExpression identifier("\\b([A-Za-z][A-Za-z0-9_]*(?:_id|_key))\\b");
    const auto match=identifier.match(parameter.value("description").toString());if(match.hasMatch())valueHint=match.captured(1);
    return {{"kind","command"},{"format","auto"},{"executable",query.executable},
        {"arguments",QJsonArray::fromStringList(args)},{"value_hint",valueHint},{"discovered",true}};
}
inline void apply(AppDatabase::ToolCommand *command,const QList<AppDatabase::ToolCommand> &catalog)
{
    for(int i=0;i<command->parameters.size();++i){
        const auto original=command->parameters[i].toObject();
        if(CommandParameters::type(original)!="string"||original.contains("options_source")||original.contains("state_key"))continue;
        QList<QJsonObject> candidates;
        for(const auto &query:catalog){
            if(query.executable==command->executable&&query.arguments==command->arguments)continue;
            if(!isListQuery(query)||!matches(original,query))continue;
            const auto source=sourceFor(query,original,command->parameters);if(!source.isEmpty())candidates.append(source);
        }
        if(candidates.size()!=1)continue;
        auto updated=original;updated["type"]="choice";updated["options_source"]=candidates.first();updated["discovery_base"]=original;
        auto parameters=command->parameters;parameters[i]=updated;QString error;
        if(CommandParameters::validate(command->name,parameters,command->arguments,&error))command->parameters=parameters;
    }
}
inline void mergeUnchangedParameters(AppDatabase::ToolCommand *existing,const AppDatabase::ToolCommand &candidate)
{
    for(int i=0;i<existing->parameters.size();++i)for(const auto &value:candidate.parameters){
        const auto proposed=value.toObject(),old=existing->parameters[i].toObject();
        if(old.value("key")!=proposed.value("key")||!proposed.value("discovery_base").isObject())continue;
        if(old==proposed.value("discovery_base").toObject()){
            auto parameters=existing->parameters;parameters[i]=proposed;QString error;
            if(CommandParameters::validate(existing->name,parameters,existing->arguments,&error))existing->parameters=parameters;
        }
    }
}
}
