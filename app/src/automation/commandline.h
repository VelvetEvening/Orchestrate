#pragma once
#include <QStringList>

namespace CommandLine {
// A command line is only an input shortcut for program + argv. No shell
// expansion, pipelines, redirection, or command substitution is performed.
inline bool split(const QString &text, QStringList *words, QString *error)
{
    words->clear(); QString word; QChar quote; bool started = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c == QChar::Null || c == '\n' || c == '\r') { if (error) *error = QStringLiteral("请粘贴一条单行命令。"); return false; }
        if (c == '\\' && quote == '"') {
            qsizetype end = i; while (end < text.size() && text[end] == '\\') ++end;
            const auto count = end - i;
            if (end < text.size() && text[end] == '"') {
                word += QString(count / 2, '\\');
                if (count % 2) word += '"'; else quote = {};
                i = end; started = true; continue;
            }
            word += QString(count, '\\'); i = end - 1; started = true; continue;
        }
        if (!quote.isNull()) { if (c == quote) quote = {}; else word += c; started = true; }
        else if (c == '"' || c == '\'') { quote = c; started = true; }
        else if (c.isSpace()) { if (started) { words->append(word); word.clear(); started = false; } }
        else { word += c; started = true; }
    }
    if (!quote.isNull()) { if (error) *error = QStringLiteral("命令中的引号未闭合。"); return false; }
    if (started) words->append(word);
    if (words->isEmpty() || words->first().isEmpty()) { if (error) *error = QStringLiteral("请填写程序与参数。"); return false; }
    for (const auto &word : *words) if (word == "|" || word == "||" || word == "&&" || word == ">" || word == "<" || word == ";") {
        if (error) *error = QStringLiteral("此处读取程序与参数，不解析管道或重定向；需要时请显式配置 shell 或脚本。"); return false;
    }
    return true;
}
inline QString join(const QStringList &words)
{
    QStringList quoted;
    for (const auto &word : words) {
        if (!word.isEmpty() && !word.contains(' ') && !word.contains('\t') && !word.contains('"') && !word.contains('\'')) {
            quoted << word; continue;
        }
        QString escaped = "\""; int slashes = 0;
        for (auto c : word) {
            if (c == '\\') { ++slashes; continue; }
            escaped += QString(c == '"' ? slashes * 2 + 1 : slashes, '\\');
            escaped += c; slashes = 0;
        }
        escaped += QString(slashes * 2, '\\') + '"'; quoted << escaped;
    }
    return quoted.join(' ');
}
}
