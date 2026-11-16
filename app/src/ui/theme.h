#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;
class QWidget;

namespace UiTheme {
// Shared by the stylesheet, custom calendar/drag painting and status badges.
inline constexpr auto Background = "#f7f9fa";
inline constexpr auto Sidebar = "#eff3f4";
inline constexpr auto IndexSurface = "#f7f9fa";
inline constexpr auto Surface = "#ffffff";
inline constexpr auto Text = "#2f3b48";
inline constexpr auto Muted = "#5e6c78";
inline constexpr auto Border = "#dfe6ea";
inline constexpr auto ControlBorder = "#c6d0dd";
inline constexpr auto Accent = "#365f8c";
inline constexpr auto Selection = "#dfe9f2";
inline constexpr auto Hover = "#edf2f5";
inline constexpr auto Focus = "#6f8fb6";
inline constexpr auto PrimaryHover = "#d2e0ee";
inline constexpr auto PrimaryPressed = "#c4d6e7";
inline constexpr auto NavHover = "#e5ebee";
inline constexpr auto ScrollHandle = "#bdc8d3";
inline constexpr auto ScrollHandleHover = "#97a8b9";
inline constexpr auto Progress = "#c7d7ed";
inline constexpr auto Success = "#386b50";
inline constexpr auto SuccessBackground = "#edf5ef";
inline constexpr auto Warning = "#87611e";
inline constexpr auto WarningBackground = "#faf3e5";
inline constexpr auto Danger = "#a64545";
inline constexpr auto DangerBackground = "#fbefef";
inline constexpr auto DangerHoverBorder = "#e6c5c5";
inline constexpr auto Disabled = "#919ba9";

void install(QApplication &app);
QIcon icon(const QString &name);
void setRole(QWidget *widget, const char *role);
void setSurface(QWidget *widget, const char *surface);
QString statusStyle(const QString &result);
QColor statusColor(const QString &result);
}
