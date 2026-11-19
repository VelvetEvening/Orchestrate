#pragma once

#include <QKeySequence>
#include <QWidget>

class AppDatabase;
class GlobalShortcut;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

class ShortcutSettings final : public QWidget {
public:
    ShortcutSettings(GlobalShortcut &shortcut, AppDatabase &database, QWidget *parent);

private:
    QKeyCombination selection() const;
    void refresh();

    GlobalShortcut &shortcut_;
    AppDatabase &database_;
    QCheckBox *enabled_;
    QComboBox *modifier_;
    QComboBox *letter_;
    QPushButton *apply_;
    QLabel *warning_;
};
