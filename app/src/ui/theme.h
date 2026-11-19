#pragma once

#include <QColor>
#include <QIcon>
#include <QMargins>
#include <QString>

class QApplication;
class QCalendarWidget;
class QComboBox;
class QLayout;
class QWidget;

namespace UiTheme {
// Shared by the stylesheet, custom calendar/drag painting and status badges.
inline const char *Background = "#f7f9fa";
inline const char *Sidebar = "#eff3f4";
inline const char *IndexSurface = "#f7f9fa";
inline const char *Surface = "#ffffff";
inline const char *Text = "#2f3b48";
inline const char *Muted = "#5e6c78";
inline const char *Border = "#dfe6ea";
inline const char *ControlBorder = "#b3bfcd";
inline const char *Accent = "#365f8c";
inline const char *Selection = "#dfe9f2";
inline const char *Hover = "#edf2f5";
inline const char *Focus = "#6f8fb6";
// Calendar record markers are informational, not success/warning status.
inline const char *DiaryMarker = "#648098";
inline const char *ReminderMarker = "#887789";
inline constexpr const char *Success = "#386b50";
inline constexpr const char *SuccessBackground = "#edf5ef";
inline constexpr const char *Warning = "#87611e";
inline constexpr const char *WarningBackground = "#faf3e5";
inline constexpr const char *Danger = "#a64545";
inline constexpr const char *DangerBackground = "#fbefef";
inline constexpr const char *DangerHoverBorder = "#e6c5c5";
inline constexpr const char *Disabled = "#919ba9";

QString paletteId();
QString layoutId();
bool isCompact();
void apply(QApplication &app, const QString &paletteId, const QString &layoutId);
void install(QApplication &app);
QIcon icon(const QString &name);
QIcon paletteSwatch(const QString &id);
void styleAppearanceChoice(QComboBox *choice);
void setRole(QWidget *widget, const char *role);
void setSurface(QWidget *widget, const char *surface);
void setLayoutMetrics(QLayout *layout, const QMargins &margins, int spacing = -1);
void applyCalendarPalette(QCalendarWidget *calendar);
}
