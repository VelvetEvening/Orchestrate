#pragma once

#include <QKeySequence>
#include <QWidget>

class AppDatabase;
class GlobalShortcut;
class QButtonGroup;
class QCheckBox;
class QLabel;
class QLineEdit;
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
    QButtonGroup *modifier_;
    QLineEdit *letter_;
    QPushButton *apply_;
    QLabel *warning_;
};
