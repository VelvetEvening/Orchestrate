#pragma once

#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextCursor>
#include <QMimeData>
#include <QInputMethodEvent>

namespace TextLayoutGuard {
constexpr int longLine = 8192;

inline bool hasLongLine(const QString &text)
{
    int length = 0;
    for (const auto ch : text) {
        if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r') || ch == QChar::ParagraphSeparator) length = 0;
        else if (++length > longLine) return true;
    }
    return false;
}

// Qt's word-boundary fallback repeatedly scans a very long unbroken word.
// Mixed scripts can also make WrapAnywhere expensive. Use horizontal scrolling
// for documents with extreme lines, without inserting/removing any characters.
inline void updateMode(QPlainTextEdit *edit)
{
    // Keep the cheap layout through undo/redo, which may restore a large paste
    // through Qt's own actions. Loading another document resets this flag.
    bool longBlock = edit->property("longTextHistory").toBool();
    if (!longBlock) for (auto block = edit->document()->begin(); block.isValid(); block = block.next()) {
        if (block.length() > longLine + 1) { longBlock = true; edit->setProperty("longTextHistory", true); break; }
    }
    const auto mode = longBlock ? QPlainTextEdit::NoWrap : QPlainTextEdit::WidgetWidth;
    if (edit->lineWrapMode() != mode) edit->setLineWrapMode(mode);
}

inline void configure(QPlainTextEdit *edit)
{
    QObject::connect(edit->document(), &QTextDocument::contentsChange, edit, [edit] {
        if (!edit->property("preparingTextLayout").toBool()) updateMode(edit);
    });
}

inline void prepareInsertion(QPlainTextEdit *edit, const QTextCursor &cursor, const QString &text)
{
    if (edit->property("longTextHistory").toBool()) return;
    const auto first = edit->document()->findBlock(cursor.selectionStart());
    const auto last = edit->document()->findBlock(cursor.selectionEnd());
    int length = cursor.selectionStart() - first.position();
    bool longText = false;
    for (const auto ch : text) {
        if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r') || ch == QChar::ParagraphSeparator) length = 0;
        else if (++length > longLine) { longText = true; break; }
    }
    const int suffix = last.position() + last.length() - 1 - cursor.selectionEnd();
    if (longText || length + suffix > longLine) {
        edit->setProperty("longTextHistory", true);
        edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    }
}

inline void setPlainText(QPlainTextEdit *edit, const QString &text)
{
    // Set the layout before Qt inserts any blocks; a contentsChange callback
    // alone is too late when a long line follows an ordinary paragraph.
    edit->setProperty("preparingTextLayout", true);
    const bool longText = hasLongLine(text);
    edit->setProperty("longTextHistory", longText);
    // Never re-enable wrapping on the OLD long document while replacing it.
    if (longText) edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit->setPlainText(text);
    edit->setLineWrapMode(longText ? QPlainTextEdit::NoWrap : QPlainTextEdit::WidgetWidth);
    edit->setProperty("preparingTextLayout", false);
}

class Edit : public QPlainTextEdit {
public:
    explicit Edit(QWidget *parent = nullptr) : QPlainTextEdit(parent) { configure(this); }
protected:
    void insertFromMimeData(const QMimeData *data) override {
        setProperty("preparingTextLayout", true);
        prepareInsertion(this, textCursor(), data->text());
        QPlainTextEdit::insertFromMimeData(data);
        setProperty("preparingTextLayout", false);
        updateMode(this);
    }
    void inputMethodEvent(QInputMethodEvent *event) override {
        setProperty("preparingTextLayout", true);
        prepareInsertion(this, textCursor(), event->commitString());
        QPlainTextEdit::inputMethodEvent(event);
        setProperty("preparingTextLayout", false);
        updateMode(this);
    }
};
}
