#include "headingtext.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QEvent>
#include <QListWidget>
#include <QPainter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
#include <QtMath>

namespace {
int headingLevel(const QString &line, int *contentStart)
{
    int hashes = 0;
    while (hashes < line.size() && line.at(hashes) == QLatin1Char('#')) ++hashes;
    if (hashes < 1 || hashes > 3 || hashes >= line.size() || line.at(hashes) != QLatin1Char(' ')) return 0;
    int start = hashes;
    while (start < line.size() && line.at(start) == QLatin1Char(' ')) ++start;
    if (line.mid(start).trimmed().isEmpty()) return 0;
    *contentStart = start;
    return hashes;
}

class HeadingDelegate final : public QStyledItemDelegate
{
public:
    explicit HeadingDelegate(QListWidget *list) : QStyledItemDelegate(list), list_(list)
    {
        list_->viewport()->installEventFilter(this);
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        QTextDocument document;
        prepare(document, opt, index.data(Qt::DisplayRole).toString(), list_->viewport()->width() - 2 * paddingX);
        return QSize(0, qCeil(document.size().height()) + 2 * paddingY);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        QTextDocument document;
        prepare(document, opt, index.data(Qt::DisplayRole).toString(), opt.rect.width() - 2 * paddingX);
        // Let the native style paint selection, alternating rows and focus.
        opt.text.clear();
        const QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
        QAbstractTextDocumentLayout::PaintContext context;
        context.palette = opt.palette;
        if (opt.state & QStyle::State_Selected) {
            context.palette.setColor(QPalette::Text, opt.palette.color(QPalette::HighlightedText));
        }
        painter->save();
        painter->setClipRect(opt.rect);
        painter->translate(opt.rect.topLeft() + QPoint(paddingX, paddingY));
        document.documentLayout()->draw(painter, context);
        painter->restore();
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == list_->viewport() && event->type() == QEvent::Resize && !layoutPending_) {
            layoutPending_ = true;
            QTimer::singleShot(0, this, [this] {
                layoutPending_ = false;
                list_->doItemsLayout();
            });
        }
        return QStyledItemDelegate::eventFilter(object, event);
    }

private:
    static void prepare(QTextDocument &document, const QStyleOptionViewItem &option,
                        const QString &source, int width)
    {
        document.setDefaultFont(option.font);
        // initStyleOption replaces newlines with display separators. Read the
        // model's original text, or only the first heading would be recognized.
        HeadingText::render(document, source);
        document.setTextWidth(qMax(24, width));
    }
    static constexpr int paddingX = 10;
    static constexpr int paddingY = 8;
    QListWidget *list_;
    bool layoutPending_ = false;
};
} // namespace

void HeadingText::render(QTextDocument &document, const QString &source)
{
    document.clear();
    document.setDocumentMargin(0);
    QTextOption option = document.defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document.setDefaultTextOption(option);
    QString normalized = source;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = normalized.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    QTextCursor cursor(&document);
    const QFont base = document.defaultFont();
    const qreal baseSize = base.pointSizeF() > 0 ? base.pointSizeF() : 10.0;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        int start = 0;
        const int level = headingLevel(line, &start);
        QTextBlockFormat block;
        QTextCharFormat characters;
        characters.setFont(base);
        if (level > 0) {
            constexpr qreal scales[] {1.0, 1.6, 1.35, 1.15};
            block.setHeadingLevel(level);
            block.setTopMargin(i == 0 ? 0 : (level == 1 ? 12 : 8));
            block.setBottomMargin(4);
            characters.setFontPointSize(baseSize * scales[level]);
            characters.setFontWeight(QFont::Bold);
        }
        if (i > 0) cursor.insertBlock(block, characters);
        else {
            cursor.setBlockFormat(block);
            cursor.setCharFormat(characters);
        }
        // Inserting text rather than parsing HTML prevents links, images, HTML
        // or other Markdown syntax from becoming active content.
        cursor.insertText(level > 0 ? line.mid(start) : line, characters);
    }
}

QString HeadingText::toolTipHtml(const QString &source)
{
    if (source.isEmpty()) return {};
    QTextDocument document;
    document.setDefaultFont(QApplication::font());
    render(document, source);
    return document.toHtml();
}

void HeadingText::configureList(QListWidget *list)
{
    list->setItemDelegate(new HeadingDelegate(list));
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setUniformItemSizes(false);
    list->setResizeMode(QListView::Adjust);
}
