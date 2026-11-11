#pragma once

#include "theme.h"
#include <QEvent>
#include <QFrame>
#include <QPainter>
#include <QPointer>

// Continue the visible workspace surfaces through the native caption area.
// Geometry notifications repaint only the header; no polling or layout changes.
class WorkspaceHeader final : public QFrame
{
public:
    using QFrame::QFrame;

    void setSurfaces(QWidget *sidebar, QWidget *index)
    {
        if (sidebar_) sidebar_->removeEventFilter(this);
        if (index_) index_->removeEventFilter(this);
        sidebar_ = sidebar;
        index_ = index;
        if (sidebar_) sidebar_->installEventFilter(this);
        if (index_) index_->installEventFilter(this);
        update();
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
        case QEvent::Hide:
            update();
            break;
        default:
            break;
        }
        return QFrame::eventFilter(object, event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(UiTheme::Surface));
        const auto paintSurface = [&](QWidget *surface, const char *color) {
            if (!surface || !surface->isVisible()) return;
            // QWidget::mapTo requires an ancestor. The body and caption are
            // siblings, so translate through their shared top-level window.
            const int x = surface->mapTo(window(), QPoint()).x() - mapTo(window(), QPoint()).x();
            painter.fillRect(QRect(x, 0, surface->width(), height()), QColor(color));
        };
        paintSurface(sidebar_, UiTheme::Sidebar);
        paintSurface(index_, UiTheme::IndexSurface);
    }

private:
    QPointer<QWidget> sidebar_;
    QPointer<QWidget> index_;
};
