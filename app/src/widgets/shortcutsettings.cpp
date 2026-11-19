#include "shortcutsettings.h"
#include "data/appdatabase.h"
#include "platform/globalshortcut.h"
#include "ui/theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QValidator>
#include <QVBoxLayout>

namespace {
bool isShortcutLetter(const QString &text)
{
    if (text.size() != 1) return false;
    const auto c = text.front().unicode();
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

class LetterValidator final : public QValidator {
public:
    using QValidator::QValidator;
    State validate(QString &text, int &) const override
    {
        if (text.isEmpty()) return Intermediate;
        if (!isShortcutLetter(text)) return Invalid;
        text = text.toUpper();
        return Acceptable;
    }
};

class ShortcutLetterInput final : public QLineEdit {
public:
    explicit ShortcutLetterInput(QWidget *parent) : QLineEdit(parent)
    {
        setValidator(new LetterValidator(this));
        setObjectName(QStringLiteral("globalShortcutLetter"));
        setAccessibleName(QStringLiteral("快捷键字母"));
        setPlaceholderText(QStringLiteral("A–Z"));
        setToolTip(QStringLiteral("输入一个字母 A–Z，按回车应用"));
        setAlignment(Qt::AlignCenter);
        setInputMethodHints(Qt::ImhLatinOnly | Qt::ImhPreferUppercase | Qt::ImhNoPredictiveText);
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        const bool typing = !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
        if ((typing && isShortcutLetter(event->text())) || event->matches(QKeySequence::Paste))
            selectAll();
        QLineEdit::keyPressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        QLineEdit::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton) selectAll();
    }
};

QString editingWarning(QKeyCombination binding)
{
    if (binding.keyboardModifiers() != Qt::ControlModifier) return {};
    if (binding.key() == Qt::Key_V)
        return QStringLiteral("Ctrl+V 常用于粘贴。启用这个快捷键时，剪贴板条目仍可复制，请用目标应用的右键菜单粘贴。");
    static const QHash<Qt::Key, QString> actions {
        {Qt::Key_A, QStringLiteral("全选")}, {Qt::Key_C, QStringLiteral("复制")},
        {Qt::Key_F, QStringLiteral("查找")}, {Qt::Key_S, QStringLiteral("保存")},
        {Qt::Key_X, QStringLiteral("剪切")}, {Qt::Key_Y, QStringLiteral("重做")},
        {Qt::Key_Z, QStringLiteral("撤销")}
    };
    const auto action = actions.value(binding.key());
    if (action.isEmpty()) return {};
    return QStringLiteral("%1 常用于%2。启用这个快捷键时，在其他应用中按下会唤起本窗口。")
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
    enabled_ = new QCheckBox(QStringLiteral("启用全局快捷键"), this);
    enabled_->setObjectName(QStringLiteral("globalShortcutCheck"));
    row->addWidget(enabled_);

    auto *segments = new QFrame(this);
    segments->setObjectName(QStringLiteral("shortcutModifierSegments"));
    segments->setAccessibleName(QStringLiteral("快捷键修饰键"));
    auto *segmentRow = new QHBoxLayout(segments);
    segmentRow->setContentsMargins(3, 3, 3, 3);
    segmentRow->setSpacing(2);
    modifier_ = new QButtonGroup(segments);
    modifier_->setObjectName(QStringLiteral("globalShortcutModifier"));
    for (auto modifier : {Qt::AltModifier, Qt::ControlModifier}) {
        auto *segment = new QToolButton(segments);
        segment->setText(modifier == Qt::AltModifier ? QStringLiteral("Alt") : QStringLiteral("Ctrl"));
        segment->setCheckable(true);
        segment->setCursor(Qt::PointingHandCursor);
        segment->setFocusPolicy(Qt::StrongFocus);
        modifier_->addButton(segment, int(modifier));
        segment->setChecked(shortcut_.binding().keyboardModifiers() == modifier);
        segmentRow->addWidget(segment);
    }
    row->addWidget(segments);
    auto *plus = new QLabel(QStringLiteral("+"), this);
    UiTheme::setRole(plus, "muted");
    row->addWidget(plus);

    letter_ = new ShortcutLetterInput(this);
    letter_->setText(QString(QChar(char16_t(shortcut_.binding().key()))));
    row->addWidget(letter_);
    apply_ = new QPushButton(this);
    apply_->setObjectName(QStringLiteral("globalShortcutApply"));
    UiTheme::setRole(apply_, "primary");
    row->addWidget(apply_);
    row->addStretch();

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
    connect(modifier_, &QButtonGroup::idClicked, this, &ShortcutSettings::refresh);
    connect(letter_, &QLineEdit::textChanged, this, &ShortcutSettings::refresh);
    connect(letter_, &QLineEdit::returnPressed, apply_, &QPushButton::click);
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
    const QString letter = letter_->text();
    return QKeyCombination(Qt::KeyboardModifier(modifier_->checkedId()),
                           letter.isEmpty() ? Qt::Key_unknown : Qt::Key(letter.front().unicode()));
}

void ShortcutSettings::refresh()
{
    const QSignalBlocker blocker(enabled_);
    enabled_->setChecked(shortcut_.enabled());
    const auto binding = selection();
    const bool changed = binding != shortcut_.binding();
    const bool retry = !changed && shortcut_.enabled() && !shortcut_.isRegistered();
    apply_->setText(retry ? QStringLiteral("重试注册") : QStringLiteral("应用"));
    apply_->setEnabled(letter_->hasAcceptableInput() && (changed || retry));
    const QString warning = editingWarning(binding);
    warning_->setText(warning);
    warning_->setVisible(!warning.isEmpty());
}
