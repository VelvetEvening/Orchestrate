#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <functional>

namespace CommandParameters {
inline const QStringList types {QStringLiteral("string"), QStringLiteral("integer"),
    QStringLiteral("time"), QStringLiteral("choice"), QStringLiteral("boolean"), QStringLiteral("readonly"),
    QStringLiteral("file"), QStringLiteral("directory"), QStringLiteral("number")};

inline QStringList sourceDependencies(const QJsonObject &source)
{
    QStringList texts;
    if(source.value("kind")=="file")texts<<source.value("path").toString();
    if(source.value("kind")=="command")for (const auto &arg : source.value("arguments").toArray()) texts << arg.toString();
    static const QRegularExpression reference(QStringLiteral("\\{([A-Za-z0-9_.-]+)\\}"));
    QStringList keys;
    for (const auto &text : texts) { auto matches = reference.globalMatch(text); while (matches.hasNext()) {
        const auto key = matches.next().captured(1); if (!keys.contains(key)) keys << key;
    } }
    return keys;
}

inline QString type(const QJsonObject &definition)
{
    return definition.value(QStringLiteral("type")).toString(QStringLiteral("string")).toLower();
}

inline QString scalarText(const QJsonValue &value)
{
    if (value.isString()) return value.toString();
    if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble()) {
        // Qt JSON retains signed 64-bit integers. Converting through double
        // would round IDs above 2^53 or print INT64_MAX in scientific notation.
        const qint64 integer = value.toInteger();
        if (value == QJsonValue(integer)) return QString::number(integer);
        return QString::number(value.toDouble(), 'g', 16);
    }
    return {};
}

inline bool validStatePath(const QJsonValue &value)
{
    if (!value.isString() || value.toString().contains(QChar::Null)) return false;
    static const QRegularExpression path(QStringLiteral("^[^.\\s]+(?:\\.[^.\\s]+)*$"));
    return path.match(value.toString()).hasMatch();
}

inline QJsonValue stateValue(const QJsonObject &state, const QString &path)
{
    QJsonValue current = state;
    for (const auto &part : path.split(QLatin1Char('.'))) {
        if (!current.isObject()) return QJsonValue(QJsonValue::Undefined);
        current = current.toObject().value(part);
    }
    return current;
}

inline QJsonValue initialValue(const QJsonObject &definition, const QJsonObject &state, bool choice = false)
{
    const QString key = definition.value(QStringLiteral("state_key")).toString();
    if (!key.isEmpty()) {
        const auto current = stateValue(state, key);
        if (choice || (!current.isUndefined() && !current.isNull())) return current;
    }
    return definition.value(QStringLiteral("default"));
}

struct Option {
    QString value;
    QString label;
    QString description;
};

// Dynamic lists have a single, explicit source. A malformed list is never
// partially displayed, and duplicate values cannot identify two different rows.
inline bool readOptions(const QJsonValue &source, bool dynamic, QList<Option> *options, QString *error)
{
    options->clear();
    const auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (!source.isArray()) return fail(QStringLiteral("选项字段必须是数组；请检查工具状态。"));
    const auto array = source.toArray();
    if (array.size() > 500) return fail(QStringLiteral("选项不能超过 500 项。"));
    QList<Option> parsed;
    QSet<QString> values;
    for (const auto &item : array) {
        Option option;
        const auto object = item.toObject();
        const auto value = item.isObject() ? object.value(QStringLiteral("value")) : item;
        if (dynamic && (!item.isObject() || !value.isString() || value.toString().trimmed().isEmpty()
            || !object.value(QStringLiteral("label")).isString()
            || object.value(QStringLiteral("label")).toString().trimmed().isEmpty()))
            return fail(QStringLiteral("每个动态选项须提供非空字符串 value 和 label。"));
        if (!(value.isString() || value.isDouble() || value.isBool()))
            return fail(QStringLiteral("选项 value 必须是字符串、数字或布尔值。"));
        if (object.contains(QStringLiteral("label")) && !object.value(QStringLiteral("label")).isString())
            return fail(QStringLiteral("选项 label 必须是字符串。"));
        if (object.contains(QStringLiteral("description")) && !object.value(QStringLiteral("description")).isString())
            return fail(QStringLiteral("选项 description 必须是字符串。"));
        option.value = scalarText(value);
        option.label = object.value(QStringLiteral("label")).toString(option.value);
        option.description = object.value(QStringLiteral("description")).toString();
        if (option.value.contains(QChar::Null)) return fail(QStringLiteral("选项 value 不能包含 NUL。"));
        if (values.contains(option.value)) return fail(QStringLiteral("选项 value“%1”重复。").arg(option.value));
        values.insert(option.value);
        parsed.append(option);
    }
    *options = parsed;
    if (error) error->clear();
    return true;
}

inline bool validate(const QString &commandName, const QJsonArray &parameters,
                     const QStringList &arguments, QString *error, bool checkDependencies = true)
{
    static const QRegularExpression validKey(QStringLiteral("^[A-Za-z0-9_.-]+$"));
    QSet<QString> keys;
    for (const auto &value : parameters) {
        const auto definition = value.toObject();
        const QString key = definition.value(QStringLiteral("key")).toString().trimmed();
        const QString parameterType = type(definition);
        QString problem;
        if (!value.isObject() || key.isEmpty()) problem = QStringLiteral("每个参数都必须是包含 key 的对象");
        else if (keys.contains(key)) problem = QStringLiteral("参数 key“%1”重复").arg(key);
        else if (!validKey.match(key).hasMatch()) problem = QStringLiteral("参数 key 只能包含英文字母、数字、下划线、点和连字符");
        else if (!types.contains(parameterType)) problem = QStringLiteral("不支持参数类型“%1”").arg(parameterType);
        else if (definition.contains(QStringLiteral("required")) && !definition.value(QStringLiteral("required")).isBool())
            problem = QStringLiteral("required 必须是 JSON 布尔值");
        for (const auto &field : {QStringLiteral("state_key"), QStringLiteral("options_state_key")}) {
            if (problem.isEmpty() && definition.contains(field) && !validStatePath(definition.value(field)))
                problem = QStringLiteral("%1 必须是非空的点分对象路径").arg(field);
        }
        const bool external = definition.contains(QStringLiteral("options_source"));
        const bool dynamic = definition.contains(QStringLiteral("options_state_key")) || external;
        if (problem.isEmpty() && dynamic && parameterType != QStringLiteral("choice"))
                problem = QStringLiteral("动态选项仅用于 choice 参数");
        if (problem.isEmpty() && external) {
            const auto source = definition.value("options_source").toObject();
            const QString kind = source.value("kind").toString(), format = source.value("format").toString("json");
            if (!definition.value("options_source").isObject() || (kind != "file" && kind != "command")) problem = QStringLiteral("选项来源须为文件或查询命令");
            else if (definition.contains("options_state_key")) problem = QStringLiteral("选项来源与状态选项不能同时配置");
            else if (kind == "file" && (source.value("path").toString().trimmed().isEmpty() || source.value("path").toString().contains(QChar::Null))) problem = QStringLiteral("请填写有效的选项文件路径");
            else if (kind == "command" && (source.value("executable").toString().trimmed().isEmpty() || source.value("executable").toString().contains(QChar::Null) || !source.value("arguments").isArray())) problem = QStringLiteral("查询命令须提供有效程序和参数数组");
            else if (format != "auto" && format != "json" && format != "lines" && format != "delimited") problem = QStringLiteral("不支持的选项输出格式");
            else if (format == "delimited" && (source.value("delimiter").toString().isEmpty() || source.value("value_column").toInt(1) < 1 || source.value("label_column").toInt(1) < 1)) problem = QStringLiteral("请配置分隔符与从 1 开始的列号");
            for (const auto &arg : source.value("arguments").toArray()) if (!arg.isString() || arg.toString().contains(QChar::Null)) problem = QStringLiteral("查询参数须为不含 NUL 的字符串");
        }
        if (problem.isEmpty() && parameterType == QStringLiteral("choice")) {
            if (dynamic && definition.contains(QStringLiteral("options")))
                problem = QStringLiteral("options 与 options_state_key 不能同时提供");
            else if (!dynamic) {
                QList<Option> options;
                if (readOptions(definition.value(QStringLiteral("options")), false, &options, &problem) && options.isEmpty())
                    problem = QStringLiteral("静态 choice 必须提供非空 options");
            }
        }
        if (problem.isEmpty() && parameterType == QStringLiteral("readonly")) {
            if (!definition.contains(QStringLiteral("state_key"))) problem = QStringLiteral("readonly 必须提供 state_key");
            else if (definition.value(QStringLiteral("required")).toBool()) problem = QStringLiteral("readonly 不能标为必填");
            for (const auto &argument : arguments)
                if (argument.contains(QLatin1Char('{') + key + QLatin1Char('}')))
                    problem = QStringLiteral("只读字段“%1”不能用于命令参数替换").arg(key);
        }
        if (!problem.isEmpty()) {
            if (error) *error = QStringLiteral("命令“%1”的参数“%2”定义无效：%3。").arg(commandName, key, problem);
            return false;
        }
        keys.insert(key);
    }
    if (!checkDependencies) return true;
    QHash<QString, QStringList> dependencies;
    for (const auto &value : parameters) {
        const auto definition = value.toObject();
        const auto key = definition.value("key").toString();
        dependencies[key] = sourceDependencies(definition.value("options_source").toObject());
        for (const auto &dependency : dependencies[key]) if (!keys.contains(dependency)) {
            if (error) *error = QStringLiteral("参数“%1”的选项引用了不存在的参数“%2”。").arg(key, dependency); return false;
        }
    }
    QSet<QString> visited, visiting;
    std::function<bool(const QString &)> visit = [&](const QString &key) {
        if (visiting.contains(key)) return false;
        if (visited.contains(key)) return true;
        visiting.insert(key);
        for (const auto &dependency : dependencies[key]) if (!visit(dependency)) return false;
        visiting.remove(key); visited.insert(key); return true;
    };
    for (const auto &key : keys) if (!visit(key)) { if (error) *error = QStringLiteral("运行参数之间不能形成循环依赖。"); return false; }
    return true;
}
}
