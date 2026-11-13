#include "headingtextedit.h"
#include "markdownedit.h"
#include "textlayoutguard.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>
#include <memory>

namespace { constexpr int PreviewTab = 0, EditTab = 1; }

HeadingTextEdit::HeadingTextEdit(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(6);
    tabs_ = new QTabBar(this);
    tabs_->setObjectName(QStringLiteral("headingModeTabs"));
    tabs_->setAccessibleName(QStringLiteral("正文显示模式"));
    tabs_->setExpanding(false); tabs_->setDrawBase(false);
    tabs_->addTab(QStringLiteral("预览")); tabs_->addTab(QStringLiteral("编辑"));
    tabs_->setTabToolTip(PreviewTab, QStringLiteral("阅读排版后的正文"));
    tabs_->setTabToolTip(EditTab, QStringLiteral("直接编辑正文，其他段落保持排版"));
    tabs_->setCurrentIndex(EditTab);
    auto *modes = new QHBoxLayout;
    modes->addWidget(tabs_);
    mathStatus_ = new QLabel(this);
    mathStatus_->setObjectName(QStringLiteral("mathPreviewStatus")); mathStatus_->setWordWrap(true);
    mathStatus_->hide();
    modes->addWidget(mathStatus_, 1);
    layout->addLayout(modes);
    pages_ = new QStackedWidget(this);
    pages_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored); pages_->setMinimumHeight(80);
    source_ = new TextLayoutGuard::Edit(pages_);
    source_->setObjectName(QStringLiteral("headingSource")); source_->setTabChangesFocus(true);
    source_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    preview_ = new MarkdownEdit(source_, pages_);
    preview_->setObjectName(QStringLiteral("headingPreview"));
    preview_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    for (auto *viewport : {source_->viewport(), preview_->viewport()}) {
        viewport->installEventFilter(this); viewport->setToolTip(QStringLiteral("Ctrl + 鼠标滚轮缩放文字"));
    }
    pages_->addWidget(source_); pages_->addWidget(preview_); layout->addWidget(pages_, 1);
    setMinimumHeight(170); setContentLabel(QStringLiteral("正文"));
    connect(preview_, &MarkdownEdit::mathStatusChanged, this, [this](const QString &status) {
        mathStatus_->setText(status); mathStatus_->setVisible(!status.isEmpty());
    });
    connect(preview_, &MarkdownEdit::sourceModeRequested, this, [this] {
        setSourceMode(true); source_->setFocus();
    });
    source_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(source_, &QWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        std::unique_ptr<QMenu> menu(source_->createStandardContextMenu());
        menu->addSeparator();
        auto *live = menu->addAction(QStringLiteral("返回即时编辑"));
        if (menu->exec(source_->viewport()->mapToGlobal(point)) == live) {
            setSourceMode(false); preview_->setFocus();
        }
    });
    connect(tabs_, &QTabBar::currentChanged, this, [this] {
        updatePage(); emit previewModeChanged(isPreviewMode());
    });
    connect(source_, &QPlainTextEdit::textChanged, this, &HeadingTextEdit::textChanged);
    updatePage();
}

void HeadingTextEdit::setPlainText(const QString &text)
{
    TextLayoutGuard::setPlainText(source_, text);
    preview_->syncSource();
    if (preview_->isVisible() || isPreviewMode()) preview_->refresh();
    preview_->verticalScrollBar()->setValue(0);
}
QString HeadingTextEdit::toPlainText() const { return source_->toPlainText(); }
void HeadingTextEdit::setPlaceholderText(const QString &text) { source_->setPlaceholderText(text); preview_->setPlaceholderText(text); }
void HeadingTextEdit::clear() { setPlainText({}); setPreviewMode(false); }
void HeadingTextEdit::setPreviewMode(bool preview) { tabs_->setCurrentIndex(preview ? PreviewTab : EditTab); }
bool HeadingTextEdit::isPreviewMode() const { return tabs_->currentIndex() == PreviewTab; }
void HeadingTextEdit::setSourceMode(bool source) { if (sourceMode_ == source) return; sourceMode_ = source; updatePage(); }
bool HeadingTextEdit::isSourceMode() const { return sourceMode_ && !isPreviewMode(); }
void HeadingTextEdit::setModeControlsVisible(bool visible) { tabs_->setVisible(visible); setMinimumHeight(visible ? 170 : 80); }
void HeadingTextEdit::setContentLabel(const QString &label)
{
    setAccessibleName(label); source_->setAccessibleName(label + QStringLiteral("原文"));
    preview_->setAccessibleName(label + QStringLiteral("即时编辑与阅读"));
    tabs_->setAccessibleName(label + QStringLiteral("显示模式"));
}

void HeadingTextEdit::updatePage()
{
    if (isSourceMode() && pages_->currentWidget() == preview_) {
        preview_->refresh();
        const auto view = preview_->textCursor();
        auto cursor = source_->textCursor();
        cursor.setPosition(preview_->sourcePosition(view.anchor()));
        cursor.setPosition(preview_->sourcePosition(view.position()), QTextCursor::KeepAnchor);
        source_->setTextCursor(cursor);
    } else if (pages_->currentWidget() == source_) preview_->syncSource();
    preview_->setReading(isPreviewMode()); preview_->refresh();
    QWidget *page = isSourceMode() ? static_cast<QWidget *>(source_) : preview_;
    pages_->setCurrentWidget(page); setFocusProxy(page);
}

bool HeadingTextEdit::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == source_->viewport() || watched == preview_->viewport()) && event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (wheel->modifiers().testFlag(Qt::ControlModifier)) {
            zoomText(wheel->angleDelta().y() ? wheel->angleDelta().y() / 120.0 : wheel->pixelDelta().y() / 40.0);
            wheel->accept(); return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
void HeadingTextEdit::zoomText(qreal steps)
{
    if (qFuzzyIsNull(steps)) return;
    const auto font = source_->font();
    setTextPointSize((font.pointSizeF() > 0 ? font.pointSizeF() : QFontInfo(font).pointSizeF()) + steps);
}
void HeadingTextEdit::setTextPointSize(qreal size)
{
    if (!qIsFinite(size)) return;
    auto font = source_->font(); size = qBound(6.0, size, 72.0);
    if (qFuzzyCompare(font.pointSizeF(), size)) return;
    font.setPointSizeF(size); source_->setFont(font); preview_->setBaseFont(font);
    emit textPointSizeChanged(size);
}

QString HeadingTextEdit::getText(QWidget *parent, const QString &title, const QString &initial, bool *accepted, bool previewFirst)
{
    QDialog dialog(parent); dialog.setWindowTitle(title); dialog.setWindowFlag(Qt::WindowMaximizeButtonHint);
    dialog.setMinimumSize(480, 340); dialog.resize(640, 460);
    auto *layout = new QVBoxLayout(&dialog);
    auto *editor = new HeadingTextEdit(&dialog); editor->setPlainText(initial); editor->setPreviewMode(previewFirst);
    layout->addWidget(editor, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    auto *save = buttons->button(QDialogButtonBox::Save); save->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    auto validate = [editor, save, initial, previewFirst] {
        save->setEnabled(!editor->toPlainText().trimmed().isEmpty() && (!previewFirst || editor->toPlainText() != initial));
    };
    connect(editor, &HeadingTextEdit::textChanged, &dialog, validate); validate();
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons); editor->setFocus();
    const bool ok = dialog.exec() == QDialog::Accepted; if (accepted) *accepted = ok;
    return ok ? editor->toPlainText() : initial;
}
