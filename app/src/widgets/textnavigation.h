#pragma once

#include <QKeyEvent>
#include <QTextBlock>
#include <QTextCursor>

namespace TextNavigation {
// Complete vertical movement at either document edge, while retaining native
// movement between visual lines (including wrapped paragraphs).
inline bool moveToDocumentEdge(QKeyEvent *event, QTextCursor *cursor)
{
    const bool up = event->key() == Qt::Key_Up;
    if ((!up && event->key() != Qt::Key_Down)
        || (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) return false;
    if (up ? (cursor->atStart() || cursor->block().previous().isValid())
           : (cursor->atEnd() || cursor->block().next().isValid())) return false;
    auto next = *cursor;
    if (next.movePosition(up ? QTextCursor::Up : QTextCursor::Down)) return false;
    cursor->movePosition(up ? QTextCursor::Start : QTextCursor::End, event->modifiers().testFlag(Qt::ShiftModifier)
        ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
    event->accept();
    return true;
}
}
