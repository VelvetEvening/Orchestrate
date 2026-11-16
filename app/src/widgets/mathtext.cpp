#include "mathtext.h"
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace {
bool escaped(const QString &text, qsizetype pos)
{
    qsizetype count = 0;
    while (pos > 0 && text.at(--pos) == '\\') ++count;
    return count % 2;
}
}

QVector<MathText::Span> MathText::spans(const QString &source)
{
    QVector<Span> result;
    if (!source.contains('$') && !source.contains(QStringLiteral("\\("))
        && !source.contains(QStringLiteral("\\["))) return result;
    QByteArray code(source.size(), 0);
    QChar fence;
    qsizetype fenceSize = 0;
    for (qsizetype start = 0; start < source.size();) {
        auto end = source.indexOf('\n', start);
        if (end < 0) end = source.size();
        auto pos = start;
        while (pos < end && source.at(pos) == ' ') ++pos;
        const auto ch = pos < end ? source.at(pos) : QChar();
        auto runEnd = pos;
        if (ch == '`' || ch == '~') while (runEnd < end && source.at(runEnd) == ch) ++runEnd;
        const bool marker = pos - start <= 3 && runEnd - pos >= 3;
        if (!fence.isNull() || marker || pos - start >= 4 || ch == '\t') {
            std::fill(code.begin() + start, code.begin() + end, (!fence.isNull() || marker) ? 1 : 2);
            if (fence.isNull() && marker) { fence = ch; fenceSize = runEnd - pos; }
            else if (marker && ch == fence && runEnd - pos >= fenceSize
                     && source.mid(runEnd, end - runEnd).trimmed().isEmpty()) fence = QChar();
        } else {
            // Literal inline code runs. Match the complete backtick run.
            for (auto i = start; i < end; ++i) {
                if (source.at(i) != '`' || escaped(source, i)) continue;
                auto j = i;
                while (j < end && source.at(j) == '`') ++j;
                const auto markerText = source.mid(i, j - i);
                auto close = source.indexOf(markerText, j);
                while (close >= 0 && close < end
                       && ((close > j && source.at(close - 1) == '`')
                           || (close + markerText.size() < end && source.at(close + markerText.size()) == '`')))
                    close = source.indexOf(markerText, close + markerText.size());
                if (close >= 0 && close < end) {
                    std::fill(code.begin() + i, code.begin() + close + markerText.size(), 1);
                    i = close + markerText.size() - 1;
                } else i = j - 1;
            }
        }
        start = end + 1;
    }
    for (qsizetype i = 0; i < source.size() && result.size() < 128; ++i) {
        if (code.at(i)) continue;
        const auto ch = source.at(i);
        if (ch != '$' && !(ch == '\\' && i + 1 < source.size()
            && (source.at(i + 1) == '(' || source.at(i + 1) == '['))) continue;
        if (escaped(source, i)) continue;
        QString close;
        qsizetype delimiter = 0;
        bool display = false;
        if (source.mid(i, 2) == QStringLiteral("\\(")) { close = QStringLiteral("\\)"); delimiter = 2; }
        else if (source.mid(i, 2) == QStringLiteral("\\[")) { close = QStringLiteral("\\]"); delimiter = 2; display = true; }
        else if (source.at(i) == '$') {
            display = source.mid(i, 2) == QStringLiteral("$$");
            delimiter = display ? 2 : 1;
            close = display ? QStringLiteral("$$") : QStringLiteral("$");
            if (!display && (i + 1 == source.size() || source.at(i + 1).isSpace()
                || (i > 0 && source.at(i - 1) == '$'))) continue;
        } else continue;
        const auto begin = i + delimiter;
        for (auto j = begin; j < source.size() && j - begin <= 4096; ++j) {
            if (code.at(j) == 1 || (!display && (source.at(j) == '\n' || source.at(j) == '\r'))) break;
            if (source.mid(j, close.size()) != close || escaped(source, j)) continue;
            if (delimiter == 1 && (source.at(j - 1).isSpace()
                || (j + 1 < source.size() && (source.at(j + 1).isDigit() || source.at(j + 1) == '$')))) break;
            if (j > begin && !source.mid(begin, j - begin).trimmed().isEmpty()) {
                result.append({i, j + delimiter - i, source.mid(begin, j - begin), display});
                i = j + delimiter - 1;
            }
            break;
        }
    }
    return result;
}

QString MathText::selectedText(const QTextCursor &cursor)
{
    QString result;
    const auto *doc = cursor.document();
    for (auto block = doc->findBlock(cursor.selectionStart()); block.isValid() && block.position() < cursor.selectionEnd(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            const int start = qMax(cursor.selectionStart(), fragment.position());
            const int end = qMin(cursor.selectionEnd(), fragment.position() + fragment.length());
            if (end <= start) continue;
            const auto raw = fragment.charFormat().property(SourceProperty).toString();
            result += raw.isEmpty() ? fragment.text().mid(start - fragment.position(), end - start) : raw;
        }
        const int newline = block.position() + block.length() - 1;
        if (newline >= cursor.selectionStart() && newline < cursor.selectionEnd() && block.next().isValid()) result += '\n';
    }
    return result;
}

int MathText::expandedPosition(const QTextDocument &document, int position)
{
    int expanded = position;
    for (auto block = document.begin(); block.isValid() && block.position() < position; block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (fragment.position() >= position) break;
            const auto raw = fragment.charFormat().property(SourceProperty).toString();
            if (!raw.isEmpty()) expanded += raw.size() - fragment.length();
        }
    return expanded;
}

int MathText::collapsedPosition(const QTextDocument &document, int position)
{
    int extra = 0;
    for (auto block = document.begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            const auto raw = fragment.charFormat().property(SourceProperty).toString();
            if (raw.isEmpty()) continue;
            const int start = fragment.position() + extra;
            if (position <= start) return qBound(0, position - extra, document.characterCount() - 1);
            if (position < start + raw.size()) return fragment.position();
            extra += raw.size() - fragment.length();
        }
    return qBound(0, position - extra, document.characterCount() - 1);
}
