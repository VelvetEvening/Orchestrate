#pragma once

#include "theme.h"

#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>

namespace SettingsForm {

inline constexpr int LabelColumn = 0;
inline constexpr int AccessoryColumn = 1;
inline constexpr int ControlColumn = 2;
inline constexpr int ColumnCount = ControlColumn + 1;

inline void configure(QGridLayout &form)
{
    form.setContentsMargins(0, 0, 0, 0);
    form.setHorizontalSpacing(12);
    form.setVerticalSpacing(8);
    form.setColumnStretch(AccessoryColumn, 1);
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

class ToggleButton final : public QPushButton
{
public:
    using QPushButton::QPushButton;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPushButton::paintEvent(event);
        const bool checked = isChecked();
        const QColor trackColor = checked ? UiTheme::Accent : UiTheme::ControlBorder;
        const QColor stroke = isEnabled() ? trackColor : UiTheme::Disabled;
        const QRectF track(9, (height() - 16) / 2.0, 30, 16);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(stroke);
        painter.setBrush(checked ? stroke : UiTheme::Surface);
        painter.drawRoundedRect(track, 8, 8);
        const qreal thumbLeft = checked ? track.right() - 14 : track.left() + 2;
        painter.setPen(Qt::NoPen);
        painter.setBrush(checked ? UiTheme::Surface : stroke);
        painter.drawEllipse(QRectF(thumbLeft, track.top() + 2, 12, 12));
    }
};

inline QString toggleText(bool enabled)
{
    return enabled ? QStringLiteral("开启") : QStringLiteral("关闭");
}

inline void setToggleValue(QAbstractButton &button, bool enabled)
{
    const QSignalBlocker blocker(button);
    button.setChecked(enabled);
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
    QObject::connect(button, &QPushButton::toggled, button, [button](bool checked) {
        button->setText(toggleText(checked));
    });
    return button;
}

} // namespace SettingsForm
