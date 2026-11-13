#include "markdownedit.h"
#include "mathpreview.h"
#include "textlayoutguard.h"
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDropEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextTable>
#include <algorithm>

MarkdownEdit::MarkdownEdit(QPlainTextEdit *source, QWidget *parent)
    : QTextEdit(parent), source_(source), math_(new MathPreview(this))
{
    setAcceptRichText(false);
    setAcceptDrops(false); // A move across projected syntax must never lose source.
    setUndoRedoEnabled(false);
    refreshTimer_.setSingleShot(true);
    connect(&refreshTimer_, &QTimer::timeout, this, &MarkdownEdit::refresh);
    connect(document(), &QTextDocument::contentsChange, this, &MarkdownEdit::changed);
    connect(this, &QTextEdit::cursorPositionChanged, this, &MarkdownEdit::rememberCursor);
    connect(this, &QTextEdit::selectionChanged, this, &MarkdownEdit::rememberCursor);
    connect(source_, &QPlainTextEdit::textChanged, this, &MarkdownEdit::syncSource);
    connect(math_, &MathPreview::ready, this, [this] {
        // Only blocks containing formulas need rebuilding when results arrive.
        for (int i = 0; i < blocks_.size(); ++i)
            if (!MathText::spans(blocks_[i].text).isEmpty()) rendered_[i].key.clear();
        if (isVisible() || isReadOnly()) refreshTimer_.start(0);
        updateMathStatus();
    });
    syncSource();
}

void MarkdownEdit::setReading(bool reading)
{
    if (isReadOnly() == reading) return;
    setReadOnly(reading);
    refresh();
}

void MarkdownEdit::setBaseFont(const QFont &font)
{
    if (this->font() == font && !force_) return;
    updating_ = true;
    setFont(font); document()->setDefaultFont(font);
    updating_ = false;
    force_ = true; refresh();
}

void MarkdownEdit::syncSource()
{
    if (updating_) return;
    const auto cursor = source_->textCursor();
    position_ = cursor.position(); anchor_ = cursor.anchor();
    if (isVisible() || isReadOnly()) refreshTimer_.start(0);
}

int MarkdownEdit::sourcePosition(int p) const
{
    if (blocks_.isEmpty()) return 0;
    auto it = std::upper_bound(offsets_.begin(), offsets_.end(), p);
    const int index = qMax(0, int(it - offsets_.begin()) - 1);
    const auto &map = rendered_[index].sourcePositions;
    return blocks_[index].start + map[qBound(0, p - offsets_[index], int(map.size()) - 1)];
}

int MarkdownEdit::viewPosition(int p) const
{
    if (blocks_.isEmpty()) return 0;
    int index = 0;
    while (index + 1 < blocks_.size() && blocks_[index + 1].start <= p) ++index;
    const auto &map = rendered_[index].sourcePositions;
    const int local = p - blocks_[index].start;
    auto found = std::lower_bound(map.begin(), map.end(), local);
    return offsets_[index] + qMin(int(found - map.begin()), int(map.size()) - 1);
}

void MarkdownEdit::setSourceCursor(int position, int anchor)
{
    position_ = qBound(0, position, int(source_->toPlainText().size()));
    anchor_ = anchor < 0 ? position_ : qBound(0, anchor, int(source_->toPlainText().size()));
    refresh();
}

void MarkdownEdit::rememberCursor()
{
    if (updating_ || composing_ || snapshot_ != source_->toPlainText()) return;
    const auto cursor = textCursor();
    position_ = sourcePosition(cursor.position()); anchor_ = sourcePosition(cursor.anchor());
    if (cursor.hasSelection()) {
        if (cursor.position() == 0) position_ = 0;
        if (cursor.anchor() == 0) anchor_ = 0;
        if (cursor.position() == document()->characterCount() - 1) position_ = snapshot_.size();
        if (cursor.anchor() == document()->characterCount() - 1) anchor_ = snapshot_.size();
    }
    if (!isReadOnly()) refreshTimer_.start(0);
}

void MarkdownEdit::refresh()
{
    if (updating_ || composing_ || selecting_) return;
    refreshTimer_.stop();
    const QString text = source_->toPlainText();
    const int topSource = sourcePosition(cursorForPosition(QPoint(0, 0)).position());
    const int topOffset = cursorRect(cursorForPosition(QPoint(0, 0))).top();
    const auto next = MarkdownText::parse(text);
    QVector<MarkdownText::Rendered> presentation;
    presentation.reserve(next.size());
    // Match the unchanged tail as well as the prefix, so edits don't invalidate
    // the rest of a long note merely because its absolute offsets moved.
    int commonPrefix = 0, commonSuffix = 0;
    while (commonPrefix < blocks_.size() && commonPrefix < next.size()
        && blocks_[commonPrefix].text == next[commonPrefix].text && blocks_[commonPrefix].kind == next[commonPrefix].kind) ++commonPrefix;
    while (commonSuffix < blocks_.size() - commonPrefix && commonSuffix < next.size() - commonPrefix
        && blocks_[blocks_.size() - 1 - commonSuffix].text == next[next.size() - 1 - commonSuffix].text
        && blocks_[blocks_.size() - 1 - commonSuffix].kind == next[next.size() - 1 - commonSuffix].kind) ++commonSuffix;
    math_->request(text, font().pointSizeF() * logicalDpiY() / 72.0);
    renderedCount_ = 0;
    for (int i = 0; i < next.size(); ++i) {
        const auto &b = next[i];
        int start = -1, end = -1;
        const int low = qMin(position_, anchor_), high = qMax(position_, anchor_);
        if (!isReadOnly() && high >= b.start && low <= b.start + b.text.size()) {
            start = qMax(0, low - b.start); end = qMin(int(b.text.size()), high - b.start);
        }
        const QString key = MarkdownText::cacheKey(b, start, end);
        const int old = i < commonPrefix ? i : i >= next.size() - commonSuffix ? i + blocks_.size() - next.size() : -1;
        if (!force_ && old >= 0 && rendered_[old].key == key) presentation.append(rendered_[old]);
        else { presentation.append(MarkdownText::render(b, font(), math_->images(), start, end, devicePixelRatioF())); ++renderedCount_; }
    }
    int prefix = 0, suffix = 0;
    if (!force_) {
        while (prefix < rendered_.size() && prefix < presentation.size() && rendered_[prefix].key == presentation[prefix].key) ++prefix;
        while (suffix < rendered_.size() - prefix && suffix < presentation.size() - prefix
            && rendered_[rendered_.size() - 1 - suffix].key == presentation[presentation.size() - 1 - suffix].key) ++suffix;
    }
    updating_ = true;
    const bool longLine = TextLayoutGuard::hasLongLine(text);
    // Do not enable wrapping on the old, potentially enormous document when
    // undoing a paste or loading a shorter note.
    if (longLine) setLineWrapMode(QTextEdit::NoWrap);
    if (prefix != presentation.size() || prefix != rendered_.size()) {
        QTextCursor patch(document());
        int begin = prefix < offsets_.size() ? offsets_[prefix] : document()->characterCount() - 1;
        int finish = suffix ? offsets_[rendered_.size() - suffix] : document()->characterCount() - 1;
        // Separators belong to the following block at the end of the document.
        if (prefix && !suffix) begin = qMax(0, begin - 1);
        patch.setPosition(begin); patch.setPosition(finish, QTextCursor::KeepAnchor);
        patch.removeSelectedText();
        if (prefix && !suffix) patch.insertBlock();
        for (int i = prefix; i < presentation.size() - suffix; ++i) {
            if (i > prefix) patch.insertBlock();
            patch.setBlockFormat(presentation[i].firstBlockFormat);
            patch.insertFragment(presentation[i].fragment);
        }
        if (suffix && prefix < presentation.size() - suffix) patch.insertBlock();
    }
    blocks_ = next; rendered_ = presentation; snapshot_ = text;
    offsets_.clear(); int pos = 0;
    for (const auto &r : rendered_) { offsets_.append(pos); pos += r.length() + 1; }
    force_ = false;
    if (!longLine) setLineWrapMode(QTextEdit::WidgetWidth);
    auto cursor = textCursor();
    cursor.setPosition(qBound(0, viewPosition(anchor_), document()->characterCount() - 1));
    cursor.setPosition(qBound(0, viewPosition(position_), document()->characterCount() - 1), QTextCursor::KeepAnchor);
    setTextCursor(cursor);
    QTextCursor top(document()); top.setPosition(qBound(0, viewPosition(topSource), document()->characterCount() - 1));
    verticalScrollBar()->setValue(verticalScrollBar()->value() + cursorRect(top).top() - topOffset);
    updating_ = false;
    updateMathStatus();
}

void MarkdownEdit::updateMathStatus()
{
    if (math_->pending()) { emit mathStatusChanged(QStringLiteral("公式排版中…")); return; }
    const auto text = source_->toPlainText();
    for (const auto &span : MathText::spans(text)) if (!math_->images().contains(text.mid(span.start, span.length))) {
        emit mathStatusChanged(QStringLiteral("部分公式暂未渲染，已保留原文")); return;
    }
    emit mathStatusChanged({});
}

void MarkdownEdit::changed(int position, int removed, int added)
{
    if (updating_ || isReadOnly()) return;
    const int start = sourcePosition(position), end = sourcePosition(position + removed);
    QTextCursor addedText(document());
    addedText.setPosition(position); addedText.setPosition(position + added, QTextCursor::KeepAnchor);
    QString value = addedText.selectedText(); value.replace(QChar::ParagraphSeparator, '\n');
    updating_ = true;
    auto cursor = source_->textCursor();
    cursor.setPosition(start); cursor.setPosition(end, QTextCursor::KeepAnchor);
    TextLayoutGuard::prepareInsertion(source_, cursor, value);
    cursor.insertText(value); source_->setTextCursor(cursor);
    position_ = anchor_ = cursor.position();
    updating_ = false;
    // Qt has already changed the live document. Restore its affected segment
    // before the next projection diff, while keeping IME preedit undisturbed.
    force_ = true;
    refreshTimer_.start(0);
}

void MarkdownEdit::replaceSource(int start, int length, const QString &text)
{
    updating_ = true;
    auto cursor = source_->textCursor();
    cursor.setPosition(start); cursor.setPosition(start + length, QTextCursor::KeepAnchor);
    TextLayoutGuard::prepareInsertion(source_, cursor, text);
    cursor.insertText(text); source_->setTextCursor(cursor);
    position_ = anchor_ = cursor.position();
    updating_ = false; refresh(); ensureCursorVisible();
}

void MarkdownEdit::history(bool redo)
{
    updating_ = true;
    auto cursor = source_->textCursor();
    if (redo) source_->document()->redo(&cursor); else source_->document()->undo(&cursor);
    source_->setTextCursor(cursor); position_ = anchor_ = cursor.position();
    updating_ = false; refresh(); ensureCursorVisible();
}

void MarkdownEdit::keyPressEvent(QKeyEvent *event)
{
    if (isReadOnly()) { QTextEdit::keyPressEvent(event); return; }
    refresh();
    if (event->matches(QKeySequence::Undo)) { history(false); return; }
    if (event->matches(QKeySequence::Redo)) { history(true); return; }
    if (event->matches(QKeySequence::Cut)) {
        copy(); replaceSource(qMin(position_, anchor_), qAbs(position_ - anchor_), {}); return;
    }
    if (event->matches(QKeySequence::Paste)) { insertFromMimeData(QApplication::clipboard()->mimeData()); return; }
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        if (tableMove(event->key() == Qt::Key_Backtab || event->modifiers().testFlag(Qt::ShiftModifier))) return;
    }
    // Ordinary typing edits the canonical document directly; this preserves
    // Qt's native undo coalescing and avoids rebuilding unaffected paragraphs.
    if (!event->text().isEmpty() && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        && event->key() != Qt::Key_Backspace && event->key() != Qt::Key_Delete) {
        QString value = event->text(); value.replace('\r', '\n');
        replaceSource(qMin(position_, anchor_), qAbs(position_ - anchor_), value); return;
    }
    if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) {
        auto cursor = source_->textCursor(); cursor.setPosition(anchor_); cursor.setPosition(position_, QTextCursor::KeepAnchor);
        if (!cursor.hasSelection()) cursor.movePosition(event->key() == Qt::Key_Backspace
            ? (event->modifiers().testFlag(Qt::ControlModifier) ? QTextCursor::PreviousWord : QTextCursor::PreviousCharacter)
            : (event->modifiers().testFlag(Qt::ControlModifier) ? QTextCursor::NextWord : QTextCursor::NextCharacter), QTextCursor::KeepAnchor);
        replaceSource(cursor.selectionStart(), cursor.selectionEnd() - cursor.selectionStart(), {}); return;
    }
    QTextEdit::keyPressEvent(event);
}

void MarkdownEdit::inputMethodEvent(QInputMethodEvent *event)
{
    if (isReadOnly()) { event->ignore(); return; }
    if (!composing_) refresh();
    const bool commit = !event->commitString().isEmpty() || event->replacementLength() != 0;
    if (commit || event->preeditString().isEmpty()) {
        // Preedit is transient. Never put it (or its temporary removal of a
        // selection) into the source undo history. A partial commit may start
        // another preedit in the same event.
        updating_ = true;
        QInputMethodEvent clear;
        QTextEdit::inputMethodEvent(&clear);
        updating_ = false;
        composing_ = false;
        force_ = true;
        if (commit) {
            const int start = event->replacementLength() || event->replacementStart()
                ? qBound(0, position_ + event->replacementStart(), int(source_->toPlainText().size())) : qMin(position_, anchor_);
            const int count = event->replacementLength() ? qMin(event->replacementLength(), int(source_->toPlainText().size()) - start) : qAbs(position_ - anchor_);
            replaceSource(start, count, event->commitString());
        } else refresh();
    }
    if (!event->preeditString().isEmpty()) {
        composing_ = true;
        updating_ = true;
        QInputMethodEvent preedit(event->preeditString(), event->attributes());
        QTextEdit::inputMethodEvent(&preedit);
        updating_ = false;
    }
    event->accept();
}

void MarkdownEdit::insertFromMimeData(const QMimeData *data)
{
    if (isReadOnly() || !data->hasText()) return;
    refresh();
    replaceSource(qMin(position_, anchor_), qAbs(position_ - anchor_), data->text());
}

QMimeData *MarkdownEdit::createMimeDataFromSelection() const
{
    auto *data = new QMimeData;
    const auto cursor = textCursor();
    const int start = cursor.selectionStart() == 0 ? 0 : sourcePosition(cursor.selectionStart());
    const int end = cursor.selectionEnd() == document()->characterCount() - 1 ? snapshot_.size() : sourcePosition(cursor.selectionEnd());
    data->setText(snapshot_.mid(start, end - start)); return data;
}

void MarkdownEdit::dropEvent(QDropEvent *event) { event->ignore(); }

void MarkdownEdit::showEvent(QShowEvent *event)
{
    QTextEdit::showEvent(event);
    refresh();
}

void MarkdownEdit::mousePressEvent(QMouseEvent *event)
{
    selecting_ = event->button() == Qt::LeftButton;
    QTextEdit::mousePressEvent(event);
}

void MarkdownEdit::mouseDoubleClickEvent(QMouseEvent *event)
{
    selecting_ = event->button() == Qt::LeftButton;
    QTextEdit::mouseDoubleClickEvent(event);
}

void MarkdownEdit::mouseReleaseEvent(QMouseEvent *event)
{
    QTextEdit::mouseReleaseEvent(event);
    selecting_ = false;
    refresh();
}

int MarkdownEdit::currentTable(int *row, int *column) const
{
    for (int i = 0; i < blocks_.size(); ++i) {
        const auto &b = blocks_[i];
        if (b.kind != MarkdownText::Kind::Table || position_ < b.start || position_ > b.start + b.text.size()) continue;
        for (int r = 0; r < b.rows.size(); ++r) for (int c = 0; c < b.rows[r].size(); ++c) {
            const auto cell = b.rows[r][c];
            if (position_ >= b.start + cell.start && position_ <= b.start + cell.start + cell.length) {
                if (row) *row = r; if (column) *column = c; return i;
            }
        }
    }
    return -1;
}

bool MarkdownEdit::tableMove(bool backward)
{
    int row, col; const int i = currentTable(&row, &col); if (i < 0) return false;
    const auto b = blocks_[i]; const int columns = b.rows[0].size();
    int next = row * columns + col + (backward ? -1 : 1);
    if (next < 0) { setSourceCursor(b.start); return true; }
    if (next >= b.rows.size() * columns) { tableOperation(0); return true; }
    const auto cell = b.rows[next / columns][next % columns];
    setSourceCursor(b.start + cell.start + cell.length, b.start + cell.start); ensureCursorVisible(); return true;
}

void MarkdownEdit::tableOperation(int operation)
{
    int row, col; const int index = currentTable(&row, &col); if (index < 0) return;
    const auto b = blocks_[index];
    QVector<QStringList> rows;
    for (const auto &line : b.rows) {
        QStringList values; for (const auto &c : line) values.append(b.text.mid(c.start, c.length)); rows.append(values);
    }
    auto alignment = b.alignments;
    if (operation == 0) { QStringList values; for (int c = 0; c < rows[0].size(); ++c) values.append(QString()); rows.insert(row + 1, values); ++row; col = 0; }
    if (operation == 1) { if (row == 0) return; rows.removeAt(row); row = qMin(row, int(rows.size()) - 1); }
    if (operation == 2) { for (auto &line : rows) line.insert(col + 1, QString()); alignment.insert(col + 1, Qt::AlignLeft); ++col; }
    if (operation == 3) { if (rows[0].size() == 1) return; for (auto &line : rows) line.removeAt(col); alignment.removeAt(col); col = qMin(col, int(rows[0].size()) - 1); }
    QStringList lines;
    for (int r = 0; r < rows.size(); ++r) {
        lines.append("| " + rows[r].join(" | ") + " |");
        if (r == 0) {
            QStringList separators; for (auto a : alignment) separators.append(a == Qt::AlignRight ? "---:" : a == Qt::AlignHCenter ? ":---:" : "---");
            lines.append("| " + separators.join(" | ") + " |");
        }
    }
    replaceSource(b.start, b.text.size(), lines.join('\n'));
    const auto fresh = MarkdownText::parse(lines.join('\n')).first();
    setSourceCursor(b.start + fresh.rows[row][col].start);
}

void MarkdownEdit::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    auto *copyAction = menu.addAction(QStringLiteral("复制"), QKeySequence::Copy, this, &QTextEdit::copy);
    copyAction->setEnabled(textCursor().hasSelection());
    menu.addAction(QStringLiteral("全选"), QKeySequence::SelectAll, this, &QTextEdit::selectAll);
    if (!isReadOnly()) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("撤销"), QKeySequence::Undo, this, [this] { history(false); })->setEnabled(source_->document()->isUndoAvailable());
        menu.addAction(QStringLiteral("重做"), QKeySequence::Redo, this, [this] { history(true); })->setEnabled(source_->document()->isRedoAvailable());
        menu.addAction(QStringLiteral("剪切"), QKeySequence::Cut, this, [this] { copy(); replaceSource(qMin(position_, anchor_), qAbs(position_ - anchor_), {}); })->setEnabled(textCursor().hasSelection());
        menu.addAction(QStringLiteral("粘贴"), QKeySequence::Paste, this, [this] { insertFromMimeData(QApplication::clipboard()->mimeData()); })->setEnabled(QApplication::clipboard()->mimeData()->hasText());
        int row, col;
        if (currentTable(&row, &col) >= 0) {
            menu.addSeparator();
            menu.addAction(QStringLiteral("在下方插入行"), this, [this] { tableOperation(0); });
            menu.addAction(QStringLiteral("删除当前行"), this, [this] { tableOperation(1); })->setEnabled(row > 0);
            menu.addAction(QStringLiteral("在右侧插入列"), this, [this] { tableOperation(2); });
            menu.addAction(QStringLiteral("删除当前列"), this, [this] { tableOperation(3); })->setEnabled(blocks_[currentTable()].rows[0].size() > 1);
        }
        menu.addSeparator(); menu.addAction(QStringLiteral("编辑 Markdown 源码"), this, &MarkdownEdit::sourceModeRequested);
    }
    menu.exec(event->globalPos());
}
