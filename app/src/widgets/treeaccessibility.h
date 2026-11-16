#pragma once

#include <QAccessible>
#include <QTreeWidget>

// Qt 6.11.2 caches accessible tree cells by visible row. Collapsing a branch
// removes view rows without invalidating that cache, so child(i) and
// indexOfChild(child(i)) can disagree. Windows UIA sibling navigation can then
// cycle indefinitely. Reset synchronously, before another client query.
inline void resetTreeAccessibility(QTreeView *tree)
{
    QAccessibleTableModelChangeEvent event(tree, QAccessibleTableModelChangeEvent::ModelReset);
    if (QAccessible::isActive()) {
        QAccessible::updateAccessibility(&event);
    } else if (auto *accessible = QAccessible::queryAccessibleInterface(tree)) {
        // A client may have queried cells before accessibility becomes active.
        if (auto *table = accessible->tableInterface()) table->modelChange(&event);
    }
}

inline void synchronizeTreeAccessibility(QTreeWidget *tree)
{
    const auto reset = [tree] { resetTreeAccessibility(tree); };
    // QTreeWidget forwards the view signals to item signals first. Register
    // before application handlers, which may cause reentrant UIA queries.
    QObject::connect(tree, &QTreeWidget::itemCollapsed, tree, reset);
    QObject::connect(tree, &QTreeWidget::itemExpanded, tree, reset);
}
