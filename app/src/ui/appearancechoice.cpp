#include "theme.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDate>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>

namespace {
// Paint the type sample and a miniature calendar from the same live theme as the controls.
class AppearancePreview final : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF bounds = QRectF(rect()).adjusted(.5, .5, -.5, -.5);
        QPainterPath panel;
        panel.addRoundedRect(bounds, 10, 10);
        QLinearGradient ground(bounds.topLeft(), bounds.bottomRight());
        ground.setColorAt(0, UiTheme::IndexSurface);
        ground.setColorAt(1, UiTheme::Selection);
        p.fillPath(panel, ground);
        p.setPen(QPen(UiTheme::Border, 1));
        p.drawPath(panel);
        p.setClipPath(panel);

        const qreal scale = qMin(width() / 402.0, height() / 194.0);
        p.translate(width() * .5, height() * .5);
        p.scale(scale, scale);
        p.translate(-201, -97);
        auto font = this->font();
        font.setPixelSize(12);
        p.setFont(font);
        p.setPen(UiTheme::Muted);
        p.drawText(QRectF(20, 36, 128, 20), Qt::AlignVCenter, QStringLiteral("效果预览"));
        font.setPixelSize(49);
        font.setWeight(QFont::DemiBold);
        p.setFont(font);
        p.setPen(UiTheme::Text);
        p.drawText(QRectF(18, 54, 133, 66), Qt::AlignVCenter, QStringLiteral("Aa 字"));
        qreal x = 29;
        for (const auto &color : {UiTheme::Sidebar, UiTheme::Surface, UiTheme::Text, UiTheme::Accent}) {
            p.setPen(QPen(UiTheme::Border, 1));
            p.setBrush(color);
            p.drawEllipse(QPointF(x, 145), 9, 9);
            x += 26;
        }

        const QRectF window(158, 15, 226, 164);
        QPainterPath frame;
        frame.addRoundedRect(window, 7, 7);
        p.fillPath(frame, UiTheme::Surface);
        p.save();
        p.setClipPath(frame, Qt::IntersectClip);
        p.fillRect(QRectF(window.topLeft(), QSizeF(36, window.height())), UiTheme::Sidebar);
        p.setPen(Qt::NoPen);
        const int pitch = UiTheme::isCompact() ? 14 : 19;
        for (int i = 0; i < 4; ++i) {
            p.setBrush(i == 0 ? UiTheme::Accent : UiTheme::Muted);
            p.drawRoundedRect(QRectF(168, 34 + i * pitch, i == 0 ? 17 : 11, 3), 1.5, 1.5);
        }
        p.restore();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(UiTheme::ControlBorder, .8));
        p.drawPath(frame);

        const auto today = QDate::currentDate();
        const QDate first(today.year(), today.month(), 1);
        font.setPixelSize(15);
        p.setFont(font);
        p.setPen(UiTheme::Text);
        p.drawText(QRectF(206, 22, 165, 24), Qt::AlignVCenter, today.toString(QStringLiteral("yyyy 年 M 月")));
        font.setPixelSize(10);
        font.setWeight(QFont::Normal);
        p.setFont(font);
        p.setPen(UiTheme::Muted);
        const QString days = QStringLiteral("一二三四五六日");
        for (int column = 0; column < days.size(); ++column)
            p.drawText(QRectF(204 + column * 24, 49, 21, 18), Qt::AlignCenter, QString(days.at(column)));
        const int rowHeight = UiTheme::isCompact() ? 15 : 18;
        for (int day = 1; day <= first.daysInMonth(); ++day) {
            const int cell = first.dayOfWeek() - 1 + day - 1;
            const QRectF box(204 + (cell % 7) * 24, 67 + (cell / 7) * rowHeight, 21, rowHeight);
            if (day == today.day()) {
                p.setPen(Qt::NoPen);
                p.setBrush(UiTheme::Selection);
                p.drawRoundedRect(box, 4, 4);
            }
            p.setPen(day == today.day() ? UiTheme::SelectionText : UiTheme::Muted);
            p.drawText(box, Qt::AlignCenter, QString::number(day));
        }
    }
};

class AppearanceOptionDelegate final : public QStyledItemDelegate
{
public:
    explicit AppearanceOptionDelegate(QComboBox *choice) : QStyledItemDelegate(choice), choice_(choice) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool chosen = index.row() == choice_->currentIndex();
        QStyleOptionViewItem row(option);
        // Qt moves the list selection as the user browses. The check follows the applied value.
        const bool browsing = row.state.testFlag(QStyle::State_Selected) || row.state.testFlag(QStyle::State_MouseOver);
        row.state.setFlag(QStyle::State_MouseOver, browsing && !chosen);
        row.state.setFlag(QStyle::State_Selected, chosen);
        row.state.setFlag(QStyle::State_HasFocus, false);
        QStyledItemDelegate::paint(painter, row, index);
        if (!chosen) return;

        const QRect mark = QStyle::alignedRect(option.direction, Qt::AlignTrailing | Qt::AlignVCenter,
                                                QSize(14, 14), option.rect.adjusted(7, 0, -7, 0));
        painter->save();
        painter->translate(mark.topLeft());
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(UiTheme::SelectionText, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPolyline(QPolygonF{{2, 7}, {5.5, 10.5}, {12, 4}});
        painter->restore();
    }

private:
    QComboBox *choice_;
};
}

QWidget *UiTheme::createAppearancePreview(QWidget *parent)
{
    auto *preview = new AppearancePreview(parent);
    preview->setObjectName(QStringLiteral("appearancePreview"));
    preview->setAccessibleName(QStringLiteral("当前风格和布局的字样、配色与日历预览"));
    preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return preview;
}

void UiTheme::styleAppearanceChoice(QComboBox *choice)
{
    setRole(choice, "appearanceChoice");
    choice->setIconSize(QSize(22, 16));
    choice->setCursor(Qt::PointingHandCursor);
    auto *list = choice->view();
    auto *popup = list->window();
    popup->setWindowFlags(popup->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    popup->setAttribute(Qt::WA_TranslucentBackground);
    setRole(popup, "appearancePopup");
    popup->setContentsMargins(0, 5, 0, 5);
    setRole(list, "appearanceOptions");
    choice->setItemDelegate(new AppearanceOptionDelegate(choice));
}
