#pragma once
#include "ui/theme.h"
#include <QCalendarWidget>
#include <QPainter>
#include <QSet>

// Paint date cells independently of the native style's inactive selection.
// QCalendarWidget still owns navigation, keyboard input and accessibility.
class WorkspaceCalendar : public QCalendarWidget
{
public:
    explicit WorkspaceCalendar(QWidget *parent = nullptr) : QCalendarWidget(parent)
    {
        setGridVisible(false);
        setToolTip(QStringLiteral("日期下方：圆角短线表示日记，空心圆环表示提醒；并列表示当天两者都有。"));
        QFont dateFont = font();
        dateFont.setPointSize(11);
        setFont(dateFont);
        UiTheme::applyCalendarPalette(this);
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
        const bool compact = UiTheme::isCompact();
        const int side = qMax(0, qMin(compact ? 36 : 44, qMin(rect.width(), rect.height()) - 8));
        const QRectF tile(rect.center().x() - side / 2.0, rect.center().y() - side / 2.0, side, side);
        painter->setPen(Qt::NoPen);
        if (selected || today) {
            painter->setBrush(QColor(selected ? UiTheme::Selection : UiTheme::IndexSurface));
            painter->setPen(QPen(QColor(today ? UiTheme::Focus : UiTheme::Selection), 1));
            painter->drawRoundedRect(tile, compact ? 5 : 10, compact ? 5 : 10);
            foreground = QColor(UiTheme::Accent);
        }
        QFont textFont = font();
        textFont.setBold(selected || today);
        painter->setFont(textFont);
        painter->setPen(foreground);
        painter->drawText(rect.adjusted(0,0,0,-6), Qt::AlignCenter, QString::number(date.day()));
        const qreal markerY = tile.bottom() - 6;
        const bool hasDiary = diaryDates_.contains(date);
        const bool hasReminder = reminderDates_.contains(date);
        const qreal centerX = rect.center().x();
        // Shape, as well as color, distinguishes the two record types.
        if (hasDiary) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(inMonth ? UiTheme::DiaryMarker : UiTheme::Disabled));
            painter->drawRoundedRect(QRectF(centerX + (hasReminder ? -5 : 0) - 4, markerY - 2, 8, 4), 2, 2);
        }
        if (hasReminder) {
            painter->setPen(QPen(QColor(inMonth ? UiTheme::ReminderMarker : UiTheme::Disabled), 1.2));
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(QRectF(centerX + (hasDiary ? 6 : 0) - 2.4, markerY - 2.4, 4.8, 4.8));
        }
        painter->restore();
    }
private:
    QSet<QDate> diaryDates_;
    QSet<QDate> reminderDates_;
};
