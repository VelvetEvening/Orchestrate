#pragma once

#include "mathtext.h"
#include <QTextDocumentFragment>
#include <QFont>
#include <QTextBlockFormat>

// A source-preserving presentation. Offsets are UTF-16, as in QTextCursor.
// No HTML import or Markdown serialization is used on the save path.
namespace MarkdownText {
enum class Kind { Paragraph, Heading, List, Quote, Code, Formula, Table, Rule };
struct Cell { int start = 0, length = 0; };
struct Block {
    int start = 0;
    QString text;
    Kind kind = Kind::Paragraph;
    int level = 0, prefix = 0;
    QVector<QVector<Cell>> rows;
    QVector<Qt::Alignment> alignments;
};
struct Rendered {
    QTextDocumentFragment fragment;
    QVector<int> sourcePositions;
    QString key;
    QTextBlockFormat firstBlockFormat;
    int length() const { return sourcePositions.size() - 1; }
};
QVector<Block> parse(const QString &source);
Rendered render(const Block &block, const QFont &font, const MathText::Images &images,
                int activeStart = -1, int activeEnd = -1, qreal devicePixelRatio = 1);
QString cacheKey(const Block &block, int activeStart, int activeEnd);
}
