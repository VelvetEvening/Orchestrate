#pragma once

#include "ui/theme.h"
#include <QPainter>
#include <QSplitter>

// Extend both neighboring surfaces through the native drag target so their
// color transition coincides with the single visible divider.
class WorkspaceSplitter final : public QSplitter
{
public:
    explicit WorkspaceSplitter(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSplitter(orientation, parent)
    {
        setObjectName(QStringLiteral("workspaceSplit"));
        setChildrenCollapsible(false);
        setHandleWidth(7);
    }

protected:
    QSplitterHandle *createHandle() override { return new Handle(orientation(), this); }

private:
    class Handle final : public QSplitterHandle
    {
    public:
        Handle(Qt::Orientation orientation, QSplitter *parent)
            : QSplitterHandle(orientation, parent) {}

    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter painter(this);
            const bool horizontal = orientation() == Qt::Horizontal;
            QColor leading(horizontal ? UiTheme::IndexSurface : UiTheme::Surface);
            QColor trailing(horizontal ? UiTheme::Surface : UiTheme::IndexSurface);
            if (horizontal && layoutDirection() == Qt::RightToLeft)
                qSwap(leading, trailing);
            const QRect line = horizontal
                ? QRect(width() / 2, 0, 1, height())
                : QRect(0, height() / 2, width(), 1);
            painter.fillRect(rect(), trailing);
            painter.fillRect(horizontal ? QRect(0, 0, line.x(), height())
                                        : QRect(0, 0, width(), line.y()), leading);
            painter.fillRect(line, QColor(underMouse() ? UiTheme::Focus : UiTheme::Border));
        }
        void enterEvent(QEnterEvent *event) override
        {
            QSplitterHandle::enterEvent(event);
            update();
        }
        void leaveEvent(QEvent *event) override
        {
            QSplitterHandle::leaveEvent(event);
            update();
        }
    };
};
