#include "headingtextedit.h"
#include "headingtext.h"

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

HeadingTextEdit::HeadingTextEdit(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    tabs_ = new QTabBar(this);
    tabs_->setObjectName(QStringLiteral("headingModeTabs"));
    tabs_->setAccessibleName(QStringLiteral("正文显示模式"));
    tabs_->setExpanding(false);
    tabs_->setStyleSheet(QStringLiteral(
        "QTabBar::tab { font-size: 14px; min-width: 48px; min-height: 20px; padding: 8px 14px; margin-right: 4px; border: 1px solid #d4deeb; border-radius: 6px; background: #f5f7fb; color: #526176; }"
        "QTabBar::tab:selected { background: #e8f0ff; color: #2456a6; border-color: #9bbbe8; font-weight: 600; }"
        "QTabBar::tab:hover { background: #edf3ff; }"));
    tabs_->addTab(QStringLiteral("编辑"));
    tabs_->addTab(QStringLiteral("预览"));
    layout->addWidget(tabs_);
    pages_ = new QStackedWidget(this);
    // QTextEdit's default size hint is tall. Let embedded editors shrink while
    // retaining a usable text area, rather than forcing the surrounding form.
    pages_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    pages_->setMinimumHeight(80);
    source_ = new QPlainTextEdit(pages_);
    source_->setObjectName(QStringLiteral("headingSource"));
    source_->setAccessibleName(QStringLiteral("正文原文"));
    source_->setTabChangesFocus(true);
    source_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    preview_ = new QTextEdit(pages_);
    preview_->setObjectName(QStringLiteral("headingPreview"));
    preview_->setAccessibleName(QStringLiteral("三级标题预览"));
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
    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (index == 1) updatePreview();
        pages_->setCurrentIndex(index);
        setFocusProxy(index == 0 ? static_cast<QWidget *>(source_) : preview_);
        emit previewModeChanged(index == 1);
    });
    connect(source_, &QPlainTextEdit::textChanged, this, [this] {
        if (isPreviewMode()) updatePreview();
        emit textChanged();
    });
}

void HeadingTextEdit::setPlainText(const QString &text) { source_->setPlainText(text); }
QString HeadingTextEdit::toPlainText() const { return source_->toPlainText(); }
void HeadingTextEdit::setPlaceholderText(const QString &text) { source_->setPlaceholderText(text); }
void HeadingTextEdit::clear() { source_->clear(); setPreviewMode(false); }
void HeadingTextEdit::setPreviewMode(bool preview) { tabs_->setCurrentIndex(preview ? 1 : 0); }
bool HeadingTextEdit::isPreviewMode() const { return tabs_->currentIndex() == 1; }

void HeadingTextEdit::updatePreview()
{
    QFont font = source_->font();
    if (font.pointSizeF() <= 0) font.setPointSizeF(QFontInfo(font).pointSizeF());
    preview_->document()->setDefaultFont(font);
    HeadingText::render(*preview_->document(), source_->toPlainText());
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
    const qreal newSize = qBound(6.0, size + steps, 72.0);
    if (qFuzzyCompare(size, newSize)) return;

    const int position = preview_->textCursor().position();
    const int anchor = preview_->textCursor().anchor();
    const auto topCursor = preview_->cursorForPosition(QPoint(0, 0));
    const int topPosition = topCursor.position();
    const int topOffset = preview_->cursorRect(topCursor).top();

    // Change only the display font, keeping the source text and undo history.
    font.setPointSizeF(newSize);
    source_->setFont(font);
    if (!isPreviewMode()) return;
    updatePreview();

    // Rendering headings rebuilds the preview; retain its selection and visible text.
    QTextCursor cursor(preview_->document());
    cursor.setPosition(anchor);
    cursor.setPosition(position, QTextCursor::KeepAnchor);
    preview_->setTextCursor(cursor);
    cursor.setPosition(topPosition);
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
