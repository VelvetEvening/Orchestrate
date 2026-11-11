#pragma once
#include "ui/theme.h"
#include <QCalendarWidget>
#include <QPainter>
#include <QSet>
#include <QTextCharFormat>

// Paint date cells independently of the native style's inactive selection.
// QCalendarWidget still owns navigation, keyboard input and accessibility.
class WorkspaceCalendar : public QCalendarWidget
{
public:
    explicit WorkspaceCalendar(QWidget *parent = nullptr) : QCalendarWidget(parent)
    {
        setGridVisible(false);
        QFont dateFont = font();
        dateFont.setPointSize(11);
        setFont(dateFont);
        QTextCharFormat header;
        header.setForeground(QColor(UiTheme::Muted));
        header.setBackground(QColor(UiTheme::IndexSurface));
        setHeaderTextFormat(header);
        setWeekdayTextFormat(Qt::Saturday, header);
        setWeekdayTextFormat(Qt::Sunday, header);

    }
    void setRecordDates(const QSet<QDate> &diaries, const QSet<QDate> &reminders)
    {
        diaryDates_ = diaries;
        reminderDates_ = reminders;
        updateCells();
    }
protected:
    void paintCell(QPainter *painter, const QRect &rect, QDate date) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->fillRect(rect, QColor(UiTheme::IndexSurface));
        const bool selected = date == selectedDate();
        const bool inMonth = date.month() == monthShown() && date.year() == yearShown();
        const bool today = inMonth && date == QDate::currentDate();
        const bool future = inMonth && date > QDate::currentDate();
        QColor foreground(inMonth ? (future ? UiTheme::Muted : UiTheme::Text) : UiTheme::Disabled);
        const int side = qMax(0, qMin(44, qMin(rect.width(), rect.height()) - 8));
        const QRectF tile(rect.center().x() - side / 2.0, rect.center().y() - side / 2.0, side, side);
        painter->setPen(Qt::NoPen);
        if (selected || today) {
            painter->setBrush(QColor(selected ? UiTheme::Selection : UiTheme::IndexSurface));
            painter->setPen(QPen(QColor(today ? UiTheme::Focus : UiTheme::Selection), 1));
            painter->drawRoundedRect(tile, 10, 10);
            foreground = QColor(UiTheme::Accent);
        }
        QFont textFont = font();
        textFont.setBold(selected || today);
        painter->setFont(textFont);
        painter->setPen(foreground);
        painter->drawText(rect.adjusted(0,0,0,-6), Qt::AlignCenter, QString::number(date.day()));
        const qreal markerY = tile.bottom() - 6;
        if (diaryDates_.contains(date)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(!inMonth ? UiTheme::Disabled : UiTheme::Success));
            painter->drawRoundedRect(QRectF(rect.center().x()-9,markerY-3,6,6),1,1);
        }
        if (reminderDates_.contains(date)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(!inMonth ? UiTheme::Disabled : UiTheme::Warning));
            QPolygonF triangle;
            triangle << QPointF(rect.center().x()+6,markerY-3.5)
                     << QPointF(rect.center().x()+2.5,markerY+3)
                     << QPointF(rect.center().x()+9.5,markerY+3);
            painter->drawPolygon(triangle);
        }
        painter->restore();
    }
private:
    QSet<QDate> diaryDates_;
    QSet<QDate> reminderDates_;
};
