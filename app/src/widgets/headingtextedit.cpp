#include "headingtextedit.h"
#include "headingtext.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabBar>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>

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
    preview_->document()->setDefaultFont(source_->font());
    HeadingText::render(*preview_->document(), source_->toPlainText());
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
