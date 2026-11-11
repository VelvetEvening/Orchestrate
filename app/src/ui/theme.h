#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;
class QWidget;

namespace UiTheme {
// Shared by the stylesheet, custom calendar/drag painting and status badges.
inline constexpr auto Background = "#f6f7f9";
inline constexpr auto Sidebar = "#f0f2f5";
inline constexpr auto Surface = "#ffffff";
inline constexpr auto Text = "#344254";
inline constexpr auto Muted = "#637184";
inline constexpr auto Border = "#e1e6ed";
inline constexpr auto ControlBorder = "#c6d0dd";
inline constexpr auto Accent = "#41638f";
inline constexpr auto Selection = "#e7eef8";
inline constexpr auto Hover = "#f0f3f7";
inline constexpr auto Focus = "#7895bc";
inline constexpr auto Success = "#386b50";
inline constexpr auto SuccessBackground = "#edf5ef";
inline constexpr auto Warning = "#87611e";
inline constexpr auto WarningBackground = "#faf3e5";
inline constexpr auto Danger = "#a64545";
inline constexpr auto DangerBackground = "#fbefef";
inline constexpr auto Disabled = "#919ba9";

void install(QApplication &app);
QIcon icon(const QString &name);
void setRole(QWidget *widget, const char *role);
QString statusStyle(const QString &result);
QColor statusColor(const QString &result);
}
