#include "ui/navigationsidebar.h"

#include "ui/motion.h"
#include "ui/theme.h"

#include <QEnterEvent>
#include <QPainter>
#include <QPushButton>
#include <QStyleOptionButton>
#include <QStylePainter>
#include <QVBoxLayout>

namespace {

constexpr int ExpandedSidebarWidth = 168;
constexpr int CollapsedSidebarWidth = 72;
constexpr int SidebarMotionDuration = 225;

qreal sidebarExpansion(int width)
{
    return qBound(0.0, qreal(width - CollapsedSidebarWidth) / (ExpandedSidebarWidth - CollapsedSidebarWidth), 1.0);
}

class NavigationButton final : public QPushButton
{
public:
    NavigationButton(const QString &label, QWidget *parent)
        : QPushButton(label, parent), label_(label)
    {
        // The sidebar controls the available width, including the icon-only state.
        setSizePolicy(QSizePolicy::Ignored, sizePolicy().verticalPolicy());
        hover_.setDuration(125);
        press_.setDuration(80);
        for (auto *animation : {&hover_, &press_}) {
            animation->setEasingCurve(QEasingCurve::OutCubic);
            animation->setStartValue(0.0);
            animation->setEndValue(0.0);
            connect(animation, &QVariantAnimation::valueChanged, this, [this] { update(); });
        }
        connect(this, &QPushButton::pressed, this, [this] { animateFeedback(press_, true); });
        connect(this, &QPushButton::released, this, [this] { animateFeedback(press_, false); });
        connect(&UiMotion::instance(), &UiMotion::enabledChanged, this, [this](bool enabled) {
            if (!enabled) {
                animateFeedback(hover_, underMouse());
                animateFeedback(press_, isDown());
            }
            update();
        });
    }

    void setCollapsed(bool collapsed)
    {
        setText(collapsed ? QString() : label_);
    }

    void setLabel(const QString &label, bool collapsed)
    {
        label_ = label;
        setCollapsed(collapsed);
        update();
    }

protected:
    // The checked state follows the page after its leave guard has accepted navigation.
    void nextCheckState() override {}

    void enterEvent(QEnterEvent *event) override
    {
        QPushButton::enterEvent(event);
        animateFeedback(hover_, true);
    }

    void leaveEvent(QEvent *event) override
    {
        QPushButton::leaveEvent(event);
        animateFeedback(hover_, false);
    }

    void paintEvent(QPaintEvent *) override
    {
        QStyleOptionButton option;
        initStyleOption(&option);
        const QRect contents = style()->subElementRect(QStyle::SE_PushButtonContents, &option, this);
        QStylePainter painter(this);
        paintBevel(painter, option);

        const int sidebarWidth = parentWidget()->width();
        const int narrowButtonWidth = width() + CollapsedSidebarWidth - sidebarWidth;
        const qreal centeredIcon = (narrowButtonWidth - option.iconSize.width()) / 2.0;
        const int iconX = qRound(centeredIcon + (contents.left() - centeredIcon) * sidebarExpansion(sidebarWidth));
        const QRect iconRect(QPoint(iconX, (height() - option.iconSize.height()) / 2), option.iconSize);
        option.icon.paint(&painter, iconRect, Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled,
                          isChecked() ? QIcon::On : QIcon::Off);

        QFont labelFont = font();
        if (isChecked()) labelFont.setWeight(QFont::DemiBold);
        const QFontMetrics metrics(labelFont);
        constexpr int labelGap = 8;
        const int labelX = iconRect.right() + 1 + labelGap;
        const int available = contents.right() + 1 - labelX;
        const int expandedAvailable = available + ExpandedSidebarWidth - sidebarWidth + iconX - contents.left();
        const QString label = metrics.elidedText(label_, Qt::ElideRight, expandedAvailable);
        const int labelWidth = metrics.horizontalAdvance(label);
        if (label.isEmpty() || available < labelWidth) return;
        const int fadeWidth = qBound(1, expandedAvailable - labelWidth + 1, 16);
        painter.setOpacity(qBound(0.0, qreal(available - labelWidth + 1) / fadeWidth, 1.0));
        painter.setFont(labelFont);
        painter.setPen(!isEnabled() ? UiTheme::Disabled : isChecked() ? UiTheme::SelectionText : UiTheme::Text);
        painter.drawText(QRect(labelX, 0, available, height()), Qt::AlignLeft | Qt::AlignVCenter, label);
    }

private:
    void paintBevel(QStylePainter &painter, QStyleOptionButton option) const
    {
        option.text.clear();
        option.icon = {};
        if (!isEnabled() || !UiMotion::enabled()) {
            painter.drawControl(QStyle::CE_PushButton, option);
            return;
        }

        const qreal press = press_.currentValue().toReal();
        const qreal scale = 1.0 - 0.015 * press;
        const auto center = QRectF(rect()).center();
        painter.translate(center);
        painter.scale(scale, scale);
        painter.translate(-center);
        option.state.setFlag(QStyle::State_MouseOver, false);
        option.state.setFlag(QStyle::State_Sunken, false);
        painter.drawControl(QStyle::CE_PushButton, option);

        const qreal hover = hover_.currentValue().toReal();
        if (hover > 0.0 && !isChecked()) {
            painter.setOpacity(hover);
            option.state.setFlag(QStyle::State_MouseOver);
            painter.drawControl(QStyle::CE_PushButton, option);
        }
        if (press > 0.0) {
            painter.setOpacity(press);
            option.state.setFlag(QStyle::State_Sunken);
            option.state.setFlag(QStyle::State_Raised, false);
            painter.drawControl(QStyle::CE_PushButton, option);
        }
        painter.setOpacity(1.0);
    }

    void animateFeedback(QVariantAnimation &animation, bool active)
    {
        const qreal end = active ? 1.0 : 0.0;
        const qreal start = animation.currentValue().toReal();
        animation.stop();
        const bool animate = isVisible() && UiMotion::enabled() && start != end;
        animation.setStartValue(animate ? start : end);
        animation.setEndValue(end);
        if (animate) animation.start();
    }

    QString label_;
    QVariantAnimation hover_;
    QVariantAnimation press_;
};

} // namespace

NavigationSidebar::NavigationSidebar(QWidget *parent)
    : QFrame(parent), targetWidth_(ExpandedSidebarWidth)
{
    setObjectName(QStringLiteral("sidebar"));
    auto *navigation = new QVBoxLayout(this);
    navigation->setContentsMargins(14, 16, 14, 16);
    navigation->setSpacing(6);

    widthMotion_.setDuration(SidebarMotionDuration);
    // The live clipboard layout must cross its responsive breakpoint just once.
    widthMotion_.setEasingCurve(QEasingCurve::OutCubic);
    connect(&widthMotion_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        if (widthMotion_.state() == QAbstractAnimation::Running) setFixedWidth(value.toInt());
    });
    connect(&widthMotion_, &QVariantAnimation::finished, this, [this] { setFixedWidth(targetWidth_); });

    indicatorMotion_.setDuration(SidebarMotionDuration);
    indicatorMotion_.setEasingCurve(UiMotion::outBack(0.9));
    indicatorMotion_.setStartValue(0.0);
    indicatorMotion_.setEndValue(1.0);
    connect(&indicatorMotion_, &QVariantAnimation::valueChanged, this, [this] { update(); });
    connect(&UiMotion::instance(), &UiMotion::enabledChanged, this, [this](bool enabled) {
        if (!enabled) finishMotion();
    });
}

QPushButton *NavigationSidebar::addButton(const QString &label, const QString &iconName, bool pinnedToBottom)
{
    auto *button = new NavigationButton(label, this);
    button->setObjectName(QStringLiteral("navButton"));
    button->setCheckable(true);
    button->setAutoExclusive(true);
    button->setIcon(UiTheme::navigationIcon(iconName));
    button->setIconSize(QSize(22, 22));
    button->setToolTip(label);
    button->setCursor(Qt::PointingHandCursor);
    button->setAccessibleName(label);

    auto *navigation = static_cast<QVBoxLayout *>(layout());
    if (pinnedToBottom) navigation->addStretch(1);
    navigation->addWidget(button);
    return button;
}

void NavigationSidebar::setButtonLabel(QPushButton *button, const QString &label)
{
    static_cast<NavigationButton *>(button)->setLabel(label, isCollapsed());
}

bool NavigationSidebar::isCollapsed() const
{
    return targetWidth_ == CollapsedSidebarWidth;
}

void NavigationSidebar::setCollapsed(bool collapsed, bool animated)
{
    targetWidth_ = collapsed ? CollapsedSidebarWidth : ExpandedSidebarWidth;
    for (int i = 0; i < layout()->count(); ++i) {
        if (auto *button = static_cast<NavigationButton *>(layout()->itemAt(i)->widget()))
            button->setCollapsed(collapsed);
    }
    const int from = width();
    widthMotion_.stop();
    if (!animated || !isVisible() || !UiMotion::enabled() || from == targetWidth_) {
        setFixedWidth(targetWidth_);
        return;
    }
    widthMotion_.setStartValue(from);
    widthMotion_.setEndValue(targetWidth_);
    widthMotion_.start();
}

void NavigationSidebar::setActiveButton(QPushButton *button, bool animated)
{
    button->setChecked(true);
    if (activeButton_ == button) {
        if (!animated && indicatorMotion_.state() == QAbstractAnimation::Running) {
            indicatorMotion_.stop();
            update();
        }
        return;
    }
    const bool animate = activeButton_ && animated && isVisible() && UiMotion::enabled();
    if (animate) indicatorStart_ = indicatorCenter();
    indicatorMotion_.stop();
    activeButton_ = button;
    if (animate) indicatorMotion_.start();
    update();
}

void NavigationSidebar::setArtworkPath(const QString &path)
{
    if (path == artworkPath_) return;
    artworkPath_ = path;
    artwork_ = path.isEmpty() ? QPixmap() : QPixmap(path);
    update();
}

bool NavigationSidebar::event(QEvent *event)
{
    const bool handled = QFrame::event(event);
    if (event->type() == QEvent::LayoutRequest) update();
    return handled;
}

void NavigationSidebar::hideEvent(QHideEvent *event)
{
    finishMotion();
    QFrame::hideEvent(event);
}

void NavigationSidebar::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);
    QPainter painter(this);
    paintArtwork(painter);
    if (!activeButton_) return;
    painter.setOpacity(1.0);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(UiTheme::Accent);
    painter.drawRoundedRect(QRectF(5, indicatorCenter() - 10, 3, 20), 1.5, 1.5);
}

void NavigationSidebar::finishMotion()
{
    if (widthMotion_.state() == QAbstractAnimation::Running) {
        widthMotion_.stop();
        setFixedWidth(targetWidth_);
    }
    indicatorMotion_.stop();
    update();
}

qreal NavigationSidebar::indicatorCenter() const
{
    const qreal target = QRectF(activeButton_->geometry()).center().y();
    if (indicatorMotion_.state() == QAbstractAnimation::Stopped) return target;
    const qreal distance = target - indicatorStart_;
    const qreal progress = indicatorMotion_.currentValue().toReal();
    if (progress <= 1.0) return indicatorStart_ + distance * progress;
    return target + qBound(-5.0, distance * (progress - 1.0), 5.0);
}

void NavigationSidebar::paintArtwork(QPainter &painter) const
{
    if (artwork_.isNull()) return;
    const qreal opacity = sidebarExpansion(width());
    if (opacity <= 0.0) return;
    painter.setOpacity(opacity);
    QLinearGradient wash(0, 0, width(), height());
    QColor shade = UiTheme::Surface;
    shade.setAlpha(0);
    wash.setColorAt(0, shade);
    shade.setAlpha(166);
    wash.setColorAt(1, shade);
    painter.fillRect(rect().adjusted(0, 0, -1, 0), wash);
    // Keep artwork in the stretch between the navigation entries and Settings.
    for (int i = 0; i < layout()->count(); ++i) {
        const auto *spacer = layout()->itemAt(i)->spacerItem();
        if (!spacer) continue;
        const QRect gap = spacer->geometry();
        if (gap.height() <= 0) return;
        const QSize size = artwork_.size().scaled(width(), gap.height(), Qt::KeepAspectRatio);
        const int top = qBound(gap.top(), qRound(height() * 0.72 - size.height() * 0.52),
                               gap.bottom() + 1 - size.height());
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setOpacity(0.76 * opacity);
        painter.drawPixmap(QRect((width() - size.width()) / 2, top, size.width(), size.height()), artwork_);
        return;
    }
}
