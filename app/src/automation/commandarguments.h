#pragma once

#include <QHash>
#include <QRegularExpression>
#include <QStringList>

namespace CommandArguments {
inline QStringList expand(const QStringList &arguments, const QHash<QString, QString> &values)
{
    static const QRegularExpression placeholder(QStringLiteral("\\{([A-Za-z0-9_.-]+)\\}"));
    QStringList result;
    for (const QString &argument : arguments) {
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
