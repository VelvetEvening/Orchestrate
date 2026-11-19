#pragma once

#include <QWidget>

class AppDatabase;
class GlobalShortcut;
class QButtonGroup;
class QLabel;
class QLineEdit;
class QPushButton;

class ShortcutSettings final : public QWidget {
public:
    ShortcutSettings(GlobalShortcut &shortcut, AppDatabase &database, QWidget *parent);

private:
    void applyBinding();
    void applyEnabled(bool enabled);
    void refresh();

    GlobalShortcut &shortcut_;
    AppDatabase &database_;
    QPushButton *enabled_;
    QButtonGroup *modifier_;
    QLineEdit *letter_;
    QLabel *status_;
};
