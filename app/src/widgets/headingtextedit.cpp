#include "headingtextedit.h"
#include "headingtext.h"
#include "textlayoutguard.h"
#include "mathpreview.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFontInfo>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabBar>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QMimeData>
#include <QLabel>
#include <QHBoxLayout>
#include <QtMath>

namespace {
constexpr int PreviewTab = 0;
constexpr int EditTab = 1;
class FormulaPreview final : public QTextEdit {
public:
    using QTextEdit::QTextEdit;
protected:
    QMimeData *createMimeDataFromSelection() const override {
        auto *data = new QMimeData;
        data->setText(MathText::selectedText(textCursor()));
        return data;
    }
};
}

HeadingTextEdit::HeadingTextEdit(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    tabs_ = new QTabBar(this);
    tabs_->setObjectName(QStringLiteral("headingModeTabs"));
    tabs_->setAccessibleName(QStringLiteral("正文显示模式"));
    tabs_->setExpanding(false);
    tabs_->setDrawBase(false);

    tabs_->addTab(QStringLiteral("预览"));
    tabs_->addTab(QStringLiteral("编辑"));
    tabs_->setTabToolTip(PreviewTab, QStringLiteral("标题与公式预览；行内公式用 $…$ 或 \\(…\\)，独立公式用 $$…$$ 或 \\[…\\]。"));
    // New content starts editable; existing documents explicitly select preview.
    tabs_->setCurrentIndex(EditTab);
    auto *modes = new QHBoxLayout;
    modes->addWidget(tabs_);
    mathStatus_ = new QLabel(this);
    mathStatus_->setObjectName(QStringLiteral("mathPreviewStatus"));
    mathStatus_->setWordWrap(true);
    modes->addWidget(mathStatus_, 1);
    layout->addLayout(modes);
    pages_ = new QStackedWidget(this);
    // QTextEdit's default size hint is tall. Let embedded editors shrink while
    // retaining a usable text area, rather than forcing the surrounding form.
    pages_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    pages_->setMinimumHeight(80);
    source_ = new TextLayoutGuard::Edit(pages_);
    source_->setObjectName(QStringLiteral("headingSource"));
    source_->setAccessibleName(QStringLiteral("正文原文"));
    source_->setTabChangesFocus(true);
    source_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    preview_ = new FormulaPreview(pages_);
    preview_->setObjectName(QStringLiteral("headingPreview"));
    preview_->setAccessibleName(QStringLiteral("标题与公式预览"));
    preview_->setReadOnly(true);
    preview_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    preview_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    for (auto *viewport : {source_->viewport(), preview_->viewport()}) {
        viewport->installEventFilter(this);
        viewport->setToolTip(QStringLiteral("Ctrl + 鼠标滚轮缩放文字"));
    }
    pages_->addWidget(source_);
    pages_->addWidget(preview_);
    layout->addWidget(pages_, 1);
    setMinimumHeight(170);
    setFocusProxy(source_);
    math_ = new MathPreview(this);
    connect(math_, &MathPreview::ready, this, [this] {
        if (!isPreviewMode()) return;
        auto *document = preview_->document();
        const auto selection = preview_->textCursor();
        const int position = MathText::expandedPosition(*document, selection.position());
        const int anchor = MathText::expandedPosition(*document, selection.anchor());
        const auto top = preview_->cursorForPosition(QPoint(0, 0));
        const int topPosition = MathText::expandedPosition(*document, top.position());
        const int topOffset = preview_->cursorRect(top).top();
        HeadingText::render(*document, source_->toPlainText(), math_->images(), preview_->devicePixelRatioF());
        QTextCursor cursor(document);
        cursor.setPosition(MathText::collapsedPosition(*document, anchor));
        cursor.setPosition(MathText::collapsedPosition(*document, position), QTextCursor::KeepAnchor);
        preview_->setTextCursor(cursor);
        cursor.setPosition(MathText::collapsedPosition(*document, topPosition));
        auto *scroll = preview_->verticalScrollBar();
        scroll->setValue(scroll->value() + preview_->cursorRect(cursor).top() - topOffset);
        const auto rawText = source_->toPlainText();
        const auto spans = MathText::spans(rawText);
        bool fallback = false;
        for (const auto &span : spans)
            if (!math_->images().contains(rawText.mid(span.start, span.length))) { fallback = true; break; }
        mathStatus_->setText(fallback ? QStringLiteral("部分公式暂未渲染，已保留原文") : QString());
    });
    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        const bool preview = index == PreviewTab;
        if (preview) updatePreview();
        else {
            if (math_->pending()) { math_->cancel(); previewDirty_ = true; }
            mathStatus_->clear();
        }
        QWidget *page = preview ? static_cast<QWidget *>(preview_) : source_;
        pages_->setCurrentWidget(page);
        setFocusProxy(page);
        emit previewModeChanged(preview);
    });
    connect(source_, &QPlainTextEdit::textChanged, this, [this] {
        previewDirty_ = true;
        if (!isPreviewMode()) math_->cancel();
        if (isPreviewMode()) updatePreview();
        emit textChanged();
    });
}

void HeadingTextEdit::setPlainText(const QString &text) { TextLayoutGuard::setPlainText(source_, text); }
QString HeadingTextEdit::toPlainText() const { return source_->toPlainText(); }
void HeadingTextEdit::setPlaceholderText(const QString &text) { source_->setPlaceholderText(text); }
void HeadingTextEdit::clear() { setPlainText({}); setPreviewMode(false); }
void HeadingTextEdit::setPreviewMode(bool preview) { tabs_->setCurrentIndex(preview ? PreviewTab : EditTab); }
bool HeadingTextEdit::isPreviewMode() const { return tabs_->currentIndex() == PreviewTab; }
void HeadingTextEdit::setModeControlsVisible(bool visible)
{
    tabs_->setVisible(visible);
    setMinimumHeight(visible ? 170 : 80);
}

void HeadingTextEdit::setContentLabel(const QString &label)
{
    setAccessibleName(label);
    source_->setAccessibleName(label + QStringLiteral("原文"));
    preview_->setAccessibleName(label + QStringLiteral("阅读"));
    tabs_->setAccessibleName(label + QStringLiteral("显示模式"));
}

void HeadingTextEdit::updatePreview()
{
    QFont font = source_->font();
    if (font.pointSizeF() <= 0) font.setPointSizeF(QFontInfo(font).pointSizeF());
    if (!previewDirty_ && preview_->document()->defaultFont() == font) return;
    const QString text = source_->toPlainText();
    // Discard the previous layout before changing its width/font; otherwise
    // switching from a long document to a short one can reflow the old text.
    preview_->document()->clear();
    preview_->document()->setDefaultFont(font);
    preview_->setLineWrapMode(TextLayoutGuard::hasLongLine(text) ? QTextEdit::NoWrap : QTextEdit::WidgetWidth);
    HeadingText::render(*preview_->document(), text);
    const bool formulas = !MathText::spans(text).isEmpty();
    mathStatus_->setText(formulas ? QStringLiteral("公式排版中…") : QString());
    math_->request(text, font.pointSizeF() * preview_->logicalDpiY() / 72.0);
    // Inserting rendered text moves the viewer's cursor to the end. New content
    // starts at the top; an unchanged preview keeps its cursor and reading position.
    preview_->moveCursor(QTextCursor::Start);
    previewDirty_ = false;
}

bool HeadingTextEdit::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == source_->viewport() || watched == preview_->viewport())
        && event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (wheel->modifiers().testFlag(Qt::ControlModifier)) {
            const qreal steps = wheel->angleDelta().y() != 0
                ? wheel->angleDelta().y() / 120.0 : wheel->pixelDelta().y() / 40.0;
            zoomText(steps);
            wheel->accept();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void HeadingTextEdit::zoomText(qreal steps)
{
    if (qFuzzyIsNull(steps)) return;
    QFont font = source_->font();
    const qreal size = font.pointSizeF() > 0 ? font.pointSizeF() : QFontInfo(font).pointSizeF();
    setTextPointSize(size + steps);
}

void HeadingTextEdit::setTextPointSize(qreal requestedSize)
{
    if (!qIsFinite(requestedSize)) return;
    QFont font = source_->font();
    const qreal size = font.pointSizeF() > 0 ? font.pointSizeF() : QFontInfo(font).pointSizeF();
    const qreal newSize = qBound(6.0, requestedSize, 72.0);
    if (qFuzzyCompare(size, newSize)) return;

    const int position = MathText::expandedPosition(*preview_->document(), preview_->textCursor().position());
    const int anchor = MathText::expandedPosition(*preview_->document(), preview_->textCursor().anchor());
    const auto topCursor = preview_->cursorForPosition(QPoint(0, 0));
    const int topPosition = MathText::expandedPosition(*preview_->document(), topCursor.position());
    const int topOffset = preview_->cursorRect(topCursor).top();

    // Change only the display font, keeping the source text and undo history.
    font.setPointSizeF(newSize);
    source_->setFont(font);
    previewDirty_ = true;
    emit textPointSizeChanged(newSize);
    if (!isPreviewMode()) return;
    updatePreview();

    // Rendering headings rebuilds the preview; retain its selection and visible text.
    QTextCursor cursor(preview_->document());
    cursor.setPosition(MathText::collapsedPosition(*preview_->document(), anchor));
    cursor.setPosition(MathText::collapsedPosition(*preview_->document(), position), QTextCursor::KeepAnchor);
    preview_->setTextCursor(cursor);
    cursor.setPosition(MathText::collapsedPosition(*preview_->document(), topPosition));
    auto *scroll = preview_->verticalScrollBar();
    scroll->setValue(scroll->value() + preview_->cursorRect(cursor).top() - topOffset);
}

QString HeadingTextEdit::getText(QWidget *parent, const QString &title,
                                const QString &initial, bool *accepted, bool previewFirst)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setWindowFlag(Qt::WindowMaximizeButtonHint);
    dialog.setMinimumSize(480, 340);
    dialog.resize(640, 460);
    auto *layout = new QVBoxLayout(&dialog);
    auto *editor = new HeadingTextEdit(&dialog);
    editor->setPlainText(initial);
    editor->setPreviewMode(previewFirst);
    layout->addWidget(editor, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    auto *save = buttons->button(QDialogButtonBox::Save);
    save->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    auto validate = [editor, save, initial, previewFirst] {
        save->setEnabled(!editor->toPlainText().trimmed().isEmpty()
                         && (!previewFirst || editor->toPlainText() != initial));
    };
    connect(editor, &HeadingTextEdit::textChanged, &dialog, validate);
    validate();
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    editor->setFocus();
    const bool ok = dialog.exec() == QDialog::Accepted;
    if (accepted) *accepted = ok;
    return ok ? editor->toPlainText() : initial;
}
