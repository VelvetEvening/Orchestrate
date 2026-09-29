#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace ToolState {
constexpr qsizetype maxBytes = 1024 * 1024;
constexpr qsizetype maxItems = 500;

inline QDateTime timestamp(const QString &value)
{
    static const QRegularExpression zone(QStringLiteral("(?:Z|[+-][0-9]{2}:[0-9]{2})$"));
    if (!value.contains(QLatin1Char('T')) || !zone.match(value).hasMatch()) return {};
    return QDateTime::fromString(value, Qt::ISODateWithMs);
}

inline bool parse(const QByteArray &bytes, const QString &toolId, QJsonObject *state, QString *error)
{
    if (state) *state = {};
    if (error) error->clear();
    const auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (bytes.size() > maxBytes) return fail(QStringLiteral("状态文件超过 1 MiB 上限。"));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(QStringLiteral("状态文件必须是有效的 JSON 对象：%1").arg(parseError.errorString()));
    const auto object = document.object();
    if (object.value(QStringLiteral("schema")).toString() != QStringLiteral("orchestrate-state/v1"))
        return fail(QStringLiteral("状态 schema 必须是 orchestrate-state/v1。"));
    if (toolId.isEmpty() || object.value(QStringLiteral("tool_id")).toString() != toolId)
        return fail(QStringLiteral("状态 tool_id 与当前工具不匹配（应为 %1）。").arg(toolId));
    if (!object.value(QStringLiteral("result")).isString()
        || object.value(QStringLiteral("result")).toString().trimmed().isEmpty())
        return fail(QStringLiteral("状态 result 必须是非空字符串。"));
    if (!object.value(QStringLiteral("summary")).isString())
        return fail(QStringLiteral("状态 summary 必须是字符串。"));
    const auto updated = timestamp(object.value(QStringLiteral("updated_at")).toString());
    if (!updated.isValid()) return fail(QStringLiteral("状态 updated_at 必须是带时区的 ISO 8601 时间。"));
    const QString date = object.value(QStringLiteral("current_date")).toString();
    if (date.size() != 10 || QDate::fromString(date, Qt::ISODate).toString(Qt::ISODate) != date)
        return fail(QStringLiteral("状态 current_date 必须是有效的 YYYY-MM-DD 日期。"));
    if (object.contains(QStringLiteral("expires_at"))) {
        const auto expires = timestamp(object.value(QStringLiteral("expires_at")).toString());
        if (!expires.isValid() || expires < updated)
            return fail(QStringLiteral("状态 expires_at 必须是带时区且不早于 updated_at 的时间。"));
    }
    if (object.contains(QStringLiteral("items"))) {
        if (!object.value(QStringLiteral("items")).isArray()) return fail(QStringLiteral("状态 items 必须是数组。"));
        const auto items = object.value(QStringLiteral("items")).toArray();
        if (items.size() > maxItems) return fail(QStringLiteral("状态 items 不能超过 500 项。"));
        for (const auto &item : items) {
            if (!item.isObject()) return fail(QStringLiteral("状态明细必须是对象。"));
            for (const auto &key : {QStringLiteral("name"), QStringLiteral("result"), QStringLiteral("summary")})
                if (!item.toObject().value(key).isString())
                    return fail(QStringLiteral("状态明细 %1 必须是字符串。").arg(key));
        }
    }
    if (state) *state = object;
    return true;
}

inline bool expired(const QJsonObject &state, const QDateTime &now = QDateTime::currentDateTime())
{
    const auto deadline = timestamp(state.value(QStringLiteral("expires_at")).toString());
    return deadline.isValid() && now >= deadline;
}

inline QString displayResult(const QJsonObject &state)
{
    return expired(state) ? QStringLiteral("expired") : state.value(QStringLiteral("result")).toString();
}
}
