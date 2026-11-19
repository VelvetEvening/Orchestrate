#include "shortcutsettings.h"
#include "data/appdatabase.h"
#include "platform/globalshortcut.h"
#include "ui/settingsform.h"
#include "ui/theme.h"

#include <QButtonGroup>
#include <QFocusEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QToolButton>
#include <QValidator>
#include <functional>
#include <utility>

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
    ShortcutLetterInput(QWidget *parent, std::function<void()> restore)
        : QLineEdit(parent), restore_(std::move(restore))
    {
        setValidator(new LetterValidator(this));
        setObjectName(QStringLiteral("globalShortcutLetter"));
        setAccessibleName(QStringLiteral("快捷键字母"));
        setPlaceholderText(QStringLiteral("A–Z"));
        setAlignment(Qt::AlignCenter);
        setInputMethodHints(Qt::ImhLatinOnly | Qt::ImhPreferUppercase | Qt::ImhNoPredictiveText);
    }

protected:
    void focusOutEvent(QFocusEvent *event) override
    {
        QLineEdit::focusOutEvent(event);
        if (event->reason() != Qt::PopupFocusReason && !hasAcceptableInput()) restore_();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        const bool typing = !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
        if ((typing && isShortcutLetter(event->text())) || event->matches(QKeySequence::Paste)) {
            if (event->isAutoRepeat()) {
                event->accept();
                return;
            }
            selectAll();
        }
        QLineEdit::keyPressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        QLineEdit::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton) selectAll();
    }

private:
    std::function<void()> restore_;
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
    auto *form = new QGridLayout(this);
    SettingsForm::configure(*form);
    enabled_ = SettingsForm::makeToggle(this, QStringLiteral("globalShortcutEnabled"), shortcut_.enabled());
    SettingsForm::addRow(*form, 0, QStringLiteral("全局快捷键"), enabled_);

    auto *bindingEditor = new QWidget(this);
    bindingEditor->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto *editor = new QHBoxLayout(bindingEditor);
    editor->setContentsMargins(0, 0, 0, 0);
    editor->setSpacing(6);
    form->addWidget(bindingEditor, 0, SettingsForm::AccessoryColumn, Qt::AlignRight | Qt::AlignVCenter);

    auto *segments = new QFrame(bindingEditor);
    segments->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
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
        segment->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        modifier_->addButton(segment, int(modifier));
        segmentRow->addWidget(segment);
    }
    editor->addWidget(segments);
    auto *plus = new QLabel(QStringLiteral("+"), bindingEditor);
    plus->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    UiTheme::setRole(plus, "muted");
    editor->addWidget(plus);

    letter_ = new ShortcutLetterInput(bindingEditor, [this] { refresh(); });
    letter_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    editor->addWidget(letter_);
    QWidget::setTabOrder(letter_, enabled_);

    status_ = SettingsForm::addStatus(*form, 1, QStringLiteral("globalShortcutStatus"), this);

    refresh();
    connect(&shortcut_, &GlobalShortcut::statusChanged, this, &ShortcutSettings::refresh);
    connect(modifier_, &QButtonGroup::idClicked, this, &ShortcutSettings::applyBinding);
    connect(letter_, &QLineEdit::textChanged, this, &ShortcutSettings::applyBinding);
    connect(enabled_, &QPushButton::toggled, this, &ShortcutSettings::applyEnabled);
}

void ShortcutSettings::applyBinding()
{
    if (!letter_->hasAcceptableInput()) return;
    const auto key = Qt::Key(letter_->text().at(0).toUpper().unicode());
    const QKeyCombination binding(Qt::KeyboardModifier(modifier_->checkedId()), key);
    if (binding == shortcut_.binding()) {
        if (shortcut_.hasError()) shortcut_.apply(shortcut_.enabled(), binding);
        else refresh();
        return;
    }
    shortcut_.apply(shortcut_.enabled(), binding, [this, binding](QString *error) {
        return database_.setSetting("global_shortcut_binding", GlobalShortcut::text(binding), error);
    });
}

void ShortcutSettings::applyEnabled(bool enabled)
{
    if (enabled == shortcut_.enabled()) return;
    shortcut_.apply(enabled, shortcut_.binding(), [this, enabled](QString *error) {
        return database_.setSetting("global_shortcut_enabled", enabled ? "true" : "false", error);
    });
}

void ShortcutSettings::refresh()
{
    SettingsForm::setToggleValue(*enabled_, shortcut_.enabled());
    const auto binding = shortcut_.binding();
    const QSignalBlocker modifierBlocker(modifier_);
    modifier_->button(int(binding.keyboardModifiers()))->setChecked(true);
    const QSignalBlocker letterBlocker(letter_);
    const QString letter(QChar(char16_t(binding.key())));
    if (letter_->text() != letter) letter_->setText(letter);

    QString hint = QStringLiteral("输入一个 A–Z 字母，自动保存。");
    const auto warning = editingWarning(binding);
    if (!warning.isEmpty()) hint += QLatin1Char('\n') + warning;
    letter_->setToolTip(hint);
    const auto error = shortcut_.status();
    status_->setText(error);
    status_->setVisible(!error.isEmpty());
}
