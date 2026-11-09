#pragma once

#include "treeaccessibility.h"
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QSignalBlocker>
#include <functional>
#include <type_traits>

// A scope identifies reorderable siblings, not a destination container. Empty
// scopes and filtered views cannot be reordered.
constexpr int SiblingOrderScopeRole = Qt::UserRole + 100;

template<class Base, class Item>
class SiblingOrderView : public Base
{
public:
    explicit SiblingOrderView(QWidget *parent = nullptr) : Base(parent)
    {
        this->setSelectionMode(QAbstractItemView::SingleSelection);
        this->setDragEnabled(true);
        this->setAcceptDrops(true);
        this->setDragDropMode(QAbstractItemView::InternalMove);
        this->setDefaultDropAction(Qt::MoveAction);
        this->setDropIndicatorShown(false);
        this->setAutoScroll(true);
        this->setAutoScrollMargin(28);
        if constexpr (std::is_same_v<Base, QTreeWidget>) this->setAutoExpandDelay(-1);
    }

    // Called before changing the view. A failed save leaves selection and order intact.
    std::function<bool(Item *, const QList<int> &)> saveOrder;
    bool orderingEnabled = true;

protected:
    void startDrag(Qt::DropActions) override
    {
        const auto index = this->currentIndex().siblingAtColumn(0);
        if (!orderingEnabled || !index.isValid() || scope(index).isEmpty() || !saveOrder) return;
        source_ = index;
        QDrag drag(this);
        drag.setMimeData(this->model()->mimeData({index}));
        const QRect rect = this->visualRect(index);
        drag.setPixmap(this->viewport()->grab(rect));
        drag.setHotSpot(QPoint(12, rect.height() / 2));
        // Do not use Base::startDrag: it may remove the source after an accepted
        // move even though our drop handler has already persisted and moved it.
        drag.exec(Qt::MoveAction);
        source_ = QPersistentModelIndex();
        clearIndicator();
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->source() == this && source_.isValid() && orderingEnabled) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
        } else event->ignore();
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        const auto target = dropTarget(event->position().toPoint());
        if (event->source() != this || !compatible(source_, target)) {
            clearIndicator();
            event->ignore();
            return;
        }
        // Preserve Qt's edge auto-scroll, but use an insertion line rather than
        // its tree "drop into item" affordance: this operation never reparents.
        Base::dragMoveEvent(event);
        const QRect rect = this->visualRect(target);
        indicatorY_ = event->position().y() < rect.center().y() ? rect.top() : subtreeBottom(target);
        indicatorX_ = rect.left();
        this->viewport()->update();
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }

    void dragLeaveEvent(QDragLeaveEvent *event) override
    {
        clearIndicator();
        Base::dragLeaveEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        const auto target = dropTarget(event->position().toPoint());
        clearIndicator();
        if (event->source() == this && compatible(source_, target)
            && moveItem(source_, target, event->position().y() >= this->visualRect(target).center().y())) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
        } else event->ignore();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->modifiers() == Qt::ControlModifier
            && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
            const auto from = this->currentIndex().siblingAtColumn(0);
            const bool down = event->key() == Qt::Key_Down;
            const auto target = this->model()->index(from.row() + (down ? 1 : -1), 0, from.parent());
            if (compatible(from, target)) moveItem(from, target, down);
            event->accept();
            return;
        }
        Base::keyPressEvent(event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        Base::paintEvent(event);
        if (indicatorY_ < 0) return;
        QPainter painter(this->viewport());
        painter.setPen(QPen(QColor(QStringLiteral("#2563eb")), 2));
        const int left = qMax(4, indicatorX_);
        painter.drawLine(left, indicatorY_, this->viewport()->width() - 6, indicatorY_);
        painter.drawLine(left, indicatorY_ - 3, left, indicatorY_ + 3);
    }

private:
    QString scope(const QModelIndex &index) const { return index.data(SiblingOrderScopeRole).toString(); }

    QModelIndex dropTarget(const QPoint &position) const
    {
        auto target = this->indexAt(position).siblingAtColumn(0);
        // An expanded group's children belong to that group's drop area. Walk
        // up to the source level; tools/records still cannot change containers.
        while (target.isValid() && target.parent() != source_.parent()) target = target.parent();
        return target;
    }

    int subtreeBottom(const QModelIndex &target) const
    {
        auto last = target;
        if constexpr (std::is_same_v<Base, QTreeWidget>) {
            for (auto next = this->indexBelow(last); next.isValid(); next = this->indexBelow(last)) {
                auto ancestor = next.parent();
                while (ancestor.isValid() && ancestor != target) ancestor = ancestor.parent();
                if (!ancestor.isValid()) break;
                last = next;
            }
        }
        return this->visualRect(last).bottom() + 1;
    }

    bool compatible(const QModelIndex &from, const QModelIndex &target) const
    {
        return orderingEnabled && saveOrder && from.isValid() && target.isValid()
            && from.parent() == target.parent() && !scope(from).isEmpty() && scope(from) == scope(target);
    }

    bool moveItem(const QModelIndex &from, const QModelIndex &target, bool after)
    {
        if (!compatible(from, target)) return false;
        int destination = target.row() + (after ? 1 : 0);
        if (from.row() < destination) --destination;
        if (from.row() == destination) return true;
        QList<int> ids;
        int sourcePosition = -1;
        int targetPosition = -1;
        for (int row = 0; row < this->model()->rowCount(from.parent()); ++row) {
            const auto index = this->model()->index(row, 0, from.parent());
            if (scope(index) != scope(from)) continue;
            if (row == from.row()) sourcePosition = ids.size();
            if (row == target.row()) targetPosition = ids.size();
            ids.append(index.data(Qt::UserRole).toInt());
        }
        int insertion = targetPosition + (after ? 1 : 0);
        if (sourcePosition < insertion) --insertion;
        ids.move(sourcePosition, insertion);
        Item *item = this->itemFromIndex(from);
        if (!saveOrder(item, ids)) return false;
        const QSignalBlocker blocker(this);
        if constexpr (std::is_same_v<Base, QTreeWidget>) {
            const bool expanded = item->isExpanded();
            if (auto *parent = item->parent()) {
                parent->takeChild(from.row());
                parent->insertChild(destination, item);
            } else {
                this->takeTopLevelItem(from.row());
                this->insertTopLevelItem(destination, item);
            }
            item->setExpanded(expanded);
        } else {
            this->takeItem(from.row());
            this->insertItem(destination, item);
        }
        this->setCurrentItem(item);
        this->scrollToItem(item);
        this->doItemsLayout();
        if constexpr (std::is_same_v<Base, QTreeWidget>) resetTreeAccessibility(this);
        else {
            QAccessibleTableModelChangeEvent changed(this, QAccessibleTableModelChangeEvent::ModelReset);
            QAccessible::updateAccessibility(&changed);
        }
        return true;
    }

    void clearIndicator() { indicatorY_ = -1; this->viewport()->update(); }
    QPersistentModelIndex source_;
    int indicatorY_ = -1;
    int indicatorX_ = 0;
};

using SiblingOrderTree = SiblingOrderView<QTreeWidget, QTreeWidgetItem>;
using SiblingOrderList = SiblingOrderView<QListWidget, QListWidgetItem>;
