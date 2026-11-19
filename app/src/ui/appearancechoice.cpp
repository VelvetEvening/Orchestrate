#include "theme.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QPainter>
#include <QStyledItemDelegate>

namespace {
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
        painter->setPen(QPen(QColor(UiTheme::Accent), 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPolyline(QPolygonF{{2, 7}, {5.5, 10.5}, {12, 4}});
        painter->restore();
    }

private:
    QComboBox *choice_;
};
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
