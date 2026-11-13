#include "markdowntext.h"
#include "textlayoutguard.h"
#include "ui/theme.h"
#include <QBuffer>
#include <QFontDatabase>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QTextTable>

namespace {
using namespace MarkdownText;
bool escaped(const QString &s, int i)
{
    int count = 0;
    while (i > 0 && s.at(--i) == '\\') ++count;
    return count % 2;
}

QVector<Cell> cells(const QString &line, int offset)
{
    QVector<Cell> result;
    int first = 0, last = line.size();
    while (first < last && line.at(first).isSpace()) ++first;
    while (last > first && line.at(last - 1).isSpace()) --last;
    if (first < last && line.at(first) == '|') ++first;
    if (last > first && line.at(last - 1) == '|' && !escaped(line, last - 1)) --last;
    int start = first;
    for (int i = first; i <= last; ++i) {
        // GFM requires pipes inside code spans and formulas to be escaped too.
        if (i != last && (line.at(i) != '|' || escaped(line, i))) continue;
        int a = start, b = i;
        while (a < b && line.at(a).isSpace()) ++a;
        while (b > a && line.at(b - 1).isSpace()) --b;
        result.append({offset + a, b - a});
        start = i + 1;
    }
    return result;
}

class Writer {
public:
    QTextDocument document;
    QTextCursor cursor{&document};
    QVector<int> positions;
    const Block &block;
    const MathText::Images &images;
    qreal dpr;
    Writer(const Block &b, const QFont &font, const MathText::Images &im, qreal ratio)
        : block(b), images(im), dpr(ratio)
    {
        document.setDefaultFont(font);
        document.setDocumentMargin(0);
        document.setUndoRedoEnabled(false);
        positions.append(0);
    }
    void point(int p, int source)
    {
        while (positions.size() <= p) positions.append(source);
        positions[p] = source;
    }
    void text(int start, int length, const QTextCharFormat &format)
    {
        const int pos = cursor.position();
        cursor.insertText(block.text.mid(start, length), format);
        for (int i = 0; i <= length; ++i) point(pos + i, start + i);
    }
    void literal(const QString &value, int start, int end, const QTextCharFormat &format)
    {
        const int pos = cursor.position();
        cursor.insertText(value, format);
        for (int i = 0; i < value.size(); ++i) point(pos + i, start);
        point(cursor.position(), end);
    }
    void formula(int start, int length, const QTextCharFormat &style)
    {
        const auto raw = block.text.mid(start, length);
        if (!images.contains(raw)) { text(start, length, style); return; }
        const auto original = images.value(raw);
        const qreal base = document.defaultFont().pointSizeF();
        const qreal scale = style.fontPointSize() > 0 && base > 0 ? style.fontPointSize() / base : 1;
        const QSizeF size(original.width() / 3.0 * scale, original.height() / 3.0 * scale);
        const QImage image = original.scaled(qMax(1, qRound(size.width() * dpr)),
            qMax(1, qRound(size.height() * dpr)), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        QTextImageFormat format;
        format.setName(QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64()));
        format.setWidth(size.width()); format.setHeight(size.height());
        format.setVerticalAlignment(QTextCharFormat::AlignMiddle);
        format.setProperty(MathText::SourceProperty, raw);
        format.setProperty(QTextFormat::ImageAltText, raw);
        format.setToolTip(raw);
        const int pos = cursor.position();
        cursor.insertImage(format);
        point(pos, start); point(pos + 1, start + length);
    }
    void inlines(int start, int length, QTextCharFormat style, int depth = 0)
    {
        if (depth > 8) { text(start, length, style); return; }
        const int end = start + length;
        const QString part = block.text.mid(start, length);
        QHash<int, int> formulas;
        for (const auto &span : MathText::spans(part)) formulas.insert(start + span.start, span.length);
        int plain = start;
        for (int i = start; i < end;) {
            if (formulas.contains(i)) {
                text(plain, i - plain, style);
                const int size = formulas.value(i);
                formula(i, size, style); i += size; plain = i; continue;
            }
            const QChar ch = block.text.at(i);
            if (ch == '\\' && i + 1 < end && QStringLiteral("\\`*_{}[]()#+-.!|>~$").contains(block.text.at(i + 1))) {
                text(plain, i - plain, style);
                literal(block.text.mid(i + 1, 1), i, i + 2, style);
                i += 2; plain = i; continue;
            }
            if (ch == '`' || ch == '*' || ch == '_' || ch == '~') {
                int n = 1;
                while (i + n < end && block.text.at(i + n) == ch) ++n;
                if (ch != '`') n = qMin(n, ch == '~' ? 2 : 3);
                const QString marker(n, ch);
                int close = block.text.indexOf(marker, i + n);
                while (close >= 0 && close < end && escaped(block.text, close)) close = block.text.indexOf(marker, close + n);
                const bool wordUnderscore = ch == '_' && i > start && block.text.at(i - 1).isLetterOrNumber();
                if (!wordUnderscore && (ch != '~' || n == 2) && close > i + n && close + n <= end) {
                    text(plain, i - plain, style);
                    QTextCharFormat inner = style;
                    if (ch == '`') { inner.setFontFamilies({QFontDatabase::systemFont(QFontDatabase::FixedFont).family()}); inner.setBackground(QColor(UiTheme::Sidebar)); }
                    else if (ch == '~') inner.setFontStrikeOut(true);
                    else { if (n >= 2) inner.setFontWeight(QFont::Bold); if (n % 2) inner.setFontItalic(true); }
                    if (ch == '`') text(i + n, close - i - n, inner);
                    else inlines(i + n, close - i - n, inner, depth + 1);
                    // Boundaries include the hidden delimiters when copied as source.
                    i = close + n; plain = i; continue;
                }
            }
            // Links remain inert text; external images/HTML are never loaded.
            if (ch == '[' && (i == start || block.text.at(i - 1) != '!')) {
                const int labelEnd = block.text.indexOf(QStringLiteral("]("), i + 1);
                const int urlEnd = labelEnd >= 0 ? block.text.indexOf(')', labelEnd + 2) : -1;
                if (labelEnd > i && urlEnd > labelEnd && urlEnd < end) {
                    text(plain, i - plain, style);
                    auto link = style; link.setForeground(QColor(UiTheme::Accent)); link.setFontUnderline(true);
                    link.setToolTip(block.text.mid(labelEnd + 2, urlEnd - labelEnd - 2));
                    inlines(i + 1, labelEnd - i - 1, link, depth + 1);
                    i = urlEnd + 1; plain = i; continue;
                }
            }
            ++i;
        }
        text(plain, end - plain, style);
    }
};
}

QVector<MarkdownText::Block> MarkdownText::parse(const QString &source)
{
    const auto lines = source.split('\n', Qt::KeepEmptyParts);
    QVector<int> offsets;
    int offset = 0;
    for (const auto &line : lines) { offsets.append(offset); offset += line.size() + 1; }
    QVector<Block> blocks;
    static const QRegularExpression heading(QStringLiteral("^(#{1,6}) +(?=\\S)(.*)$"));
    static const QRegularExpression fence(QStringLiteral("^ {0,3}(`{3,}|~{3,})(.*)$"));
    static const QRegularExpression list(QStringLiteral("^( *)(?:[-+*]|[0-9]+[.)]) +"));
    static const QRegularExpression rule(QStringLiteral("^ {0,3}(?:(?:\\* *){3,}|(?:- *){3,}|(?:_ *){3,})$"));
    static const QRegularExpression separator(QStringLiteral("^:?-{3,}:?$"));
    QHash<int, MathText::Span> display;
    for (const auto &span : MathText::spans(source)) if (span.display) display.insert(span.start, span);
    for (int i = 0; i < lines.size(); ++i) {
        Block b; b.start = offsets[i]; b.text = lines[i];
        const auto f = fence.match(lines[i]);
        if (f.hasMatch()) {
            b.kind = Kind::Code; b.prefix = lines[i].size() + 1;
            const QChar marker = f.captured(1).at(0); const int count = f.captured(1).size();
            int j = i + 1;
            for (; j < lines.size(); ++j) {
                const auto close = fence.match(lines[j]);
                if (close.hasMatch() && close.captured(1).at(0) == marker
                    && close.captured(1).size() >= count && close.captured(2).trimmed().isEmpty()) break;
            }
            b.level = j < lines.size() ? lines[j].size() + 1 : 0;
            i = qMin(j, int(lines.size()) - 1);
            b.text = source.mid(b.start, offsets[i] + lines[i].size() - b.start);
        } else if (display.contains(b.start) && source.mid(b.start + display.value(b.start).length, lines[i].size()).section('\n', 0, 0).trimmed().isEmpty()) {
            const auto span = display.value(b.start);
            b.kind = Kind::Formula;
            while (i + 1 < lines.size() && offsets[i + 1] < b.start + span.length) ++i;
            b.text = source.mid(b.start, offsets[i] + lines[i].size() - b.start);
        } else if (i + 1 < lines.size() && lines[i].contains('|') && lines[i + 1].contains('|')) {
            const auto header = cells(lines[i], 0);
            const auto divider = cells(lines[i + 1], 0);
            bool valid = header.size() == divider.size() && !header.isEmpty() && header.size() <= 128;
            for (const auto &c : divider) valid = valid && separator.match(lines[i + 1].mid(c.start, c.length)).hasMatch();
            if (valid) {
                b.kind = Kind::Table; b.rows.append(header);
                for (const auto &c : divider) {
                    const auto token = lines[i + 1].mid(c.start, c.length);
                    b.alignments.append(token.endsWith(':') ? (token.startsWith(':') ? Qt::AlignHCenter : Qt::AlignRight) : Qt::AlignLeft);
                }
                i += 1;
                while (i + 1 < lines.size() && !lines[i + 1].trimmed().isEmpty() && lines[i + 1].contains('|')) {
                    const auto row = cells(lines[i + 1], offsets[i + 1] - b.start);
                    // Preserve malformed/uneven tables literally instead of dropping cells.
                    if (row.size() != header.size()) break;
                    ++i; b.rows.append(row);
                }
                b.text = source.mid(b.start, offsets[i] + lines[i].size() - b.start);
            }
        }
        if (b.kind == Kind::Paragraph) {
            const auto h = heading.match(b.text); const auto l = list.match(b.text);
            if (h.hasMatch()) { b.kind = Kind::Heading; b.level = h.captured(1).size(); b.prefix = h.capturedStart(2); }
            else if (rule.match(b.text).hasMatch()) b.kind = Kind::Rule;
            else if (l.hasMatch()) { b.kind = Kind::List; b.prefix = l.capturedLength(); b.level = l.captured(1).size() / 2; }
            else if (b.text.startsWith(QStringLiteral("> "))) { b.kind = Kind::Quote; b.prefix = 2; }
            else if (b.text.startsWith(QStringLiteral("    ")) || b.text.startsWith('\t')) { b.kind = Kind::Code; b.prefix = b.text.startsWith('\t') ? 1 : 4; }
        }
        blocks.append(b);
    }
    return blocks;
}

QString MarkdownText::cacheKey(const Block &b, int activeStart, int activeEnd)
{
    QString key = QString::number(int(b.kind)) + ':' + QString::number(b.text.size()) + ':' + b.text;
    if (b.kind == Kind::Table) {
        for (int r = 0; r < b.rows.size(); ++r) for (int c = 0; c < b.rows[r].size(); ++c) {
            const auto cell = b.rows[r][c];
            if (activeStart <= cell.start + cell.length && activeEnd >= cell.start && activeStart >= 0)
                key += QStringLiteral("\ncell:%1:%2").arg(r).arg(c);
        }
    } else if (activeStart >= 0) key += QStringLiteral("\nsource");
    return key;
}

MarkdownText::Rendered MarkdownText::render(const Block &b, const QFont &font,
    const MathText::Images &images, int activeStart, int activeEnd, qreal dpr)
{
    Writer w(b, font, images, dpr);
    QTextCharFormat style; style.setFont(font);
    QTextBlockFormat paragraph; paragraph.setBottomMargin(4);
    const bool active = activeStart >= 0;
    if (b.kind == Kind::Heading) {
        constexpr qreal scales[]{1, 1.6, 1.35, 1.15, 1.1, 1.05, 1};
        style.setFontPointSize((font.pointSizeF() > 0 ? font.pointSizeF() : 10) * scales[b.level]);
        style.setFontWeight(QFont::Bold); paragraph.setHeadingLevel(b.level);
        paragraph.setTopMargin(8); paragraph.setBottomMargin(4);
    } else if (b.kind == Kind::Code) {
        style.setFontFamilies({QFontDatabase::systemFont(QFontDatabase::FixedFont).family()});
        paragraph.setBackground(QColor(UiTheme::Sidebar)); paragraph.setLeftMargin(8); paragraph.setRightMargin(8);
        paragraph.setNonBreakableLines(true);
    } else if (b.kind == Kind::Quote) {
        paragraph.setLeftMargin(18); style.setForeground(QColor(UiTheme::Muted));
    } else if (b.kind == Kind::List) paragraph.setLeftMargin(12 + b.level * 16);
    else if (b.kind == Kind::Formula && !active) { paragraph.setAlignment(Qt::AlignHCenter); paragraph.setTopMargin(8); paragraph.setBottomMargin(8); }
    w.cursor.setBlockFormat(paragraph);
    if (b.kind == Kind::Table) {
        QTextTableFormat tableFormat;
        tableFormat.setBorder(1); tableFormat.setBorderBrush(QColor(UiTheme::ControlBorder));
        tableFormat.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
        tableFormat.setCellSpacing(0); tableFormat.setCellPadding(7);
        tableFormat.setHeaderRowCount(1);
        auto *table = w.cursor.insertTable(b.rows.size(), b.rows[0].size(), tableFormat);
        for (int r = 0; r < b.rows.size(); ++r) for (int c = 0; c < b.rows[r].size(); ++c) {
            const Cell cell = b.rows[r][c];
            const auto target = table->cellAt(r, c);
            w.cursor = target.firstCursorPosition();
            QTextBlockFormat cellBlock; cellBlock.setAlignment(b.alignments[c]);
            w.cursor.setBlockFormat(cellBlock);
            auto cellStyle = style;
            if (r == 0) { cellStyle.setFontWeight(QFont::Bold); QTextTableCellFormat format; format.setBackground(QColor(UiTheme::Sidebar)); table->cellAt(r, c).setFormat(format); }
            w.point(w.cursor.position(), cell.start);
            if (active && activeStart <= cell.start + cell.length && activeEnd >= cell.start) w.text(cell.start, cell.length, cellStyle);
            else w.inlines(cell.start, cell.length, cellStyle);
        }
        w.cursor = table->lastCursorPosition(); w.cursor.movePosition(QTextCursor::NextBlock);
    } else if (active) w.text(0, b.text.size(), style);
    else if (b.kind == Kind::Formula) {
        const auto spans = MathText::spans(b.text);
        if (spans.size() == 1) w.formula(0, spans[0].length, style); else w.text(0, b.text.size(), style);
    } else if (b.kind == Kind::Code) w.text(qMin(b.prefix, int(b.text.size())), qMax(0, int(b.text.size()) - b.prefix - b.level), style);
    else if (b.kind == Kind::Rule) { w.cursor.setBlockFormat(paragraph); w.literal(QStringLiteral("────────────"), 0, b.text.size(), style); }
    else {
        if (b.kind == Kind::List) {
            const auto marker = b.text.left(b.prefix).trimmed();
            w.literal(marker.size() == 1 ? QStringLiteral("• ") : marker + ' ', 0, b.prefix, style);
        }
        w.inlines(b.prefix, b.text.size() - b.prefix, style);
    }
    w.point(w.document.characterCount() - 1, b.text.size());
    // Every frame/cell boundary gets a stable source offset, including empty cells.
    for (int i = 1; i < w.positions.size(); ++i) w.positions[i] = qMax(w.positions[i - 1], w.positions[i]);
    return {QTextDocumentFragment(&w.document), w.positions, cacheKey(b, activeStart, activeEnd), w.document.begin().blockFormat()};
}
