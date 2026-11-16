#pragma once

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>

namespace CommandArguments {
inline QStringList expand(const QStringList &arguments, const QHash<QString, QString> &values, const QJsonArray &parameters = {})
{
    static const QRegularExpression placeholder(QStringLiteral("\\{([A-Za-z0-9_.-]+)\\}"));
    QStringList result;
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        const QString &argument = arguments[i];
        bool omittedPair = false;
        if (i + 1 < arguments.size()) for (const auto &value : parameters) {
            const auto definition = value.toObject(); const auto key = definition.value("key").toString();
            if (definition.value("omit_empty").toBool() && definition.value("flag").toString() == argument
                && arguments[i+1] == "{"+key+"}" && values.contains(key) && values.value(key).isEmpty()) { omittedPair = true; break; }
        }
        if (omittedPair) { ++i; continue; }
        const auto whole = placeholder.match(argument);
        if (whole.hasMatch() && whole.capturedStart() == 0 && whole.capturedLength() == argument.size()
            && values.contains(whole.captured(1)) && values.value(whole.captured(1)).isEmpty()) continue;
        QString expanded;
        qsizetype offset = 0;
        auto matches = placeholder.globalMatch(argument);
        while (matches.hasNext()) {
            const auto match = matches.next();
            expanded += argument.mid(offset, match.capturedStart() - offset);
            expanded += values.value(match.captured(1), match.captured());
            offset = match.capturedEnd();
        }
        expanded += argument.mid(offset);
        result.append(expanded);
    }
    return result;
}
}
