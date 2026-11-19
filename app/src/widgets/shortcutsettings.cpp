#include "shortcutsettings.h"
#include "data/appdatabase.h"
#include "platform/globalshortcut.h"
#include "ui/theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {
QString editingWarning(QKeyCombination binding)
{
    if (binding.keyboardModifiers() != Qt::ControlModifier) return {};
    if (binding.key() == Qt::Key_V)
        return QStringLiteral("Ctrl+V 常用于粘贴。启用这个唤起键时，剪贴板条目仍可复制，请用目标应用的右键菜单粘贴。");
    static const QHash<Qt::Key, QString> actions {
        {Qt::Key_A, QStringLiteral("全选")}, {Qt::Key_C, QStringLiteral("复制")},
        {Qt::Key_F, QStringLiteral("查找")}, {Qt::Key_S, QStringLiteral("保存")},
        {Qt::Key_X, QStringLiteral("剪切")}, {Qt::Key_Y, QStringLiteral("重做")},
        {Qt::Key_Z, QStringLiteral("撤销")}
    };
    const auto action = actions.value(binding.key());
    if (action.isEmpty()) return {};
    return QStringLiteral("%1 常用于%2。启用这个唤起键时，在其他应用中按下也会打开本窗口。")
        .arg(GlobalShortcut::text(binding), action);
}
}

ShortcutSettings::ShortcutSettings(GlobalShortcut &shortcut, AppDatabase &database, QWidget *parent)
    : QWidget(parent), shortcut_(shortcut), database_(database)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    layout->addLayout(row);
    enabled_ = new QCheckBox(QStringLiteral("启用全局唤起快捷键"), this);
    enabled_->setObjectName(QStringLiteral("globalShortcutCheck"));
    row->addWidget(enabled_);
    row->addStretch();

    modifier_ = new QComboBox(this);
    modifier_->setObjectName(QStringLiteral("globalShortcutModifier"));
    modifier_->setAccessibleName(QStringLiteral("快捷键修饰键"));
    modifier_->addItem(QStringLiteral("Alt"), int(Qt::AltModifier));
    modifier_->addItem(QStringLiteral("Ctrl"), int(Qt::ControlModifier));
    modifier_->setCurrentIndex(modifier_->findData(int(shortcut_.binding().keyboardModifiers())));
    row->addWidget(modifier_);
    auto *plus = new QLabel(QStringLiteral("+"), this);
    UiTheme::setRole(plus, "muted");
    row->addWidget(plus);

    letter_ = new QComboBox(this);
    letter_->setObjectName(QStringLiteral("globalShortcutLetter"));
    letter_->setAccessibleName(QStringLiteral("快捷键字母"));
    for (int key = Qt::Key_A; key <= Qt::Key_Z; ++key)
        letter_->addItem(QString(QChar(key)), key);
    letter_->setMaxVisibleItems(8);
    letter_->setCurrentIndex(letter_->findData(int(shortcut_.binding().key())));
    row->addWidget(letter_);
    apply_ = new QPushButton(this);
    apply_->setObjectName(QStringLiteral("globalShortcutApply"));
    row->addWidget(apply_);

    auto *status = new QLabel(shortcut_.status(), this);
    status->setObjectName(QStringLiteral("globalShortcutStatus"));
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    warning_ = new QLabel(this);
    warning_->setObjectName(QStringLiteral("globalShortcutWarning"));
    warning_->setWordWrap(true);
    warning_->setTextFormat(Qt::PlainText);
    UiTheme::setRole(warning_, "warning");
    layout->addWidget(warning_);

    connect(&shortcut_, &GlobalShortcut::statusChanged, this, [this, status](const QString &message) {
        status->setText(message);
        refresh();
    });
    connect(modifier_, &QComboBox::currentIndexChanged, this, &ShortcutSettings::refresh);
    connect(letter_, &QComboBox::currentIndexChanged, this, &ShortcutSettings::refresh);
    connect(enabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        shortcut_.apply(enabled, shortcut_.binding(), [this, enabled](QString *error) {
            return database_.setSetting("global_shortcut_enabled", enabled ? "true" : "false", error);
        });
    });
    connect(apply_, &QPushButton::clicked, this, [this] {
        const auto binding = selection();
        if (binding == shortcut_.binding()) {
            shortcut_.apply(shortcut_.enabled(), binding);
            return;
        }
        shortcut_.apply(shortcut_.enabled(), binding, [this, binding](QString *error) {
            return database_.setSetting("global_shortcut_binding", GlobalShortcut::text(binding), error);
        });
    });
    refresh();
}

QKeyCombination ShortcutSettings::selection() const
{
    return QKeyCombination(Qt::KeyboardModifier(modifier_->currentData().toInt()), Qt::Key(letter_->currentData().toInt()));
}

void ShortcutSettings::refresh()
{
    const QSignalBlocker blocker(enabled_);
    enabled_->setChecked(shortcut_.enabled());
    const auto binding = selection();
    const bool changed = binding != shortcut_.binding();
    const bool retry = !changed && shortcut_.enabled() && !shortcut_.isRegistered();
    apply_->setText(retry ? QStringLiteral("重试注册") : QStringLiteral("应用"));
    apply_->setEnabled(changed || retry);
    const QString warning = editingWarning(binding);
    warning_->setText(warning);
    warning_->setVisible(!warning.isEmpty());
}
