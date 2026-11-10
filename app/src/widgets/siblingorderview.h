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
#include <QTreeWidgetItemIterator>
#include <functional>
#include <type_traits>

// A scope identifies reorderable siblings. Container transfers are validated
// separately by the page, which owns the destination's domain rules.
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
    std::function<bool(Item *)> isContainer;
    std::function<bool(const QList<Item *> &, Item *)> canTransfer;
    // May rebuild the view. Never use source/destination pointers after calling it.
    std::function<bool(const QList<Item *> &, Item *)> saveTransfer;
    std::function<void(bool)> dragActivityChanged;
    bool orderingEnabled = true;
    bool transferEnabled = true;

    bool isDragging() const { return dragging_; }
    bool canTransferSelectionTo(Item *destination) const
    {
        const auto items = transferItems();
        return transferEnabled && canTransfer && saveTransfer && !items.isEmpty()
            && canTransfer(items, destination);
    }
    bool transferSelectionTo(Item *destination)
    {
        if (!canTransferSelectionTo(destination)) return false;
        const auto items = transferItems();
        clearIndicator();
        return saveTransfer(items, destination);
    }
    bool dropSelectionAt(const QPoint &position)
    {
        const auto destination = transferTarget(position);
        clearIndicator();
        if (destination.valid && canTransferSelectionTo(destination.item))
            return transferSelectionTo(destination.item);
        const QModelIndex from = dragging_ ? QModelIndex(source_) : this->currentIndex().siblingAtColumn(0);
        const auto target = dropTarget(position, from);
        return compatible(from, target)
            && moveItem(from, target, position.y() >= this->visualRect(target).center().y());
    }

protected:
    void startDrag(Qt::DropActions) override
    {
        const auto index = this->currentIndex().siblingAtColumn(0);
        if (!index.isValid() || scope(index).isEmpty()
            || ((!orderingEnabled || !saveOrder) && (!transferEnabled || !saveTransfer))) return;
        source_ = index;
        sources_.clear();
        for (auto *item : transferItems()) sources_.append(this->indexFromItem(item));
        dragging_ = true;
        if (dragActivityChanged) dragActivityChanged(true);
        QDrag drag(this);
        drag.setMimeData(this->model()->mimeData({index}));
        const QRect rect = this->visualRect(index);
        drag.setPixmap(this->viewport()->grab(rect));
        drag.setHotSpot(QPoint(12, rect.height() / 2));
        // Do not use Base::startDrag: it may remove the source after an accepted
        // move even though our drop handler has already persisted and moved it.
        drag.exec(Qt::MoveAction);
        dragging_ = false;
        source_ = QPersistentModelIndex();
        sources_.clear();
        clearIndicator();
        if (dragActivityChanged) dragActivityChanged(false);
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->source() == this && source_.isValid() && (orderingEnabled || transferEnabled)) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
        } else event->ignore();
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        const auto position = event->position().toPoint();
        const auto destination = transferTarget(position);
        const bool transferring = canTransferSelectionTo(destination.item) && destination.valid;
        const auto target = dropTarget(position, source_);
        if (event->source() != this || (!transferring && !compatible(source_, target))) {
            clearIndicator();
            event->ignore();
            return;
        }
        // Preserve Qt's edge auto-scroll; our own paint distinguishes a
        // destination container from a sibling insertion position.
        Base::dragMoveEvent(event);
        clearIndicator();
        if (transferring) {
            containerIndicator_ = destination.item
                ? this->visualRect(this->indexFromItem(destination.item))
                : this->viewport()->rect().adjusted(2, 2, -3, -3);
        } else {
            const QRect rect = this->visualRect(target);
            indicatorY_ = event->position().y() < rect.center().y() ? rect.top() : subtreeBottom(target);
            indicatorX_ = rect.left();
        }
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
        clearIndicator();
        const bool saved = event->source() == this && dropSelectionAt(event->position().toPoint());
        if (saved) {
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
        if (indicatorY_ < 0 && containerIndicator_.isEmpty()) return;
        QPainter painter(this->viewport());
        painter.setPen(QPen(QColor(QStringLiteral("#2563eb")), 2));
        if (!containerIndicator_.isEmpty()) {
            painter.fillRect(containerIndicator_, QColor(37, 99, 235, 24));
            painter.drawRect(containerIndicator_.adjusted(1, 1, -1, -1));
            return;
        }
        const int left = qMax(4, indicatorX_);
        painter.drawLine(left, indicatorY_, this->viewport()->width() - 6, indicatorY_);
        painter.drawLine(left, indicatorY_ - 3, left, indicatorY_ + 3);
    }

private:
    QList<Item *> transferItems() const
    {
        QList<Item *> result;
        if (dragging_) {
            for (const auto &index : sources_) {
                if (!index.isValid()) return {}; // A refresh invalidates the whole drag.
                result.append(this->itemFromIndex(index));
            }
        } else if constexpr (std::is_same_v<Base, QTreeWidget>) {
            for (QTreeWidgetItemIterator it(const_cast<SiblingOrderView *>(this)); *it; ++it) {
                auto *item = *it;
                if (!item->isSelected() || item->isHidden()) continue;
                bool visible = true;
                for (auto *parent = item->parent(); parent; parent = parent->parent())
                    if (parent->isHidden() || !parent->isExpanded()) visible = false;
                if (visible) result.append(item);
            }
        } else result = this->selectedItems();
        return result;
    }
    struct TransferTarget { Item *item = nullptr; bool valid = false; };
    TransferTarget transferTarget(const QPoint &position) const
    {
        if constexpr (std::is_same_v<Base, QTreeWidget>) {
            if (!transferEnabled || !isContainer) return {};
            auto *item = this->itemAt(position);
            if (!item) return {nullptr, true}; // Empty space is the current root.
            if (isContainer(item)) return {item, true};
            // A leaf in another folder is also a destination for that folder.
            return {item->parent(), true};
        }
        return {};
    }
    QString scope(const QModelIndex &index) const { return index.data(SiblingOrderScopeRole).toString(); }

    QModelIndex dropTarget(const QPoint &position, const QModelIndex &from) const
    {
        auto target = this->indexAt(position).siblingAtColumn(0);
        // For group sorting, descendants belong to their group's drop area.
        while (target.isValid() && target.parent() != from.parent()) target = target.parent();
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
        return orderingEnabled && saveOrder && transferItems().size() == 1 && from.isValid() && target.isValid()
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

    void clearIndicator() { indicatorY_ = -1; containerIndicator_ = {}; this->viewport()->update(); }
    QPersistentModelIndex source_;
    QList<QPersistentModelIndex> sources_;
    bool dragging_ = false;
    QRect containerIndicator_;
    int indicatorY_ = -1;
    int indicatorX_ = 0;
};

using SiblingOrderTree = SiblingOrderView<QTreeWidget, QTreeWidgetItem>;
using SiblingOrderList = SiblingOrderView<QListWidget, QListWidgetItem>;
