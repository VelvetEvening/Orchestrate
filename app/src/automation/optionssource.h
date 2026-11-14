#pragma once
#include "commandarguments.h"
#include "commandparameters.h"
#include "commandlaunch.h"
#include "optionsformat.h"
#include "platform/fileaccess.h"
#include <QJsonDocument>
#include <QPointer>

namespace OptionsSource {
inline bool parse(const QByteArray &bytes, const QJsonObject &source, QList<CommandParameters::Option> *result, QString *error)
{
    const auto fail = [&](const QString &message) { if (error) *error = message; result->clear(); return false; };
    if (bytes.size() > 1024 * 1024) return fail(QStringLiteral("选项数据超过 1 MiB。"));
    if (source.value("format")=="auto") {
        QJsonObject detected;QString problem;
        if(!OptionsFormat::infer(bytes,source,&detected,&problem))return fail(problem);
        return parse(bytes,detected,result,error);
    }
    QJsonArray options;
    const QString format = source.value("format").toString("json");
    if (format == "json") {
        QJsonParseError problem; auto document = QJsonDocument::fromJson(bytes, &problem);
        if (problem.error != QJsonParseError::NoError) return fail(QStringLiteral("选项不是有效 JSON：%1").arg(problem.errorString()));
        QJsonValue items = document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object());
        const QString path = source.value("items_path").toString();
        if (!path.isEmpty()) items = CommandParameters::stateValue(document.object(), path);
        if (!items.isArray()) return fail(QStringLiteral("选项列表路径必须指向数组。"));
        const QString valueKey = source.value("value_key").toString("value"), labelKey = source.value("label_key").toString("label");
        for (const auto &item : items.toArray()) {
            QString value, label;
            if (item.isObject()) {
                value = CommandParameters::scalarText(CommandParameters::stateValue(item.toObject(), valueKey));
                label = labelKey.isEmpty() ? value : CommandParameters::scalarText(CommandParameters::stateValue(item.toObject(), labelKey));
            } else value = label = CommandParameters::scalarText(item);
            options.append(QJsonObject{{"value", value}, {"label", label}});
        }
    } else {
        QString text = QString::fromUtf8(bytes); if (text.startsWith(QChar(0xfeff))) text.remove(0,1);
        const QString emptyText = source.value("empty_text").toString();
        if (!emptyText.isEmpty() && text.trimmed() == emptyText) { result->clear(); return true; }
        const QString pattern = source.value("value_pattern").toString();
        QRegularExpression allowed("\\A(?:" + pattern + ")\\z");
        if (pattern.size() > 512 || (!pattern.isEmpty() && !allowed.isValid())) return fail(QStringLiteral("选项值校验表达式无效。"));
        const int valueColumn = source.value("value_column").toInt(1)-1, labelColumn = source.value("label_column").toInt(1)-1;
        bool first=true;
        for (auto line : text.split('\n')) {
            if (line.trimmed().isEmpty()) continue;
            if (line.size() > 4096 || options.size() >= 500) return fail(QStringLiteral("选项不能超过 500 行，单行不能超过 4096 字符。"));
            QString value = line.trimmed(), label = value;
            if (format == "delimited") {
                const auto delimiter = source.value("delimiter").toString();
                if(delimiter=="|"){
                    if(QRegularExpression("^[|+ :\\-]+$").match(line.trimmed()).hasMatch())continue;
                    line=line.trimmed();if(line.startsWith('|'))line.remove(0,1);if(line.endsWith('|'))line.chop(1);
                }
                const auto parts = line.split(delimiter);
                const int expected = source.value("columns").toInt(0);
                if (delimiter.isEmpty() || valueColumn < 0 || labelColumn < 0 || parts.size() <= qMax(valueColumn,labelColumn)
                    || (expected > 0 && parts.size() != expected)) return fail(QStringLiteral("选项行与列配置不匹配；请检查完整输出和分隔符。"));
                if(first&&source.value("header").toBool()){first=false;continue;}
                value = parts[valueColumn].trimmed(); label = parts[labelColumn].trimmed();
            } else if (format != "lines") return fail(QStringLiteral("不支持的选项格式。"));
            first=false;
            if(value.contains(QChar(0x2026))||value.contains("..."))return fail(QStringLiteral("选项值包含省略标记，请改用完整输出或 JSON。"));
            if (!pattern.isEmpty() && !allowed.match(value).hasMatch()) return fail(QStringLiteral("选项值不符合配置的格式，未加载部分结果。"));
            options.append(QJsonObject{{"value",value},{"label",label}});
        }
    }
    return CommandParameters::readOptions(options, true, result, error);
}
using Callback = std::function<void(bool, const QList<CommandParameters::Option> &, const QString &)>;
using RawCallback = std::function<void(bool,const QByteArray &,const QString &)>;
inline void readRaw(QObject *context, const AppDatabase::AutomationTool &tool, const QJsonObject &source,
                 const QHash<QString, QString> &values, RawCallback callback)
{
    const QPointer<QObject> guard(context);
    const auto complete = [guard, callback](bool ok, const QByteArray &bytes, const QString &failure) {
        if (!guard) return;
        callback(ok,bytes,failure);
    };
    if (source.value("kind") == "file") {
        auto expanded = CommandArguments::expand({source.value("path").toString()}, values);
        if (expanded.size() != 1) { complete(false, {}, QStringLiteral("选项文件路径为空。")); return; }
        QString path = expanded.first();
        if (tool.targetType == "windows-local") {
            if (QFileInfo(path).isRelative()) path = QDir(tool.workingDirectory).filePath(path);
            FileAccess::request(context, "read", path, complete); return;
        }
        AppDatabase::ToolCommand query; query.executable = "cat"; query.arguments = {"--", path};
        auto *process = new QProcess;
        CommandLaunch::configure(process, CommandLaunch::build(tool, query, query.arguments));
        ProcessCapture::capture(context, process, complete, QStringLiteral("选项文件"), 15000);
    } else if (source.value("kind") == "command") {
        AppDatabase::ToolCommand query; query.executable = source.value("executable").toString();
        for (const auto &argument : source.value("arguments").toArray()) query.arguments << argument.toString();
        auto *process = new QProcess;
        CommandLaunch::configure(process, CommandLaunch::build(tool, query, CommandArguments::expand(query.arguments, values)));
        ProcessCapture::capture(context, process, complete, QStringLiteral("选项查询"), 15000);
    } else complete(false, {}, QStringLiteral("未配置选项来源。"));
}
inline void read(QObject *context,const AppDatabase::AutomationTool &tool,const QJsonObject &source,
    const QHash<QString,QString> &values,Callback callback)
{
    readRaw(context,tool,source,values,[source,callback](bool ok,const QByteArray &bytes,const QString &failure){
        QList<CommandParameters::Option> options;QString error=failure;
        if(ok)ok=parse(bytes,source,&options,&error);callback(ok,options,error);
    });
}
}
