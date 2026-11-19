#pragma once

#include <QColor>
#include <QIcon>
#include <QList>
#include <QMargins>
#include <QString>

class QAbstractItemModel;
class QApplication;
class QCalendarWidget;
class QComboBox;
class QLayout;
class QModelIndex;
class QWidget;
struct SkinTheme;

namespace UiTheme {
// Shared by the stylesheet, custom calendar/drag painting and status badges.
extern const QColor &Background;
extern const QColor &Sidebar;
extern const QColor &IndexSurface;
extern const QColor &Surface;
extern const QColor &Text;
extern const QColor &Muted;
extern const QColor &Border;
extern const QColor &ControlBorder;
extern const QColor &Accent;
extern const QColor &Selection;
extern const QColor &SelectionText;
extern const QColor &Focus;
// Calendar record markers are informational, not success/warning status.
extern const QColor &DiaryMarker;
extern const QColor &ReminderMarker;
extern const QColor &Success;
extern const QColor &SuccessBackground;
extern const QColor &Warning;
extern const QColor &Danger;
extern const QColor &DangerBackground;
extern const QColor &Disabled;
extern const QColor &GraphSecondary;
extern const QColor &GraphTertiary;

QString paletteId();
QString layoutId();
bool isCompact();
void setInstalledThemes(const QList<SkinTheme> &themes);
QList<SkinTheme> installedThemes();
QString sidebarImagePath();
void apply(QApplication &app, const QString &paletteId, const QString &layoutId);
void install(QApplication &app);
QIcon icon(const QString &name);
QIcon paletteSwatch(const QString &id);
void styleAppearanceChoice(QComboBox *choice);
void setRole(QWidget *widget, const char *role);
void setSurface(QWidget *widget, const char *surface);
void setLayoutMetrics(QLayout *layout, const QMargins &margins, int spacing = -1);
void setItemForeground(QAbstractItemModel *model, const QModelIndex &index, const char *colorToken);
void applyCalendarPalette(QCalendarWidget *calendar);
}
