#pragma once

#include "motion.h"
#include "theme.h"

#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVariantAnimation>

namespace SettingsForm {

inline constexpr int LabelColumn = 0;
inline constexpr int AccessoryColumn = 1;
inline constexpr int ControlColumn = 2;
inline constexpr int ColumnCount = ControlColumn + 1;

inline void configure(QGridLayout &form, int stretchColumn = AccessoryColumn)
{
    form.setContentsMargins(0, 0, 0, 0);
    form.setHorizontalSpacing(12);
    form.setVerticalSpacing(8);
    form.setColumnStretch(stretchColumn, 1);
}

inline void addRow(QGridLayout &form, int row, const QString &label, QWidget *control)
{
    auto *caption = new QLabel(label, control->parentWidget());
    caption->setBuddy(control);
    control->setAccessibleName(label);
    form.addWidget(caption, row, LabelColumn);
    form.addWidget(control, row, ControlColumn);
}

inline QLabel *addStatus(QGridLayout &form, int row, const QString &name, QWidget *parent)
{
    auto *status = new QLabel(parent);
    status->setObjectName(name);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    UiTheme::setRole(status, "error");
    status->hide();
    form.addWidget(status, row, LabelColumn, 1, ColumnCount);
    return status;
}

inline QString toggleText(bool enabled)
{
    return enabled ? QStringLiteral("开启") : QStringLiteral("关闭");
}

class ToggleButton final : public QPushButton
{
public:
    explicit ToggleButton(QWidget *parent) : QPushButton(parent)
    {
        animation_.setEasingCurve(UiMotion::outBack(1.0));
        connect(&animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            thumbProgress_ = value.toReal();
            update();
        });
        connect(this, &QPushButton::toggled, this, [this](bool checked) {
            setText(toggleText(checked));
            const qreal target = checked ? 1.0 : 0.0;
            const int duration = UiMotion::duration(170);
            if (!isVisible() || duration == 0 || thumbProgress_ == target) {
                settleThumb();
                return;
            }
            animation_.stop();
            animation_.setDuration(duration);
            animation_.setStartValue(thumbProgress_);
            animation_.setEndValue(target);
            animation_.start();
        });
        connect(&UiMotion::instance(), &UiMotion::enabledChanged, this, [this](bool enabled) {
            if (!enabled) settleThumb();
        });
    }

protected:
    void checkStateSet() override
    {
        QPushButton::checkStateSet();
        settleThumb();
    }

    void paintEvent(QPaintEvent *event) override
    {
        QPushButton::paintEvent(event);
        const QColor &off = isEnabled() ? UiTheme::ControlBorder : UiTheme::Disabled;
        const QColor &on = isEnabled() ? UiTheme::Accent : UiTheme::Disabled;
        const qreal progress = qBound(0.0, thumbProgress_, 1.0);
        const QRectF track(9, (height() - 16) / 2.0, 30, 16);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(blend(off, on, progress));
        painter.setBrush(blend(UiTheme::Surface, on, progress));
        painter.drawRoundedRect(track, 8, 8);
        const qreal thumbLeft = track.left() + 2 + (track.width() - 16) * thumbProgress_;
        painter.setPen(Qt::NoPen);
        painter.setBrush(blend(off, UiTheme::Surface, progress));
        painter.drawEllipse(QRectF(thumbLeft, track.top() + 2, 12, 12));
    }

private:
    static QColor blend(const QColor &from, const QColor &to, qreal progress)
    {
        const auto channel = [progress](qreal start, qreal end) { return start + (end - start) * progress; };
        return QColor::fromRgbF(channel(from.redF(), to.redF()), channel(from.greenF(), to.greenF()),
                               channel(from.blueF(), to.blueF()), channel(from.alphaF(), to.alphaF()));
    }

    void settleThumb()
    {
        animation_.stop();
        thumbProgress_ = isChecked() ? 1.0 : 0.0;
        update();
    }

    QVariantAnimation animation_;
    qreal thumbProgress_ = 0.0;
};

inline void setToggleValue(QAbstractButton &button, bool enabled)
{
    const QSignalBlocker blocker(button);
    if (button.isChecked() != enabled) button.setChecked(enabled);
    button.setText(toggleText(enabled));
}

inline QPushButton *makeToggle(QWidget *parent, const QString &name, bool enabled)
{
    auto *button = new ToggleButton(parent);
    button->setObjectName(name);
    button->setCheckable(true);
    button->setAutoDefault(false);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedWidth(96);
    UiTheme::setRole(button, "settingsToggle");
    setToggleValue(*button, enabled);
    return button;
}

} // namespace SettingsForm
