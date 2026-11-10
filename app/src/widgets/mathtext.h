#pragma once
#include <QHash>
#include <QImage>
#include <QString>
#include <QVector>
#include <QTextFormat>

class QTextCursor;
class QTextDocument;

namespace MathText {
constexpr int SourceProperty = QTextFormat::UserProperty + 31;
struct Span { qsizetype start, length; QString code; bool display; };
using Images = QHash<QString, QImage>; // Delimited source -> 3x raster, local to one preview.
QVector<Span> spans(const QString &source);
QString selectedText(const QTextCursor &cursor);
int expandedPosition(const QTextDocument &document, int position);
int collapsedPosition(const QTextDocument &document, int position);
}
