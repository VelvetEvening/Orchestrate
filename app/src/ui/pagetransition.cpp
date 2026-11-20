#include "pagetransition.h"
#include "motion.h"

#include <QApplication>
#include <QDeadlineTimer>
#include <QEvent>
#include <QPainter>
#include <QPointer>
#include <QStackedWidget>
#include <QTimer>
#include <QVariantAnimation>
#include <algorithm>
#include <utility>

namespace {
using PageTransition::Direction;
constexpr int DurationMs = 210;
constexpr qreal ModuleTravel = 15.0;
constexpr qreal ModuleScaleDelta = 0.012;
constexpr qreal ChildTravel = 26.0;
constexpr auto RetargetDeadline = "_orchestratePageRetargetDeadline";

void extendRetargetDeadline(QWidget *window)
{
    window->setProperty(RetargetDeadline, QVariant::fromValue(QDeadlineTimer(DurationMs, Qt::PreciseTimer)));
}

class TransitionLayer;
QList<TransitionLayer *> activeLayers;

class TransitionLayer final : public QWidget {
public:
    TransitionLayer(QStackedWidget *stack, QWidget *page, const QRect &bounds,
                    QPixmap outgoing, Direction direction)
        : QWidget(stack), stack_(stack), page_(page),
          outgoing_(std::move(outgoing)), direction_(direction)
    {
        setObjectName(QStringLiteral("pageTransition"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setGeometry(bounds);

        animation_.setDuration(DurationMs);
        animation_.setStartValue(0.0);
        animation_.setEndValue(1.0);
        animation_.setEasingCurve(UiMotion::outBack(0.9));
        connect(&animation_, &QVariantAnimation::valueChanged, this, [this] { update(); });
        connect(&animation_, &QVariantAnimation::finished, this, &TransitionLayer::dismiss);
        connect(stack_, &QStackedWidget::currentChanged, this, [this] {
            if (stack_->currentWidget() != page_) dismiss();
        });
        connect(stack_, &QStackedWidget::widgetRemoved, this, &TransitionLayer::dismiss);
        connect(&UiMotion::instance(), &UiMotion::enabledChanged, this, [this](bool enabled) {
            if (!enabled) dismiss();
        });

        activeLayers.append(this);
        qApp->installEventFilter(this);
    }

    ~TransitionLayer() override { activeLayers.removeOne(this); }

    QStackedWidget *stack() const { return stack_; }

    void holdOutgoingFrame()
    {
        if (dismissed_) return;
        raise();
        show();
        QMetaObject::invokeMethod(this, &TransitionLayer::start, Qt::QueuedConnection);
    }

    void start()
    {
        if (dismissed_) return;
        const QRect bounds = geometry();
        if (!page_ || stack_->currentWidget() != page_ || !page_->isVisible()
            || stack_->window()->isMinimized() || stack_->contentsRect() != bounds || !UiMotion::enabled()) {
            dismiss();
            return;
        }

        // The caller has finished initializing the live page and restoring its focus.
        const QPointer<TransitionLayer> self(this);
        auto incoming = page_->grab(bounds.translated(-page_->pos()));
        if (!self || dismissed_) return;
        if (incoming.isNull()) {
            dismiss();
            return;
        }
        incoming_ = std::move(incoming);
        extendRetargetDeadline(stack_->window());
        // End snapshot coverage even if the animation driver runs late.
        QTimer::singleShot(DurationMs, Qt::PreciseTimer, this, &TransitionLayer::dismiss);
        animation_.start();
    }

    void dismiss()
    {
        if (dismissed_) return;
        dismissed_ = true;
        activeLayers.removeOne(this);
        qApp->removeEventFilter(this);
        animation_.stop();
        hide();
        outgoing_ = {};
        incoming_ = {};
        deleteLater();
    }

protected:
    bool eventFilter(QObject *receiver, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::ShortcutOverride:
        case QEvent::KeyPress:
        case QEvent::InputMethod:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::Wheel:
        case QEvent::TabletPress:
        case QEvent::TouchBegin:
        case QEvent::DragEnter:
        case QEvent::ContextMenu:
            // Reveal the current native caret and controls before delivering input.
            dismiss();
            break;
        case QEvent::FocusIn:
        case QEvent::FocusOut:
            if (animation_.state() == QAbstractAnimation::Running) dismiss();
            break;
        case QEvent::Resize:
            if (receiver == stack_ || receiver == stack_->window()
                || (animation_.state() == QAbstractAnimation::Running && receiver == page_))
                dismiss();
            break;
        case QEvent::Hide:
        case QEvent::ParentChange:
        case QEvent::DevicePixelRatioChange:
        case QEvent::PaletteChange:
        case QEvent::StyleChange:
            if (receiver == stack_ || receiver == stack_->window() || receiver == page_) dismiss();
            break;
        case QEvent::WindowDeactivate:
        case QEvent::WindowStateChange:
            if (receiver == stack_->window()) dismiss();
            break;
        default:
            break;
        }
        return false;
    }

    void paintEvent(QPaintEvent *) override
    {
        const qreal progress = animation_.currentValue().toReal();
        const qreal opacity = std::clamp(progress, 0.0, 1.0);
        const qreal remaining = 1.0 - progress;
        const qreal travel = direction_ == Direction::Back ? -ChildTravel : ChildTravel;
        const bool module = direction_ == Direction::Module;

        QPainter painter(this);
        painter.fillRect(rect(), palette().brush(QPalette::Window));
        if (opacity < 1.0) {
            painter.setOpacity(1.0 - opacity);
            painter.drawPixmap(QPointF(module ? 0.0 : -travel * opacity, 0), outgoing_);
        }
        painter.setOpacity(opacity);
        if (module) {
            const QPointF center = QRectF(rect()).center();
            const qreal scale = 1.0 - ModuleScaleDelta * remaining;
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.translate(center + QPointF(0, ModuleTravel * remaining));
            painter.scale(scale, scale);
            painter.translate(-center);
        } else {
            painter.translate(travel * remaining, 0);
        }
        painter.drawPixmap(QPointF(), incoming_);
    }

private:
    QStackedWidget *const stack_;
    QPointer<QWidget> page_;
    QPixmap outgoing_;
    QPixmap incoming_;
    const Direction direction_;
    QVariantAnimation animation_;
    bool dismissed_ = false;
};

bool cancelRelated(QStackedWidget *stack, bool includeChildren)
{
    const auto layers = activeLayers;
    bool interrupted = false;
    for (auto *layer : layers) {
        auto *owner = layer->stack();
        if (owner == stack || owner->isAncestorOf(stack) || (includeChildren && stack->isAncestorOf(owner))) {
            layer->dismiss();
            interrupted = true;
        }
    }
    return interrupted;
}
}

void PageTransition::setCurrent(QStackedWidget *stack, QWidget *page, Direction direction)
{
    if (!stack || !page || stack->indexOf(page) < 0) return;
    if (stack->currentWidget() == page) {
        if (direction == Direction::Instant) cancel(stack);
        return;
    }

    const bool interrupted = cancelRelated(stack, true);
    const QRect bounds = stack->contentsRect();
    auto *window = stack->window();
    QPointer<TransitionLayer> layer;
    if (direction != Direction::Instant && UiMotion::enabled() && stack->isVisible()
        && !window->isMinimized() && stack->currentWidget() && !bounds.isEmpty()) {
        // Keep the deadline across nested stacks and input that dismisses the layer before navigation.
        const auto deadline = window->property(RetargetDeadline).value<QDeadlineTimer>();
        extendRetargetDeadline(window);
        if (!interrupted && deadline.hasExpired()) {
            auto outgoing = stack->grab(bounds);
            if (!outgoing.isNull()) layer = new TransitionLayer(stack, page, bounds, std::move(outgoing), direction);
        }
    }

    stack->setCurrentWidget(page);
    if (layer) layer->holdOutgoingFrame();
}

void PageTransition::cancel(QStackedWidget *stack)
{
    if (stack) cancelRelated(stack, false);
}
