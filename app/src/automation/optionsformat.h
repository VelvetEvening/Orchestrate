#pragma once
#include "commandparameters.h"
#include <QJsonDocument>
#include <QStringDecoder>

namespace OptionsFormat {
struct Table { QString delimiter; QList<QStringList> rows; };
inline Table table(const QString &text)
{
    Table result;QStringList lines;
    for(const auto &line:text.split('\n'))if(!line.trimmed().isEmpty())lines<<line.trimmed();
    if(lines.isEmpty())return result;
    result.delimiter=lines.first().contains('\t')?"\t":lines.first().contains('|')?"|":QString();
    if(result.delimiter.isEmpty())return result;
    int width=0;
    for(auto line:lines){
        if(QRegularExpression("^[|+ :\\-]+$").match(line).hasMatch())continue;
        if(result.delimiter=="|"){if(line.startsWith('|'))line.remove(0,1);if(line.endsWith('|'))line.chop(1);}
        auto cells=line.split(result.delimiter);for(auto &cell:cells)cell=cell.trimmed();
        if(cells.size()<2||(width&&width!=cells.size()))return {};
        width=cells.size();result.rows<<cells;
    }
    return result;
}
inline QString normalized(QString value){return value.trimmed().toLower().replace(' ','_').replace('-','_');}
inline QString valueKey(const QStringList &fields,const QString &hint)
{
    QStringList choices{hint,"value","id","key","uuid","name"};
    for(const auto &choice:choices)if(!choice.isEmpty())for(const auto &field:fields)if(normalized(field)==normalized(choice))return field;
    return {};
}
inline QString labelKey(const QStringList &fields,const QString &fallback)
{
    for(const auto &choice:QStringList{"label","display_name","name","title","名称","名字"})
        for(const auto &field:fields)if(normalized(field)==choice)return field;
    return fallback;
}
inline void arrays(const QJsonValue &value,const QString &path,QList<QPair<QString,QJsonArray>> *found,int depth=0)
{
    if(value.isArray()){found->append({path,value.toArray()});return;}
    if(!value.isObject()||depth>=4)return;
    const auto object=value.toObject();
    for(auto it=object.begin();it!=object.end();++it){
        if(it.key().contains('.'))continue;
        arrays(it.value(),path.isEmpty()?it.key():path+'.'+it.key(),found,depth+1);
    }
}
// Infer only one explicit array or columns with recognizable headers. A
// headerless table needs a user-selected value column, never a guessed ID.
inline bool infer(const QByteArray &bytes,const QJsonObject &source,QJsonObject *result,QString *error)
{
    const auto fail=[&](const QString &message){if(error)*error=message;return false;};
    *result=source;
    QStringDecoder decoder(QStringDecoder::Utf8);QString text=decoder(bytes);
    if(decoder.hasError())return fail(QStringLiteral("选项输出不是有效 UTF-8，请配置工具的输出编码。"));
    if(text.startsWith(QChar(0xfeff)))text.remove(0,1);
    const auto trimmed=text.trimmed();
    if(trimmed.startsWith('[')||trimmed.startsWith('{')){
        QJsonParseError problem;const auto doc=QJsonDocument::fromJson(trimmed.toUtf8(),&problem);
        if(problem.error!=QJsonParseError::NoError)return fail(QStringLiteral("查询返回的 JSON 不完整或格式无效。"));
        QList<QPair<QString,QJsonArray>> found;arrays(doc.isArray()?QJsonValue(doc.array()):QJsonValue(doc.object()),{},&found);
        if(found.size()!=1)return fail(QStringLiteral("输出中有多个列表或没有列表，请在下拉设置中选择列表路径。"));
        (*result)["format"]="json";(*result)["items_path"]=found.first().first;
        const auto rows=found.first().second;
        if(rows.isEmpty()||!rows.first().isObject())return true;
        const auto fields=rows.first().toObject().keys();
        const auto key=valueKey(fields,source.value("value_hint").toString());
        if(key.isEmpty())return fail(QStringLiteral("无法确定哪个字段是实际参数值，请在下拉设置中选择。"));
        (*result)["value_key"]=key;(*result)["label_key"]=labelKey(fields,key);return true;
    }
    const auto columns=table(text);
    if(!columns.rows.isEmpty()){
        const auto header=columns.rows.first();const auto key=valueKey(header,source.value("value_hint").toString());
        if(key.isEmpty())return fail(QStringLiteral("列表没有可识别的列名，请点“设置下拉”选择实际参数值所在列；保存后无需重复设置。"));
        (*result)["format"]="delimited";(*result)["delimiter"]=columns.delimiter;(*result)["columns"]=header.size();
        (*result)["value_column"]=header.indexOf(key)+1;(*result)["label_column"]=header.indexOf(labelKey(header,key))+1;(*result)["header"]=true;
        return true;
    }
    if(trimmed.contains('|')||trimmed.contains('\t'))return fail(QStringLiteral("输出列数不一致，请检查完整列表，未加载部分结果。"));
    static const QRegularExpression empty(QStringLiteral("^(?:no .*(?:found|available|yet)[.!]?|(?:还没有|暂无|没有可用|此账号没有).*)$"),QRegularExpression::CaseInsensitiveOption);
    if(!trimmed.contains('\n')&&empty.match(trimmed).hasMatch())(*result)["empty_text"]=trimmed;
    static const QRegularExpression diagnostic(QStringLiteral("^(?:error|warning|traceback|错误|警告|提示)[:： ]"),QRegularExpression::CaseInsensitiveOption);
    for(const auto &line:text.split('\n'))if(diagnostic.match(line.trimmed()).hasMatch())return fail(QStringLiteral("查询输出包含错误或提示，无法作为选项列表。"));
    (*result)["format"]="lines";return true;
}
}
