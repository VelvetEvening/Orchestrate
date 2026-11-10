#pragma once

#include <QString>
#include "mathtext.h"

class QListWidget;
class QTextDocument;

// Deliberately not Markdown: only a line-start # / ## / ### followed by an
// ASCII space and nonblank text is a heading. Optional, locally rendered math
// images replace explicit math delimiters; other Markdown remains literal.
namespace HeadingText {
void render(QTextDocument &document, const QString &source, const MathText::Images &images = {}, qreal devicePixelRatio = 1);
QString toolTipHtml(const QString &source);
void configureList(QListWidget *list);
}
