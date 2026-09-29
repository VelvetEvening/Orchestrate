#pragma once

#include <QString>

class QListWidget;
class QTextDocument;

// Deliberately not Markdown: only a line-start # / ## / ### followed by an
// ASCII space and nonblank text is a heading. Everything else is literal text.
namespace HeadingText {
void render(QTextDocument &document, const QString &source);
QString toolTipHtml(const QString &source);
void configureList(QListWidget *list);
}
