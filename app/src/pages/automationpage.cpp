#include "automationpage.h"
#include "automation/commandarguments.h"
#include "automation/toolstate.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringDecoder>
#include <QStyle>
#include <QTimeEdit>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <cmath>
#include <memory>

namespace {

constexpr int kGroupItem = 1;
constexpr int kToolItem = 2;
const QString kUngroupedNameKey = QStringLiteral("automation.ungrouped_name");
const QString kBuiltinGroupKey = QStringLiteral("automation.builtin_group_id");
const QString kBuiltinGroupName = QStringLiteral("系统工具");

QFrame *makeCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("card"));
    card->setFrameShape(QFrame::StyledPanel);
    card->setFrameShadow(QFrame::Plain);
    return card;
}

QLabel *makeTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("pageTitle"));
    return label;
}

QLabel *makeCardTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("cardTitle"));
    return label;
}

QLabel *makeMuted(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("muted"));
    label->setWordWrap(true);
    return label;
}

QLabel *makeTag(const QString &text, const QString &objectName, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(objectName);
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return label;
}

QString jsonString(const QJsonObject &object, const QString &key, const QString &fallback = QString())
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : fallback;
}

QString jsonScalarText(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        return number == std::floor(number) ? QString::number(static_cast<qint64>(number))
                                            : QString::number(number);
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    return QString();
}

// Looks up a dotted path such as "settings.time" inside a state JSON object.
QJsonValue jsonPath(const QJsonObject &object, const QString &path)
{
    QJsonValue current = object;
    for (const QString &part : path.split(QLatin1Char('.'), Qt::SkipEmptyParts)) {
        if (!current.isObject()) {
            return QJsonValue(QJsonValue::Undefined);
        }
        current = current.toObject().value(part);
    }
    return current;
}

QString shellQuote(const QString &value)
{
    QString quoted = value;
    quoted.replace(QStringLiteral("'"), QStringLiteral("'\"'\"'"));
    return QStringLiteral("'") + quoted + QStringLiteral("'");
}

QString displayArgument(const QString &value)
{
    return value.contains(QLatin1Char(' ')) ? QStringLiteral("\"%1\"").arg(value) : value;
}

bool isAbsoluteRemotePath(const QString &path)
{
    return path.startsWith(QLatin1Char('/')) && !path.contains(QLatin1Char('\\'));
}

QString remoteDirectory(const QString &path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    if (slash < 0) {
        return QStringLiteral(".");
    }
    if (slash == 0) return QStringLiteral("/");
    return path.left(slash);
}

QString resolveRemotePath(const QString &baseDirectory, const QString &path)
{
    if (isAbsoluteRemotePath(path)) {
        return path;
    }
    if (baseDirectory.isEmpty()) {
        return path;
    }
    return QDir(baseDirectory).filePath(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
}

// Tools may print UTF-8 (PowerShell with UTF-8 output, most SSH hosts) or the
// local ANSI code page; prefer UTF-8 when the bytes are valid UTF-8.
QString decodeOutput(const QByteArray &data)
{
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8(data);
    return utf8.hasError() ? QString::fromLocal8Bit(data) : text;
}

QString displayTime(const QString &iso)
{
    QDateTime time = QDateTime::fromString(iso, Qt::ISODateWithMs);
    if (!time.isValid()) {
        time = QDateTime::fromString(iso, Qt::ISODate);
    }
    return time.isValid() ? time.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : iso;
}

QString resultLabel(const QString &result, const QString &toolId = QString())
{
    if (result == QStringLiteral("success")) return QStringLiteral("成功");
    if (result == QStringLiteral("already")) {
        return toolId == QStringLiteral("daily-check-in") ? QStringLiteral("今天已签到") : QStringLiteral("已处理");
    }
    if (result == QStringLiteral("failure")) return QStringLiteral("失败");
    if (result == QStringLiteral("skipped")) return QStringLiteral("跳过");
    if (result == QStringLiteral("disabled")) return QStringLiteral("已停用");
    if (result == QStringLiteral("running")) return QStringLiteral("运行中");
    if (result == QStringLiteral("partial_success")) return QStringLiteral("部分成功");
    if (result == QStringLiteral("none")) return QStringLiteral("暂无状态");
    if (result == QStringLiteral("expired")) return QStringLiteral("已过期");
    return result.isEmpty() || result == QStringLiteral("unknown") ? QStringLiteral("未知") : result;
}

QColor resultColor(const QString &result)
{
    if (result == QStringLiteral("success") || result == QStringLiteral("already")) {
        return QColor(QStringLiteral("#15803d"));
    }
    if (result == QStringLiteral("failure")) {
        return QColor(QStringLiteral("#dc2626"));
    }
    if (result == QStringLiteral("skipped") || result == QStringLiteral("disabled")
        || result == QStringLiteral("none")) {
        return QColor(QStringLiteral("#64748b"));
    }
    if (result == QStringLiteral("running") || result == QStringLiteral("loading")) {
        return QColor(QStringLiteral("#2563eb"));
    }
    return QColor(QStringLiteral("#b45309"));
}

QString badgeStyle(const QString &result)
{
    QString background = QStringLiteral("#fef3c7");
    if (result == QStringLiteral("success") || result == QStringLiteral("already")) {
        background = QStringLiteral("#dcfce7");
    } else if (result == QStringLiteral("failure")) {
        background = QStringLiteral("#fee2e2");
    } else if (result == QStringLiteral("skipped") || result == QStringLiteral("disabled")
               || result == QStringLiteral("none")) {
        background = QStringLiteral("#e2e8f0");
    } else if (result == QStringLiteral("running") || result == QStringLiteral("loading")) {
        background = QStringLiteral("#dbeafe");
    }
    return QStringLiteral("color: %1; background: %2; border-radius: 11px; padding: 3px 12px;"
                          " font-weight: 600;")
        .arg(resultColor(result).name(), background);
}

QIcon statusDot(const QColor &color)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QRectF(8, 8, 16, 16));
    return QIcon(pixmap);
}

void applyToolStatus(QTreeWidgetItem *item, const QJsonObject *state, const QString *error)
{
    QString text = QStringLiteral("未读取");
    QColor color(QStringLiteral("#94a3b8"));
    if (error != nullptr) {
        text = QStringLiteral("读取失败");
        color = QColor(QStringLiteral("#dc2626"));
    } else if (state != nullptr) {
        const QString result = ToolState::displayResult(*state);
        text = resultLabel(result, jsonString(*state, QStringLiteral("tool_id")));
        color = resultColor(result);
    }
    item->setIcon(0, statusDot(color));
    item->setText(1, text);
    item->setForeground(1, color);
}

// ---- Command parameters -------------------------------------------------

const QStringList kParameterTypes {
    QStringLiteral("string"),
    QStringLiteral("integer"),
    QStringLiteral("time"),
    QStringLiteral("choice"),
    QStringLiteral("boolean")
};

QString parameterType(const QJsonObject &definition)
{
    const QString type = jsonString(definition, QStringLiteral("type"), QStringLiteral("string")).toLower();
    return kParameterTypes.contains(type) ? type : QStringLiteral("string");
}

bool validateParameters(const QString &commandName, const QJsonArray &parameters, QString *errorMessage)
{
    static const QRegularExpression validKey(QStringLiteral("^[A-Za-z0-9_.-]+$"));
    QSet<QString> keys;
    for (const QJsonValue &value : parameters) {
        const QJsonObject definition = value.toObject();
        const QString key = jsonString(definition, QStringLiteral("key")).trimmed();
        const QString type = jsonString(definition, QStringLiteral("type"), QStringLiteral("string")).toLower();
        QString problem;
        if (!value.isObject() || key.isEmpty()) {
            problem = QStringLiteral("每个参数都必须是包含 key 的对象");
        } else if (keys.contains(key)) {
            problem = QStringLiteral("参数 key“%1”重复").arg(key);
        } else if (!validKey.match(key).hasMatch()) {
            problem = QStringLiteral("参数 key 只能包含英文字母、数字、下划线、点和连字符");
        } else if (!kParameterTypes.contains(type)) {
            problem = QStringLiteral("参数“%1”的类型“%2”不受支持（可用：%3）")
                          .arg(key, type, kParameterTypes.join(QStringLiteral("、")));
        } else if (type == QStringLiteral("choice")
                   && definition.value(QStringLiteral("options")).toArray().isEmpty()) {
            problem = QStringLiteral("选择型参数“%1”必须提供 options").arg(key);
        }
        if (!problem.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("命令“%1”的参数定义无效：%2。").arg(commandName, problem);
            }
            return false;
        }
        keys.insert(key);
    }
    return true;
}

struct ParameterInput {
    QString key;
    QString type;
    QString label;
    bool required = false;
    QJsonObject definition;
    QWidget *widget = nullptr;
};

// Shows a form for the command's parameters. Defaults come from the manifest,
// or from the tool's current state when the parameter names a state_key.
bool promptParameters(QWidget *parent,
                      const AppDatabase::ToolCommand &command,
                      const QJsonObject &state,
                      QHash<QString, QString> *values)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(command.name);
    dialog.setMinimumWidth(460);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(26, 22, 26, 20);
    layout->setSpacing(12);
    layout->addWidget(makeCardTitle(command.name, &dialog));
    if (!command.description.isEmpty()) {
        layout->addWidget(makeMuted(command.description, &dialog));
    }
    if (command.highRisk) {
        auto *warning = new QLabel(QStringLiteral("该命令被工具声明为高风险，请确认参数后再运行。"), &dialog);
        warning->setObjectName(QStringLiteral("warningText"));
        warning->setWordWrap(true);
        layout->addWidget(warning);
    }

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 6, 0, 0);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(12);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    QList<ParameterInput> inputs;
    for (const QJsonValue &value : command.parameters) {
        const QJsonObject definition = value.toObject();
        ParameterInput input;
        input.key = jsonString(definition, QStringLiteral("key")).trimmed();
        if (input.key.isEmpty()) {
            continue;
        }
        input.type = parameterType(definition);
        input.label = jsonString(definition, QStringLiteral("label"), input.key);
        input.required = definition.value(QStringLiteral("required")).toBool(false);
        input.definition = definition;

        QJsonValue initial = definition.value(QStringLiteral("default"));
        const QString stateKey = jsonString(definition, QStringLiteral("state_key"));
        if (!stateKey.isEmpty()) {
            const QJsonValue fromState = jsonPath(state, stateKey);
            if (!fromState.isUndefined() && !fromState.isNull()) {
                initial = fromState;
            }
        }
        const QString initialText = jsonScalarText(initial);

        if (input.type == QStringLiteral("integer")) {
            auto *spin = new QSpinBox(&dialog);
            spin->setRange(definition.value(QStringLiteral("min")).toInt(0),
                           definition.value(QStringLiteral("max")).toInt(1000000));
            spin->setSingleStep(qMax(1, definition.value(QStringLiteral("step")).toInt(1)));
            const QString unit = jsonString(definition, QStringLiteral("unit"));
            if (!unit.isEmpty()) {
                spin->setSuffix(QStringLiteral(" ") + unit);
            }
            bool ok = false;
            const int number = initialText.toInt(&ok);
            spin->setValue(ok ? number : spin->minimum());
            input.widget = spin;
        } else if (input.type == QStringLiteral("time")) {
            auto *edit = new QTimeEdit(&dialog);
            edit->setDisplayFormat(QStringLiteral("HH:mm"));
            QTime time = QTime::fromString(initialText, QStringLiteral("HH:mm"));
            if (!time.isValid()) {
                time = QTime::fromString(initialText, QStringLiteral("H:mm"));
            }
            edit->setTime(time.isValid() ? time : QTime(0, 0));
            input.widget = edit;
        } else if (input.type == QStringLiteral("choice")) {
            auto *combo = new QComboBox(&dialog);
            for (const QJsonValue &option : definition.value(QStringLiteral("options")).toArray()) {
                if (option.isObject()) {
                    const QJsonObject optionObject = option.toObject();
                    const QString optionValue = jsonScalarText(optionObject.value(QStringLiteral("value")));
                    combo->addItem(jsonString(optionObject, QStringLiteral("label"), optionValue), optionValue);
                } else {
                    combo->addItem(jsonScalarText(option), jsonScalarText(option));
                }
            }
            const int index = combo->findData(initialText);
            if (index >= 0) {
                combo->setCurrentIndex(index);
            }
            input.widget = combo;
        } else if (input.type == QStringLiteral("boolean")) {
            auto *check = new QCheckBox(jsonString(definition, QStringLiteral("text")), &dialog);
            check->setChecked(initialText == QStringLiteral("true"));
            input.widget = check;
        } else {
            auto *edit = new QLineEdit(&dialog);
            edit->setPlaceholderText(jsonString(definition, QStringLiteral("placeholder")));
            edit->setText(initialText);
            input.widget = edit;
        }

        QWidget *field = input.widget;
        const QString help = jsonString(definition, QStringLiteral("description"));
        if (!help.isEmpty()) {
            auto *box = new QWidget(&dialog);
            auto *boxLayout = new QVBoxLayout(box);
            boxLayout->setContentsMargins(0, 0, 0, 0);
            boxLayout->setSpacing(4);
            boxLayout->addWidget(input.widget);
            auto *helpLabel = new QLabel(help, box);
            helpLabel->setObjectName(QStringLiteral("fieldHelp"));
            helpLabel->setWordWrap(true);
            boxLayout->addWidget(helpLabel);
            field = box;
        }
        form->addRow(input.label + (input.required ? QStringLiteral(" *") : QString()) + QStringLiteral("："),
                     field);
        inputs.append(input);
    }
    layout->addLayout(form);

    auto *errorLabel = new QLabel(&dialog);
    errorLabel->setObjectName(QStringLiteral("warningText"));
    errorLabel->setWordWrap(true);
    errorLabel->hide();
    layout->addWidget(errorLabel);

    layout->addSpacing(6);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancelButton = new QPushButton(QStringLiteral("取消"), &dialog);
    buttons->addWidget(cancelButton);
    auto *runButton = new QPushButton(command.highRisk ? QStringLiteral("确认运行") : QStringLiteral("运行"),
                                      &dialog);
    runButton->setObjectName(QStringLiteral("primaryButton"));
    runButton->setDefault(true);
    buttons->addWidget(runButton);
    layout->addLayout(buttons);

    QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(runButton, &QPushButton::clicked, &dialog, [&dialog, &inputs, errorLabel] {
        for (const ParameterInput &input : inputs) {
            auto *edit = qobject_cast<QLineEdit *>(input.widget);
            if (input.required && edit != nullptr && edit->text().trimmed().isEmpty()) {
                errorLabel->setText(QStringLiteral("请填写“%1”。").arg(input.label));
                errorLabel->show();
                edit->setFocus();
                return;
            }
        }
        dialog.accept();
    });

    // With word-wrapped labels QDialog picks its height for a narrower width than it is
    // finally shown at, and the spare height piles up around the title and description.
    if (layout->hasHeightForWidth()) {
        const int width = qMax(dialog.minimumWidth(), dialog.sizeHint().width());
        dialog.resize(width, layout->totalHeightForWidth(width));
    }

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    for (const ParameterInput &input : inputs) {
        QString value;
        if (auto *spin = qobject_cast<QSpinBox *>(input.widget)) {
            value = QString::number(spin->value());
        } else if (auto *timeEdit = qobject_cast<QTimeEdit *>(input.widget)) {
            value = timeEdit->time().toString(QStringLiteral("HH:mm"));
        } else if (auto *combo = qobject_cast<QComboBox *>(input.widget)) {
            value = combo->currentData().toString();
        } else if (auto *check = qobject_cast<QCheckBox *>(input.widget)) {
            value = check->isChecked()
                ? jsonString(input.definition, QStringLiteral("true_value"), QStringLiteral("true"))
                : jsonString(input.definition, QStringLiteral("false_value"), QStringLiteral("false"));
        } else if (auto *edit = qobject_cast<QLineEdit *>(input.widget)) {
            value = edit->text().trimmed();
        }
        values->insert(input.key, value);
    }
    return true;
}

QWidget *makeCommandRow(const AppDatabase::ToolCommand &command, QStyle *style, QWidget *parent)
{
    auto *row = new QWidget(parent);
    row->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(14, 9, 14, 9);
    layout->setSpacing(12);

    auto *icon = new QLabel(row);
    icon->setPixmap(style->standardIcon(QStyle::SP_MediaPlay).pixmap(18, 18));
    icon->setFixedSize(22, 22);
    icon->setAlignment(Qt::AlignCenter);
    layout->addWidget(icon);

    auto *textColumn = new QVBoxLayout;
    textColumn->setSpacing(2);
    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(6);
    auto *title = new QLabel(command.name, row);
    title->setObjectName(QStringLiteral("commandName"));
    titleRow->addWidget(title);
    if (!command.parameters.isEmpty()) {
        titleRow->addWidget(makeTag(QStringLiteral("需填参数"), QStringLiteral("tag"), row));
    }
    if (command.highRisk) {
        titleRow->addWidget(makeTag(QStringLiteral("高风险"), QStringLiteral("tagDanger"), row));
    }
    if (command.interactive) {
        titleRow->addWidget(makeTag(QStringLiteral("交互式"), QStringLiteral("tagMuted"), row));
    }
    titleRow->addStretch(1);
    textColumn->addLayout(titleRow);

    QString detail = command.description;
    if (detail.isEmpty()) {
        QStringList parts {command.executable};
        for (const QString &argument : command.arguments) {
            parts << displayArgument(argument);
        }
        detail = parts.join(QLatin1Char(' '));
    }

    auto *subtitle = new QLabel(detail, row);
    subtitle->setObjectName(QStringLiteral("commandDetail"));
    subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    textColumn->addWidget(subtitle);
    layout->addLayout(textColumn, 1);
    return row;
}

} // namespace

AutomationPage::AutomationPage(AppDatabase *database, QWidget *parent)
    : QWidget(parent)
    , database_(database)
{
    setStyleSheet(QStringLiteral(R"(
        QTreeWidget#toolTree {
            border: none;
            background: transparent;
            outline: none;
            show-decoration-selected: 0;
        }
        QTreeWidget#toolTree::item {
            padding: 7px 6px;
            border: none;
            color: #334155;
        }
        QTreeWidget#toolTree::item:hover {
            background: #f1f5fb;
        }
        QTreeWidget#toolTree::item:selected {
            background: #e0ebff;
            color: #1d4ed8;
        }
        QTreeWidget#toolTree::item:first {
            border-top-left-radius: 7px;
            border-bottom-left-radius: 7px;
        }
        QTreeWidget#toolTree::item:last {
            border-top-right-radius: 7px;
            border-bottom-right-radius: 7px;
        }
        QTreeWidget#toolTree::item:only-one {
            border-radius: 7px;
        }
        QTreeWidget#toolTree::branch {
            background: transparent;
        }
        QListWidget#commandList {
            border: none;
            background: transparent;
            padding: 0;
            outline: none;
        }
        QListWidget#commandList::item {
            border: 1px solid #e3e8f0;
            border-radius: 8px;
            background: #fbfcfe;
        }
        QListWidget#commandList::item:hover {
            background: #f3f7ff;
            border-color: #bfd3fb;
        }
        QListWidget#commandList::item:selected {
            background: #eaf1ff;
            border-color: #6ea0f7;
        }
        QLabel#toolName {
            color: #111827;
            font-size: 20px;
            font-weight: 700;
        }
        QLabel#sourceText {
            color: #94a3b8;
            font-size: 12px;
        }
        QLabel#stateSummary {
            color: #1f2937;
            font-size: 15px;
        }
        QLabel#commandName {
            color: #1f2937;
            font-size: 14px;
            font-weight: 600;
        }
        QLabel#commandDetail {
            color: #6b7280;
            font-size: 12px;
        }
        QLabel#tag, QLabel#tagDanger, QLabel#tagMuted {
            border-radius: 8px;
            padding: 1px 7px;
            font-size: 11px;
        }
        QLabel#tag {
            color: #1d4ed8;
            background: #e0ebff;
        }
        QLabel#tagDanger {
            color: #b91c1c;
            background: #fee2e2;
        }
        QLabel#tagMuted {
            color: #475569;
            background: #e2e8f0;
        }
        QFrame#stateRow {
            background: #f8fafc;
            border: 1px solid #edf1f6;
            border-radius: 8px;
        }
        QLabel#stateRowName {
            color: #1f2937;
            font-weight: 600;
        }
        QFrame#runPanel {
            background: #111827;
            border-radius: 8px;
        }
        QLabel#runTitle {
            color: #e5e7eb;
            font-weight: 600;
        }
        QPlainTextEdit#runOutput {
            background: transparent;
            border: none;
            color: #cbd5e1;
            padding: 0;
            selection-background-color: #334155;
            selection-color: #f8fafc;
        }
        QLabel#fieldHelp {
            color: #94a3b8;
            font-size: 12px;
        }
        QLabel#warningText {
            color: #92400e;
            background: #fffbeb;
            border: 1px solid #fde68a;
            border-radius: 6px;
            padding: 7px 10px;
        }
        QScrollArea#detailsScroll {
            background: transparent;
            border: none;
        }
        QWidget#detailsColumn {
            background: transparent;
        }
        QSplitter::handle {
            background: transparent;
        }
    )"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(34, 30, 34, 30);
    layout->setSpacing(16);

    layout->addWidget(makeTitle(QStringLiteral("自动化工具"), this));
    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);
    auto *newGroupButton = new QPushButton(QStringLiteral("新建工具组"), this);
    toolbar->addWidget(newGroupButton);
    renameGroupButton_ = new QPushButton(QStringLiteral("重命名工具组"), this);
    renameGroupButton_->setEnabled(false);
    toolbar->addWidget(renameGroupButton_);
    deleteGroupButton_ = new QPushButton(QStringLiteral("删除工具组"), this);
    deleteGroupButton_->setEnabled(false);
    toolbar->addWidget(deleteGroupButton_);
    toolbar->addStretch(1);
    auto *importRemoteButton = new QPushButton(QStringLiteral("注册服务器工具"), this);
    toolbar->addWidget(importRemoteButton);
    auto *importButton = new QPushButton(QStringLiteral("注册本地工具"), this);
    importButton->setObjectName(QStringLiteral("primaryButton"));
    toolbar->addWidget(importButton);
    layout->addLayout(toolbar);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(14);

    // ---- Left: groups and tools ----
    auto *treeCard = makeCard(splitter);
    auto *treeLayout = new QVBoxLayout(treeCard);
    treeLayout->setContentsMargins(16, 18, 16, 16);
    treeLayout->setSpacing(8);
    auto *treeTitle = makeCardTitle(QStringLiteral("工具组"), treeCard);
    treeTitle->setContentsMargins(4, 0, 0, 0);
    treeLayout->addWidget(treeTitle);
    groupHint_ = makeMuted(QStringLiteral("右键可重命名、移动或移除。"), treeCard);
    groupHint_->setContentsMargins(4, 0, 0, 4);
    groupHint_->hide();
    tree_ = new QTreeWidget(treeCard);
    tree_->setObjectName(QStringLiteral("toolTree"));
    tree_->setColumnCount(2);
    tree_->setHeaderHidden(true);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->setRootIsDecorated(false);
    tree_->setIndentation(18);
    tree_->setIconSize(QSize(16, 16));
    tree_->setMinimumWidth(240);
    tree_->setUniformRowHeights(true);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->setMouseTracking(true);
    treeLayout->addWidget(tree_, 1);

    // ---- Right: empty state or tool details ----
    detailsStack_ = new QStackedWidget(splitter);

    auto *emptyPage = makeCard(detailsStack_);
    auto *emptyLayout = new QVBoxLayout(emptyPage);
    emptyLayout->setContentsMargins(36, 36, 36, 36);
    emptyLayout->setSpacing(10);
    emptyLayout->addStretch(1);
    emptyTitle_ = new QLabel(emptyPage);
    emptyTitle_->setObjectName(QStringLiteral("emptyStateTitle"));
    emptyTitle_->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyTitle_);
    emptyMessage_ = makeMuted(QString(), emptyPage);
    emptyMessage_->setAlignment(Qt::AlignCenter);
    emptyMessage_->hide();
    emptyLayout->addStretch(2);
    detailsStack_->addWidget(emptyPage);

    auto *detailsScroll = new QScrollArea(detailsStack_);
    detailsScroll->setObjectName(QStringLiteral("detailsScroll"));
    detailsScroll->setFrameShape(QFrame::NoFrame);
    detailsScroll->setWidgetResizable(true);
    detailsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    detailsScroll->viewport()->setAutoFillBackground(false);
    auto *column = new QWidget(detailsScroll);
    column->setObjectName(QStringLiteral("detailsColumn"));
    column->setAutoFillBackground(false);
    auto *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 6, 0);
    columnLayout->setSpacing(14);

    // Header: identity and registration actions.
    auto *headerCard = makeCard(column);
    auto *headerLayout = new QVBoxLayout(headerCard);
    headerLayout->setContentsMargins(22, 18, 22, 18);
    headerLayout->setSpacing(6);
    auto *headerRow = new QHBoxLayout;
    headerRow->setSpacing(8);
    toolName_ = new QLabel(headerCard);
    toolName_->setObjectName(QStringLiteral("toolName"));
    headerRow->addWidget(toolName_, 1);
    reloadManifestButton_ = new QPushButton(QStringLiteral("重新读取声明"), headerCard);
    reloadManifestButton_->setToolTip(QStringLiteral("重新读取接入声明，更新命令和工具信息；分组与刷新设置保留。"));
    headerRow->addWidget(reloadManifestButton_);
    deleteToolButton_ = new QPushButton(QStringLiteral("移除注册"), headerCard);
    deleteToolButton_->setObjectName(QStringLiteral("dangerButton"));
    headerRow->addWidget(deleteToolButton_);
    headerLayout->addLayout(headerRow);
    toolDescription_ = makeMuted(QString(), headerCard);
    headerLayout->addWidget(toolDescription_);
    toolSource_ = new QLabel(headerCard);
    toolSource_->setObjectName(QStringLiteral("sourceText"));
    toolSource_->setWordWrap(true);
    toolSource_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    toolSource_->hide();
    columnLayout->addWidget(headerCard);

    // Current state.
    auto *stateCard = makeCard(column);
    auto *stateLayout = new QVBoxLayout(stateCard);
    stateLayout->setContentsMargins(22, 16, 22, 18);
    stateLayout->setSpacing(8);
    auto *stateHeader = new QHBoxLayout;
    stateHeader->addWidget(makeCardTitle(QStringLiteral("当前状态"), stateCard));
    stateHeader->addStretch(1);
    refreshButton_ = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload),
                                     QStringLiteral("刷新状态"), stateCard);
    refreshButton_->setToolTip(QStringLiteral("重新读取当前状态 JSON，不会运行工具命令。"));
    stateHeader->addWidget(refreshButton_);
    stateLayout->addLayout(stateHeader);
    auto *badgeRow = new QHBoxLayout;
    stateBadge_ = new QLabel(stateCard);
    badgeRow->addWidget(stateBadge_);
    badgeRow->addStretch(1);
    stateLayout->addLayout(badgeRow);
    stateSummary_ = new QLabel(stateCard);
    stateSummary_->setObjectName(QStringLiteral("stateSummary"));
    stateSummary_->setWordWrap(true);
    stateSummary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    stateLayout->addWidget(stateSummary_);
    stateMeta_ = new QLabel(stateCard);
    stateMeta_->setObjectName(QStringLiteral("sourceText"));
    stateLayout->addWidget(stateMeta_);
    stateItemsTitle_ = makeMuted(QStringLiteral("状态明细"), stateCard);
    stateItemsTitle_->setContentsMargins(0, 6, 0, 0);
    stateLayout->addWidget(stateItemsTitle_);
    stateItemsBox_ = new QWidget(stateCard);
    stateItemsLayout_ = new QVBoxLayout(stateItemsBox_);
    stateItemsLayout_->setContentsMargins(0, 0, 0, 0);
    stateItemsLayout_->setSpacing(6);
    stateLayout->addWidget(stateItemsBox_);
    columnLayout->addWidget(stateCard);

    // Commands and the output of the latest run.
    auto *commandCard = makeCard(column);
    auto *commandLayout = new QVBoxLayout(commandCard);
    commandLayout->setContentsMargins(22, 16, 22, 18);
    commandLayout->setSpacing(10);
    auto *commandHeader = new QHBoxLayout;
    commandHeader->addWidget(makeCardTitle(QStringLiteral("工具命令"), commandCard));
    commandHeader->addStretch(1);
    commandHint_ = new QLabel(commandCard);
    commandHint_->setObjectName(QStringLiteral("sourceText"));
    commandHeader->addWidget(commandHint_);
    commandHint_->hide();
    commandLayout->addLayout(commandHeader);
    commandList_ = new QListWidget(commandCard);
    commandList_->setObjectName(QStringLiteral("commandList"));
    commandList_->setSpacing(3);
    commandList_->setSelectionMode(QAbstractItemView::SingleSelection);
    commandList_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    commandList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    commandList_->setMouseTracking(true);
    commandLayout->addWidget(commandList_);

    runPanel_ = new QFrame(commandCard);
    runPanel_->setObjectName(QStringLiteral("runPanel"));
    auto *runLayout = new QVBoxLayout(runPanel_);
    runLayout->setContentsMargins(14, 10, 14, 10);
    runLayout->setSpacing(6);
    auto *runHeader = new QHBoxLayout;
    runTitle_ = new QLabel(runPanel_);
    runTitle_->setObjectName(QStringLiteral("runTitle"));
    runHeader->addWidget(runTitle_, 1);
    runStatus_ = new QLabel(runPanel_);
    runHeader->addWidget(runStatus_);
    runLayout->addLayout(runHeader);
    runOutput_ = new QPlainTextEdit(runPanel_);
    runOutput_->setObjectName(QStringLiteral("runOutput"));
    runOutput_->setReadOnly(true);
    runOutput_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    runOutput_->setMinimumHeight(90);
    runOutput_->setMaximumHeight(200);
    runOutput_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    runLayout->addWidget(runOutput_);
    runPanel_->hide();
    commandLayout->addWidget(runPanel_);
    columnLayout->addWidget(commandCard);

    // Status refresh settings: only expanded while enabled.
    auto *refreshCard = makeCard(column);
    auto *refreshLayout = new QVBoxLayout(refreshCard);
    refreshLayout->setContentsMargins(22, 16, 22, 18);
    refreshLayout->setSpacing(10);
    refreshLayout->addWidget(makeCardTitle(QStringLiteral("状态刷新"), refreshCard));
    refreshEnabledCheck_ = new QCheckBox(QStringLiteral("启用周期刷新"), refreshCard);
    refreshLayout->addWidget(refreshEnabledCheck_);

    refreshOptions_ = new QWidget(refreshCard);
    auto *optionsLayout = new QVBoxLayout(refreshOptions_);
    optionsLayout->setContentsMargins(26, 0, 0, 0);
    optionsLayout->setSpacing(8);
    const auto makeOptionRow = [this](const QString &label, QHBoxLayout **rowLayout) {
        auto *row = new QWidget(refreshOptions_);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        auto *caption = new QLabel(label, row);
        caption->setMinimumWidth(72);
        layout->addWidget(caption);
        *rowLayout = layout;
        return row;
    };
    QHBoxLayout *modeLayout = nullptr;
    QWidget *modeRow = makeOptionRow(QStringLiteral("刷新方式"), &modeLayout);
    refreshModeCombo_ = new QComboBox(modeRow);
    refreshModeCombo_->addItem(QStringLiteral("按间隔"), QStringLiteral("interval"));
    refreshModeCombo_->addItem(QStringLiteral("每天固定时间"), QStringLiteral("daily"));
    refreshModeCombo_->setMinimumWidth(150);
    modeLayout->addWidget(refreshModeCombo_);
    modeLayout->addStretch(1);
    optionsLayout->addWidget(modeRow);

    QHBoxLayout *intervalLayout = nullptr;
    intervalRow_ = makeOptionRow(QStringLiteral("间隔"), &intervalLayout);
    refreshIntervalCombo_ = new QComboBox(intervalRow_);
    refreshIntervalCombo_->addItem(QStringLiteral("1 分钟"), 60);
    refreshIntervalCombo_->addItem(QStringLiteral("5 分钟"), 5 * 60);
    refreshIntervalCombo_->addItem(QStringLiteral("30 分钟"), 30 * 60);
    refreshIntervalCombo_->addItem(QStringLiteral("1 小时"), 60 * 60);
    refreshIntervalCombo_->addItem(QStringLiteral("24 小时"), 24 * 60 * 60);
    refreshIntervalCombo_->addItem(QStringLiteral("自定义"), 0);
    refreshIntervalCombo_->setMinimumWidth(150);
    intervalLayout->addWidget(refreshIntervalCombo_);
    customIntervalMinutes_ = new QSpinBox(intervalRow_);
    customIntervalMinutes_->setRange(1, 10080);
    customIntervalMinutes_->setValue(15);
    customIntervalMinutes_->setSuffix(QStringLiteral(" 分钟"));
    customIntervalMinutes_->setMinimumWidth(110);
    intervalLayout->addWidget(customIntervalMinutes_);
    intervalLayout->addStretch(1);
    optionsLayout->addWidget(intervalRow_);

    QHBoxLayout *dailyLayout = nullptr;
    dailyRow_ = makeOptionRow(QStringLiteral("每天时间"), &dailyLayout);
    dailyRefreshTimeEdit_ = new QTimeEdit(QTime(8, 0), dailyRow_);
    dailyRefreshTimeEdit_->setDisplayFormat(QStringLiteral("HH:mm"));
    dailyRefreshTimeEdit_->setMinimumWidth(110);
    dailyLayout->addWidget(dailyRefreshTimeEdit_);
    dailyLayout->addStretch(1);
    optionsLayout->addWidget(dailyRow_);
    refreshLayout->addWidget(refreshOptions_);

    refreshHint_ = makeMuted(QString(), refreshCard);
    refreshHint_->hide();
    columnLayout->addWidget(refreshCard);
    columnLayout->addStretch(1);

    detailsScroll->setWidget(column);
    detailsStack_->addWidget(detailsScroll);

    splitter->addWidget(treeCard);
    splitter->addWidget(detailsStack_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({300, 900});
    layout->addWidget(splitter, 1);

    connect(newGroupButton, &QPushButton::clicked, this, &AutomationPage::createGroup);
    connect(renameGroupButton_, &QPushButton::clicked, this, &AutomationPage::renameGroup);
    connect(deleteGroupButton_, &QPushButton::clicked, this, &AutomationPage::deleteGroup);
    connect(importButton, &QPushButton::clicked, this, &AutomationPage::importTool);
    connect(importRemoteButton, &QPushButton::clicked, this, &AutomationPage::importRemoteTool);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &AutomationPage::handleTreeSelection);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &AutomationPage::showTreeContextMenu);
    connect(refreshButton_, &QPushButton::clicked, this, &AutomationPage::refreshState);
    connect(reloadManifestButton_, &QPushButton::clicked, this, &AutomationPage::reloadManifest);
    connect(deleteToolButton_, &QPushButton::clicked, this, &AutomationPage::deleteTool);
    connect(commandList_, &QListWidget::itemActivated, this, &AutomationPage::runCommand);
    connect(refreshEnabledCheck_, &QCheckBox::toggled, this, [this] {
        updateRefreshControlVisibility();
        saveRefreshSettings();
    });
    connect(refreshModeCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        updateRefreshControlVisibility();
        saveRefreshSettings();
    });
    connect(refreshIntervalCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        updateRefreshControlVisibility();
        saveRefreshSettings();
    });
    connect(customIntervalMinutes_, qOverload<int>(&QSpinBox::valueChanged), this, [this] {
        updateRefreshControlVisibility();
        saveRefreshSettings();
    });
    connect(dailyRefreshTimeEdit_, &QTimeEdit::timeChanged, this, [this] {
        updateRefreshControlVisibility();
        saveRefreshSettings();
    });

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(30000);
    connect(refreshTimer_, &QTimer::timeout, this, &AutomationPage::refreshDueStates);
    refreshTimer_->start();

    syncBuiltinTools();
    loadAll();
    refreshAllStates();
}

QString AutomationPage::ungroupedName() const
{
    const QString name = database_ != nullptr ? database_->setting(kUngroupedNameKey).trimmed() : QString();
    return name.isEmpty() ? QStringLiteral("未分组") : name;
}

bool AutomationPage::hasRunningCommands() const
{
    for (const auto &process : runningProcesses_) {
        if (process && process->state() != QProcess::NotRunning) return true;
    }
    return false;
}

int AutomationPage::builtinToolGroupId()
{
    // The group is created once, on first start. If the user later deletes it,
    // new built-in tools go to the ungrouped section instead of recreating it.
    QString error;
    const QList<AppDatabase::ToolGroup> groups = database_->toolGroups(&error);
    const QString stored = database_->setting(kBuiltinGroupKey);
    if (!stored.isEmpty()) {
        for (const AppDatabase::ToolGroup &group : groups) {
            if (group.id == stored.toInt()) {
                return group.id;
            }
        }
        return 0;
    }
    int groupId = 0;
    for (const AppDatabase::ToolGroup &group : groups) {
        if (group.name == kBuiltinGroupName) {
            groupId = group.id;
        }
    }
    if (groupId == 0 && !database_->addToolGroup(kBuiltinGroupName, &groupId, &error)) {
        return 0;
    }
    database_->setSetting(kBuiltinGroupKey, QString::number(groupId), &error);
    return groupId;
}

void AutomationPage::syncBuiltinTools()
{
    // Built-in tools ship in <program dir>/tools/<tool>/orchestrate-tool.json. Their
    // records are rewritten on every start so paths follow the program folder when
    // it is copied to another machine; group and refresh settings are kept.
    if (database_ == nullptr) {
        return;
    }
    const QDir toolsDirectory(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tools")));
    const QStringList folders = toolsDirectory.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    if (folders.isEmpty()) {
        return;
    }

    QString error;
    const QList<AppDatabase::AutomationTool> tools = database_->automationTools(0, &error);
    QStringList problems;
    for (const QString &folder : folders) {
        const QString manifestPath = QDir(toolsDirectory.filePath(folder)).filePath(QStringLiteral("orchestrate-tool.json"));
        if (!QFileInfo::exists(manifestPath)) {
            continue;
        }
        QByteArray data;
        ParsedManifest manifest;
        if (!readManifest(manifestPath, QString(), &data, &error)
            || !parseManifest(data, manifestPath, QString(), &manifest, &error)) {
            problems.append(QStringLiteral("%1：%2").arg(folder, error));
            continue;
        }
        manifest.tool.builtin = true;

        // A local tool registered by hand before it was bundled becomes the built-in one.
        const AppDatabase::AutomationTool *existing = nullptr;
        for (const AppDatabase::AutomationTool &candidate : tools) {
            if (candidate.sshHost.isEmpty() && candidate.externalId == manifest.tool.externalId
                && (existing == nullptr || candidate.builtin)) {
                existing = &candidate;
            }
        }

        bool saved = false;
        if (existing != nullptr) {
            manifest.tool.id = existing->id;
            saved = database_->saveAutomationTool(manifest.tool, manifest.commands, nullptr, &error);
            // When taking over an ungrouped hand-registered copy, put it where a fresh
            // install would; after that the user's grouping is left alone.
            if (saved && !existing->builtin && existing->groupId == 0) {
                saved = database_->setToolGroup(existing->id, builtinToolGroupId(), &error);
            }
        } else {
            AppDatabase::AutomationTool tool = manifest.tool;
            tool.groupId = builtinToolGroupId();
            int toolId = 0;
            saved = database_->saveAutomationTool(tool, manifest.commands, &toolId, &error);
        }
        if (!saved) {
            problems.append(QStringLiteral("%1：%2").arg(folder, error));
        }
    }
    if (!problems.isEmpty()) {
        showError(QStringLiteral("内置工具加载失败：\n%1").arg(problems.join(QLatin1Char('\n'))));
    }
}

bool AutomationPage::findTool(int toolId, AppDatabase::AutomationTool *tool) const
{
    if (database_ == nullptr || toolId <= 0) {
        return false;
    }
    QString error;
    const QList<AppDatabase::AutomationTool> tools = database_->automationTools(0, &error);
    for (const AppDatabase::AutomationTool &candidate : tools) {
        if (candidate.id == toolId) {
            if (tool != nullptr) {
                *tool = candidate;
            }
            return true;
        }
    }
    return false;
}

void AutomationPage::loadAll(int preferredToolId)
{
    // Keep the current selection and collapsed groups across rebuilds.
    int selectType = 0;
    int selectId = 0;
    if (preferredToolId > 0) {
        selectType = kToolItem;
        selectId = preferredToolId;
    } else if (QTreeWidgetItem *current = tree_->currentItem()) {
        selectType = current->data(0, Qt::UserRole + 1).toInt();
        selectId = current->data(0, Qt::UserRole).toInt();
    }
    QSet<int> collapsedGroups;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        if (!tree_->topLevelItem(i)->isExpanded()) {
            collapsedGroups.insert(tree_->topLevelItem(i)->data(0, Qt::UserRole).toInt());
        }
    }
    const bool preferredShownBefore = preferredToolId > 0 && preferredToolId == selectedToolId_
        && detailsStack_->currentIndex() == 1;

    {
        const QSignalBlocker blocker(tree_);
        tree_->clear();
    }

    if (database_ == nullptr) {
        showError(QStringLiteral("数据库未连接。"));
        return;
    }

    QString error;
    const QList<AppDatabase::ToolGroup> groups = database_->toolGroups(&error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }
    const QList<AppDatabase::AutomationTool> tools = database_->automationTools(0, &error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }

    QHash<int, QTreeWidgetItem *> groupItems;
    const auto addGroupItem = [this, &groupItems](int id, const QString &name, const QString &tooltip) {
        auto *item = new QTreeWidgetItem(tree_);
        item->setText(0, name);
        item->setData(0, Qt::UserRole, id);
        item->setData(0, Qt::UserRole + 1, kGroupItem);
        item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
        item->setToolTip(0, tooltip);
        QFont font = item->font(0);
        font.setWeight(QFont::DemiBold);
        item->setFont(0, font);
        item->setForeground(1, QColor(QStringLiteral("#94a3b8")));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        groupItems.insert(id, item);
    };
    for (const AppDatabase::ToolGroup &group : groups) {
        addGroupItem(group.id, group.name, QStringLiteral("工具组：%1").arg(group.name));
    }
    addGroupItem(0, ungroupedName(), QStringLiteral("尚未归入工具组的工具"));

    QHash<int, int> toolCounts;
    for (const AppDatabase::AutomationTool &tool : tools) {
        QTreeWidgetItem *parent = groupItems.value(tool.groupId, groupItems.value(0));
        auto *item = new QTreeWidgetItem(parent);
        item->setText(0, tool.name);
        item->setData(0, Qt::UserRole, tool.id);
        item->setData(0, Qt::UserRole + 1, kToolItem);
        item->setToolTip(0, tool.description.isEmpty()
                                ? QStringLiteral("已注册工具：%1").arg(tool.name)
                                : tool.description);
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        applyToolStatus(item,
                        stateCache_.contains(tool.id) ? &stateCache_[tool.id] : nullptr,
                        stateErrors_.contains(tool.id) ? &stateErrors_[tool.id] : nullptr);
        toolCounts[parent->data(0, Qt::UserRole).toInt()] += 1;
    }
    for (auto it = groupItems.cbegin(); it != groupItems.cend(); ++it) {
        const int count = toolCounts.value(it.key());
        it.value()->setText(1, count > 0 ? QString::number(count) : QString());
        it.value()->setExpanded(!collapsedGroups.contains(it.key()));
    }

    QTreeWidgetItem *target = nullptr;
    if (selectType == kGroupItem) {
        target = groupItems.value(selectId);
    } else if (selectType == kToolItem) {
        for (auto it = groupItems.cbegin(); it != groupItems.cend() && target == nullptr; ++it) {
            for (int i = 0; i < it.value()->childCount(); ++i) {
                if (it.value()->child(i)->data(0, Qt::UserRole).toInt() == selectId) {
                    target = it.value()->child(i);
                    it.value()->setExpanded(true);
                    break;
                }
            }
        }
    }
    if (target != nullptr) {
        tree_->setCurrentItem(target);
    }
    handleTreeSelection();
    // An explicitly requested tool may have new commands even if it stayed selected,
    // in which case the selection handler skipped reloading its details.
    if (preferredShownBefore && selectedToolId_ == preferredToolId) {
        loadToolDetails(preferredToolId);
    }
}

void AutomationPage::handleTreeSelection()
{
    QTreeWidgetItem *item = tree_->currentItem();
    if (item == nullptr || !item->isSelected()) {
        selectedGroupId_ = 0;
        renameGroupButton_->setEnabled(false);
        deleteGroupButton_->setEnabled(false);
        showEmptyDetails();
        return;
    }
    const int itemType = item->data(0, Qt::UserRole + 1).toInt();
    if (itemType == kGroupItem) {
        selectedGroupId_ = item->data(0, Qt::UserRole).toInt();
        renameGroupButton_->setEnabled(true);
        deleteGroupButton_->setEnabled(selectedGroupId_ > 0);
        showEmptyDetails(selectedGroupId_);
    } else if (itemType == kToolItem) {
        selectedGroupId_ = item->parent() ? item->parent()->data(0, Qt::UserRole).toInt() : 0;
        renameGroupButton_->setEnabled(false);
        deleteGroupButton_->setEnabled(false);
        const int toolId = item->data(0, Qt::UserRole).toInt();
        if (toolId != selectedToolId_ || detailsStack_->currentIndex() != 1) {
            loadToolDetails(toolId);
        }
    }
}

void AutomationPage::showTreeContextMenu(const QPoint &position)
{
    QTreeWidgetItem *item = tree_->itemAt(position);
    QMenu menu(this);
    if (item == nullptr) {
        menu.addAction(QStringLiteral("新建工具组"), this, &AutomationPage::createGroup);
        menu.exec(tree_->viewport()->mapToGlobal(position));
        return;
    }

    tree_->setCurrentItem(item);
    const int itemType = item->data(0, Qt::UserRole + 1).toInt();
    const int id = item->data(0, Qt::UserRole).toInt();
    if (itemType == kGroupItem) {
        menu.addAction(QStringLiteral("重命名工具组"), this, &AutomationPage::renameGroup);
        QAction *deleteAction = menu.addAction(QStringLiteral("删除工具组"), this, &AutomationPage::deleteGroup);
        deleteAction->setEnabled(id > 0);
        menu.addSeparator();
        menu.addAction(QStringLiteral("新建工具组"), this, &AutomationPage::createGroup);
    } else {
        menu.addAction(QStringLiteral("刷新状态"), this, &AutomationPage::refreshState);
        menu.addAction(QStringLiteral("重新读取声明"), this, &AutomationPage::reloadManifest);
        QMenu *moveMenu = menu.addMenu(QStringLiteral("移动到工具组"));
        const int currentGroup = item->parent() ? item->parent()->data(0, Qt::UserRole).toInt() : 0;
        QString error;
        QList<QPair<int, QString>> targets;
        for (const AppDatabase::ToolGroup &group : database_->toolGroups(&error)) {
            targets.append({group.id, group.name});
        }
        targets.append({0, ungroupedName()});
        for (const auto &target : targets) {
            QAction *action = moveMenu->addAction(target.second);
            action->setCheckable(true);
            action->setChecked(target.first == currentGroup);
            action->setEnabled(target.first != currentGroup);
            const int groupId = target.first;
            connect(action, &QAction::triggered, this, [this, id, groupId] {
                moveToolToGroup(id, groupId);
            });
        }
        AppDatabase::AutomationTool tool;
        if (findTool(id, &tool) && !tool.builtin) {
            menu.addSeparator();
            menu.addAction(QStringLiteral("移除注册"), this, &AutomationPage::deleteTool);
        }
    }
    menu.exec(tree_->viewport()->mapToGlobal(position));
}

void AutomationPage::updateToolIcon(int toolId)
{
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = tree_->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            QTreeWidgetItem *item = group->child(j);
            if (item->data(0, Qt::UserRole).toInt() == toolId) {
                applyToolStatus(item,
                                stateCache_.contains(toolId) ? &stateCache_[toolId] : nullptr,
                                stateErrors_.contains(toolId) ? &stateErrors_[toolId] : nullptr);
                return;
            }
        }
    }
}

void AutomationPage::showEmptyDetails(int groupId)
{
    selectedToolId_ = 0;
    detailsStack_->setCurrentIndex(0);
    if (groupId < 0) {
        emptyTitle_->setText(QStringLiteral("选择一个工具"));
        emptyMessage_->setText(QStringLiteral("在左侧选择工具后，这里会显示它的当前状态、可运行的命令和刷新设置。\n"
                                              "还没有工具时，点击右上角“注册本地工具”选择 orchestrate-tool.json。"));
        return;
    }
    QTreeWidgetItem *item = tree_->currentItem();
    const int count = item != nullptr ? item->childCount() : 0;
    emptyTitle_->setText(item != nullptr ? item->text(0) : ungroupedName());
    emptyMessage_->setText(count > 0
                               ? QStringLiteral("该组有 %1 个工具，在左侧选择一个工具查看详情。").arg(count)
                               : QStringLiteral("该组还没有工具。选中它后注册本地工具，新工具会直接放入这个组。"));
}

void AutomationPage::loadToolDetails(int toolId)
{
    AppDatabase::AutomationTool tool;
    if (!findTool(toolId, &tool)) {
        showEmptyDetails();
        return;
    }
    selectedToolId_ = tool.id;
    detailsStack_->setCurrentIndex(1);

    toolName_->setText(tool.name);
    toolDescription_->setText(tool.description.isEmpty() ? QStringLiteral("未填写工具作用。") : tool.description);
    if (tool.builtin) {
        toolSource_->setText(QStringLiteral("内置工具 · %1").arg(QDir::toNativeSeparators(
            QDir(QCoreApplication::applicationDirPath()).relativeFilePath(tool.registrationPath))));
    } else {
        toolSource_->setText(tool.sshHost.isEmpty()
                                 ? QStringLiteral("本地工具 · %1").arg(QDir::toNativeSeparators(tool.registrationPath))
                                 : QStringLiteral("服务器 %1 · %2").arg(tool.sshHost, tool.registrationPath));
    }
    deleteToolButton_->setVisible(!tool.builtin);
    configureRefreshControls(tool);

    commandList_->clear();
    QString error;
    const QList<AppDatabase::ToolCommand> commands = database_->toolCommands(tool.id, &error);
    if (!error.isEmpty()) {
        showError(error);
    }
    for (const AppDatabase::ToolCommand &command : commands) {
        auto *item = new QListWidgetItem(commandList_);
        item->setData(Qt::UserRole, command.id);
        item->setData(Qt::AccessibleTextRole, command.name);
        QStringList parts {command.executable};
        for (const QString &argument : command.arguments) {
            parts << displayArgument(argument);
        }
        item->setToolTip(QStringLiteral("双击运行\n\n%1").arg(parts.join(QLatin1Char(' '))));
        QWidget *row = makeCommandRow(command, style(), commandList_);
        item->setSizeHint(QSize(0, row->sizeHint().height()));
        commandList_->setItemWidget(item, row);
    }
    if (commands.isEmpty()) {
        auto *item = new QListWidgetItem(QStringLiteral("接入声明没有提供命令。"), commandList_);
        item->setFlags(Qt::NoItemFlags);
        item->setSizeHint(QSize(0, 44));
    }
    int listHeight = 0;
    for (int i = 0; i < commandList_->count(); ++i) {
        listHeight += commandList_->sizeHintForRow(i) + 2 * commandList_->spacing();
    }
    commandList_->setFixedHeight(listHeight + 2 * commandList_->frameWidth());
    commandHint_->setText(commands.isEmpty() ? QString() : QStringLiteral("双击命令即可运行"));
    renderRunRecord();

    if (stateCache_.contains(tool.id)) {
        renderState(stateCache_.value(tool.id));
    } else if (stateErrors_.contains(tool.id)) {
        renderStateMessage(QStringLiteral("读取失败"), QStringLiteral("failure"), stateErrors_.value(tool.id));
    } else {
        renderStateMessage(QStringLiteral("读取中"), QStringLiteral("loading"), QStringLiteral("正在读取当前状态…"));
    }
    refreshStateForTool(tool.id, true);
}

void AutomationPage::createGroup()
{
    bool accepted = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("新建工具组"), QStringLiteral("组名称："),
                                                QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || name.isEmpty() || database_ == nullptr) {
        return;
    }
    if (name == ungroupedName()) {
        showError(QStringLiteral("“%1”已用作未分组工具的名称。").arg(name));
        return;
    }
    QString error;
    int groupId = 0;
    if (!database_->addToolGroup(name, &groupId, &error)) {
        showError(error);
        return;
    }
    loadAll();
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        if (tree_->topLevelItem(i)->data(0, Qt::UserRole).toInt() == groupId) {
            tree_->setCurrentItem(tree_->topLevelItem(i));
            break;
        }
    }
}

void AutomationPage::renameGroup()
{
    QTreeWidgetItem *item = tree_->currentItem();
    if (database_ == nullptr || item == nullptr || item->data(0, Qt::UserRole + 1).toInt() != kGroupItem) {
        return;
    }
    const int groupId = item->data(0, Qt::UserRole).toInt();
    bool accepted = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("重命名工具组"), QStringLiteral("组名称："),
                                                QLineEdit::Normal, item->text(0), &accepted).trimmed();
    if (!accepted || name.isEmpty() || name == item->text(0)) {
        return;
    }

    QString error;
    for (const AppDatabase::ToolGroup &group : database_->toolGroups(&error)) {
        if (group.id != groupId && group.name == name) {
            showError(QStringLiteral("已经存在名为“%1”的工具组。").arg(name));
            return;
        }
    }
    if (groupId > 0 && name == ungroupedName()) {
        showError(QStringLiteral("“%1”已用作未分组工具的名称。").arg(name));
        return;
    }

    // The ungrouped bucket is not a stored group; its display name is a local setting.
    const bool saved = groupId > 0 ? database_->renameToolGroup(groupId, name, &error)
                                   : database_->setSetting(kUngroupedNameKey, name, &error);
    if (!saved) {
        showError(error);
        return;
    }
    loadAll();
}

void AutomationPage::deleteGroup()
{
    if (selectedGroupId_ <= 0 || database_ == nullptr) {
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("删除工具组"),
                              QStringLiteral("删除工具组不会删除工具，组内工具会移到“%1”。确定继续吗？")
                                  .arg(ungroupedName()),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!database_->deleteToolGroup(selectedGroupId_, &error)) {
        showError(error);
        return;
    }
    tree_->setCurrentItem(nullptr);
    loadAll();
}

void AutomationPage::importTool()
{
    if (database_ == nullptr) {
        return;
    }

    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择工具接入声明（orchestrate-tool.json）"),
        QString(),
        QStringLiteral("工具接入声明 (orchestrate-tool.json);;JSON 文件 (*.json);;所有文件 (*.*)"));
    if (filePath.isEmpty()) {
        return;
    }

    QByteArray data;
    QString error;
    if (!readManifest(filePath, QString(), &data, &error)) {
        showError(QStringLiteral("无法读取接入声明：%1").arg(error));
        return;
    }
    registerToolManifest(data, filePath);
}

void AutomationPage::importRemoteTool()
{
    if (database_ == nullptr) {
        return;
    }

    bool accepted = false;
    const QString host = QInputDialog::getText(
                             this,
                             QStringLiteral("注册服务器工具"),
                             QStringLiteral("SSH 主机别名（例如 jdcloud）："),
                             QLineEdit::Normal,
                             QString(),
                             &accepted)
                             .trimmed();
    if (!accepted || host.isEmpty()) {
        return;
    }
    if (host.startsWith(QLatin1Char('-')) || host.contains(QRegularExpression(QStringLiteral("\\s")))) {
        showError(QStringLiteral("SSH 主机应为单个主机别名或 user@host，不能包含命令选项或空白。"));
        return;
    }

    const QString manifestPath = QInputDialog::getText(
                                     this,
                                     QStringLiteral("注册服务器工具"),
                                     QStringLiteral("服务器上的接入声明路径（建议使用绝对路径）："),
                                     QLineEdit::Normal,
                                     QStringLiteral("/home/lingling/work/automation/lottery/orchestrate-tool.json"),
                                     &accepted)
                                     .trimmed();
    if (!accepted || manifestPath.isEmpty()) {
        return;
    }
    if (!isAbsoluteRemotePath(manifestPath)) {
        showError(QStringLiteral("服务器接入声明路径必须是以 / 开头的绝对路径。"));
        return;
    }

    QByteArray manifestData;
    QString error;
    if (!readManifest(manifestPath, host, &manifestData, &error)) {
        showError(QStringLiteral("无法读取服务器接入声明：%1").arg(error));
        return;
    }
    registerToolManifest(manifestData, manifestPath, host);
}

bool AutomationPage::readManifest(const QString &sourcePath,
                                  const QString &sshHost,
                                  QByteArray *data,
                                  QString *errorMessage) const
{
    if (!sshHost.isEmpty()) {
        return runSshCapture(sshHost,
                             {QStringLiteral("cat"), QStringLiteral("--"), shellQuote(sourcePath)},
                             data,
                             errorMessage);
    }
    QFile file(sourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    *data = file.readAll();
    return true;
}

bool AutomationPage::parseManifest(const QByteArray &manifestData,
                                   const QString &sourcePath,
                                   const QString &sshHost,
                                   ParsedManifest *manifest,
                                   QString *errorMessage) const
{
    const auto fail = [errorMessage](const QString &message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(QStringLiteral("工具接入声明不是有效的 JSON：%1").arg(parseError.errorString()));
    }

    const QJsonObject object = document.object();
    const QString schema = jsonString(object, QStringLiteral("schema"));
    const QString externalId = jsonString(object, QStringLiteral("id"));
    const QString name = jsonString(object, QStringLiteral("name"));
    const QString targetType = jsonString(object, QStringLiteral("target_type"));
    const QString statePath = jsonString(object, QStringLiteral("state_path"));
    if (schema != QStringLiteral("orchestrate-tool/v1") || externalId.trimmed().isEmpty()
        || name.trimmed().isEmpty() || targetType.isEmpty() || statePath.trimmed().isEmpty()) {
        return fail(QStringLiteral("接入声明必须包含 schema=orchestrate-tool/v1，以及 id、name、target_type、state_path。"));
    }

    const bool remote = !sshHost.isEmpty();
    if (targetType != (remote ? QStringLiteral("ssh") : QStringLiteral("windows-local"))) {
        return fail(remote ? QStringLiteral("服务器工具的 target_type 必须是 ssh。")
                           : QStringLiteral("本地工具的 target_type 必须是 windows-local；WSL 尚未支持，ssh 工具请使用“注册服务器工具”。"));
    }
    if (remote && (!isAbsoluteRemotePath(sourcePath) || statePath.contains(QLatin1Char('\\')))) {
        return fail(QStringLiteral("服务器路径须使用 / 分隔，接入声明路径必须是绝对路径。"));
    }
    if (object.contains(QStringLiteral("commands")) && !object.value(QStringLiteral("commands")).isArray()) {
        return fail(QStringLiteral("commands 必须是数组；只读监测工具请使用空数组 []。"));
    }

    const QDir manifestDirectory = QFileInfo(sourcePath).dir();
    QString workingDirectory = jsonString(object, QStringLiteral("working_directory")).trimmed();
    if (remote) {
        if (workingDirectory.isEmpty()) {
            workingDirectory = remoteDirectory(sourcePath);
        }
        if (!isAbsoluteRemotePath(workingDirectory)) {
            return fail(QStringLiteral("服务器工具必须提供以 / 开头的 working_directory。"));
        }
    } else if (!workingDirectory.isEmpty() && QFileInfo(workingDirectory).isRelative()) {
        workingDirectory = QDir::cleanPath(manifestDirectory.filePath(workingDirectory));
    }

    AppDatabase::AutomationTool &tool = manifest->tool;
    tool.externalId = externalId;
    tool.name = name;
    tool.description = jsonString(object, QStringLiteral("description"));
    tool.targetType = targetType;
    tool.registrationPath = sourcePath;
    tool.sshHost = sshHost;
    tool.workingDirectory = workingDirectory;
    tool.statePath = remote
        ? resolveRemotePath(workingDirectory, statePath)
        : (QFileInfo(statePath).isAbsolute() ? statePath : manifestDirectory.filePath(statePath));
    manifest->group = jsonString(object, QStringLiteral("group")).trimmed();

    manifest->commands.clear();
    for (const QJsonValue &value : object.value(QStringLiteral("commands")).toArray()) {
        if (!value.isObject()) {
            return fail(QStringLiteral("commands 中的每一项都必须是命令对象。"));
        }
        const QJsonObject commandObject = value.toObject();
        AppDatabase::ToolCommand command;
        command.name = jsonString(commandObject, QStringLiteral("name"));
        command.description = jsonString(commandObject, QStringLiteral("description"));
        command.executable = jsonString(commandObject, QStringLiteral("executable"));
        for (const auto &key : {QStringLiteral("arguments"), QStringLiteral("parameters")}) {
            if (commandObject.contains(key) && !commandObject.value(key).isArray()) {
                return fail(QStringLiteral("命令“%1”的 %2 必须是数组。").arg(command.name, key));
            }
        }
        for (const auto &key : {QStringLiteral("interactive"), QStringLiteral("high_risk")}) {
            if (commandObject.contains(key) && !commandObject.value(key).isBool()) {
                return fail(QStringLiteral("命令“%1”的 %2 必须是 JSON 布尔值。").arg(command.name, key));
            }
        }
        for (const QJsonValue &argument : commandObject.value(QStringLiteral("arguments")).toArray()) {
            if (!argument.isString()) return fail(QStringLiteral("命令“%1”的 arguments 只能包含字符串。").arg(command.name));
            command.arguments.append(argument.toString());
        }
        command.parameters = commandObject.value(QStringLiteral("parameters")).toArray();
        command.interactive = commandObject.value(QStringLiteral("interactive")).toBool(false);
        command.highRisk = commandObject.value(QStringLiteral("high_risk")).toBool(false);
        if (command.name.trimmed().isEmpty() || command.executable.trimmed().isEmpty()) {
            return fail(QStringLiteral("每条命令都必须提供非空 name 和 executable。"));
        }
        if (!validateParameters(command.name, command.parameters, errorMessage)) {
            return false;
        }
        manifest->commands.append(command);
    }
    return true;
}

void AutomationPage::registerToolManifest(const QByteArray &manifestData,
                                           const QString &sourcePath,
                                           const QString &sshHost)
{
    if (database_ == nullptr) {
        return;
    }

    ParsedManifest manifest;
    QString error;
    if (!parseManifest(manifestData, sourcePath, sshHost, &manifest, &error)) {
        showError(error);
        return;
    }

    // Registering the same tool again updates it instead of creating a duplicate.
    const QList<AppDatabase::AutomationTool> tools = database_->automationTools(0, &error);
    for (const AppDatabase::AutomationTool &existing : tools) {
        if (existing.externalId != manifest.tool.externalId || existing.sshHost != sshHost) {
            continue;
        }
        if (QMessageBox::question(this, QStringLiteral("工具已注册"),
                                  QStringLiteral("“%1”已经注册过。要用这份声明更新它的命令和信息吗？\n"
                                                 "分组和刷新设置会保留。")
                                      .arg(existing.name),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes)
            != QMessageBox::Yes) {
            return;
        }
        if (applyManifestUpdate(existing.id, manifest)) {
            loadAll(existing.id);
        }
        return;
    }

    QString groupNotice;
    int groupId = selectedGroupId_ > 0 ? selectedGroupId_ : 0;
    if (groupId == 0 && !manifest.group.isEmpty()) {
        for (const AppDatabase::ToolGroup &group : database_->toolGroups(&error)) {
            if (group.name == manifest.group) {
                groupId = group.id;
                break;
            }
        }
        if (groupId == 0) {
            groupNotice = QStringLiteral("接入声明中的工具组“%1”尚未创建，工具已先放入“%2”。")
                              .arg(manifest.group, ungroupedName());
        }
    }

    AppDatabase::AutomationTool tool = manifest.tool;
    tool.groupId = groupId;
    // Refresh settings belong to Orchestrate's local database, not the tool manifest.
    // New registrations are deliberately disabled until the user enables them.
    tool.refreshEnabled = false;
    tool.refreshMode = QStringLiteral("interval");
    tool.refreshIntervalSeconds = 5 * 60;
    tool.dailyRefreshTime = QStringLiteral("08:00");

    int toolId = 0;
    if (!database_->saveAutomationTool(tool, manifest.commands, &toolId, &error)) {
        showError(error);
        loadAll();
        return;
    }

    loadAll(toolId);
    if (!groupNotice.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("工具已注册"), groupNotice);
    }
}

bool AutomationPage::applyManifestUpdate(int toolId, ParsedManifest manifest)
{
    QString error;
    manifest.tool.id = toolId;
    if (!database_->saveAutomationTool(manifest.tool, manifest.commands, nullptr, &error)) {
        showError(error);
        return false;
    }
    stateCache_.remove(toolId);
    stateErrors_.remove(toolId);
    return true;
}

void AutomationPage::reloadManifest()
{
    AppDatabase::AutomationTool tool;
    if (!findTool(selectedToolId_, &tool)) {
        return;
    }
    QByteArray data;
    QString error;
    ParsedManifest manifest;
    if (!readManifest(tool.registrationPath, tool.sshHost, &data, &error)) {
        showError(QStringLiteral("无法读取接入声明：%1").arg(error));
        return;
    }
    if (!parseManifest(data, tool.registrationPath, tool.sshHost, &manifest, &error)) {
        showError(error);
        return;
    }
    if (manifest.tool.externalId != tool.externalId
        && QMessageBox::question(this, QStringLiteral("工具标识已变化"),
                                 QStringLiteral("声明中的工具 id 从“%1”变成了“%2”。仍然用它更新当前工具吗？")
                                     .arg(tool.externalId, manifest.tool.externalId),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
               != QMessageBox::Yes) {
        return;
    }
    const int commandCount = manifest.commands.size();
    if (!applyManifestUpdate(tool.id, manifest)) {
        return;
    }
    loadAll(tool.id);
    commandHint_->setText(QStringLiteral("已重新读取声明，共 %1 条命令").arg(commandCount));
    commandHint_->show();
    QTimer::singleShot(4000, this, [this] {
        if (selectedToolId_ > 0 && commandList_->count() > 0) {
            commandHint_->hide();
        }
    });
}

void AutomationPage::moveToolToGroup(int toolId, int groupId)
{
    QString error;
    if (database_ == nullptr || !database_->setToolGroup(toolId, groupId, &error)) {
        showError(error);
        return;
    }
    loadAll(toolId);
}

bool AutomationPage::runSshCapture(const QString &host,
                                    const QStringList &arguments,
                                    QByteArray *output,
                                    QString *errorMessage) const
{
    QProcess process;
    process.setProgram(QStringLiteral("ssh"));
    QStringList sshArguments {
        QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
        QStringLiteral("-o"), QStringLiteral("ConnectTimeout=10"),
        host,
        arguments.join(QLatin1Char(' '))
    };
    process.setArguments(sshArguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(5000)) {
        if (errorMessage != nullptr) {
            *errorMessage = process.errorString();
        }
        return false;
    }
    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished(2000);
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("SSH 命令超时。");
        }
        return false;
    }
    const QByteArray data = process.readAllStandardOutput();
    const QByteArray errorData = process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage != nullptr) {
            *errorMessage = decodeOutput(errorData).trimmed();
            if (errorMessage->isEmpty()) {
                *errorMessage = decodeOutput(data).trimmed();
            }
            if (errorMessage->isEmpty()) {
                *errorMessage = QStringLiteral("SSH 命令失败（退出码 %1）。").arg(process.exitCode());
            }
        }
        return false;
    }
    if (output != nullptr) {
        *output = data;
    }
    return true;
}

void AutomationPage::deleteTool()
{
    AppDatabase::AutomationTool tool;
    if (database_ == nullptr || !findTool(selectedToolId_, &tool) || tool.builtin) {
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("移除工具注册"),
                              QStringLiteral("只会移除 Orchestrate 中的注册关系，不会删除工具文件。确定继续吗？"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }
    const int toolId = selectedToolId_;
    QString error;
    if (!database_->deleteAutomationTool(toolId, &error)) {
        showError(error);
        return;
    }
    stateCache_.remove(toolId);
    stateErrors_.remove(toolId);
    runRecords_.remove(toolId);
    lastAutoRefreshAt_.remove(toolId);
    tree_->setCurrentItem(nullptr);
    loadAll();
}

bool AutomationPage::readStateFile(const QString &path, const QString &toolId, QJsonObject *state, QString *errorMessage) const
{
    // A tool that has never run (e.g. a freshly shipped built-in tool) has no state
    // file yet; that is not a read error.
    if (!QFileInfo::exists(path)) {
        if (state != nullptr) {
            *state = QJsonObject {
                {QStringLiteral("result"), QStringLiteral("none")},
                {QStringLiteral("summary"), QStringLiteral("还没有状态文件，运行任意一条命令后生成。")}
            };
        }
        return true;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1\n%2").arg(file.errorString(), QDir::toNativeSeparators(path));
        }
        return false;
    }

    return ToolState::parse(file.read(ToolState::maxBytes + 1), toolId, state, errorMessage);
}

void AutomationPage::clearStateItems()
{
    while (QLayoutItem *item = stateItemsLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    stateItemsTitle_->hide();
    stateItemsBox_->hide();
}

void AutomationPage::renderState(const QJsonObject &state)
{
    const QString result = ToolState::displayResult(state);
    const QString toolId = jsonString(state, QStringLiteral("tool_id"));
    const QString summary = jsonString(state, QStringLiteral("summary"));
    const QString updatedAt = jsonString(state, QStringLiteral("updated_at"));
    const QString currentDate = jsonString(state, QStringLiteral("current_date"));

    stateBadge_->setText(QStringLiteral("● %1").arg(resultLabel(result, toolId)));
    stateBadge_->setStyleSheet(badgeStyle(result));
    stateSummary_->setText(summary.isEmpty() ? QStringLiteral("状态文件没有提供摘要。") : summary);
    if (ToolState::expired(state))
        stateSummary_->setText(QStringLiteral("这份状态已超过工具声明的有效期，请刷新或检查工具。上次摘要：") + summary);
    QStringList meta;
    meta << QStringLiteral("更新于 %1").arg(updatedAt.isEmpty() ? QStringLiteral("未知时间") : displayTime(updatedAt));
    if (!currentDate.isEmpty()) {
        meta << QStringLiteral("状态日期 %1").arg(currentDate);
    }
    stateMeta_->setText(meta.join(QStringLiteral("  ·  ")));
    stateMeta_->show();

    clearStateItems();
    const QJsonArray items = state.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject itemObject = value.toObject();
        const QString itemResult = jsonString(itemObject, QStringLiteral("result"));
        auto *row = new QFrame(stateItemsBox_);
        row->setObjectName(QStringLiteral("stateRow"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 8, 12, 8);
        rowLayout->setSpacing(10);
        auto *name = new QLabel(jsonString(itemObject, QStringLiteral("name"), QStringLiteral("未命名")), row);
        name->setObjectName(QStringLiteral("stateRowName"));
        name->setMinimumWidth(96);
        rowLayout->addWidget(name);
        auto *itemBadge = new QLabel(resultLabel(itemResult, toolId), row);
        itemBadge->setStyleSheet(badgeStyle(itemResult)
                                 + QStringLiteral("font-size: 12px; padding: 2px 9px; border-radius: 9px;"));
        itemBadge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        rowLayout->addWidget(itemBadge);
        auto *itemSummary = makeMuted(jsonString(itemObject, QStringLiteral("summary")), row);
        itemSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
        rowLayout->addWidget(itemSummary, 1);
        stateItemsLayout_->addWidget(row);
    }
    stateItemsTitle_->setVisible(stateItemsLayout_->count() > 0);
    stateItemsBox_->setVisible(stateItemsLayout_->count() > 0);
}

void AutomationPage::renderStateMessage(const QString &badge, const QString &result, const QString &message)
{
    stateBadge_->setText(QStringLiteral("● %1").arg(badge));
    stateBadge_->setStyleSheet(badgeStyle(result));
    stateSummary_->setText(message);
    stateMeta_->clear();
    stateMeta_->hide();
    clearStateItems();
}

bool AutomationPage::readToolState(const AppDatabase::AutomationTool &tool,
                                    QJsonObject *state,
                                    QString *errorMessage) const
{
    if (tool.sshHost.isEmpty()) {
        return readStateFile(tool.statePath, tool.externalId, state, errorMessage);
    }

    QByteArray data;
    if (!runSshCapture(tool.sshHost,
                       {QStringLiteral("cat"), QStringLiteral("--"), shellQuote(tool.statePath)},
                       &data,
                       errorMessage)) {
        return false;
    }

    return ToolState::parse(data, tool.externalId, state, errorMessage);
}

void AutomationPage::applyStateResult(int toolId,
                                      bool success,
                                      const QJsonObject &state,
                                      const QString &error,
                                      bool updateVisibleState)
{
    if (success) {
        stateCache_.insert(toolId, state);
        stateErrors_.remove(toolId);
    } else {
        stateCache_.remove(toolId);
        stateErrors_.insert(toolId, error);
    }
    updateToolIcon(toolId);
    if (updateVisibleState && selectedToolId_ == toolId) {
        if (success) {
            renderState(state);
        } else {
            renderStateMessage(QStringLiteral("读取失败"), QStringLiteral("failure"), error);
        }
    }
}

void AutomationPage::startAsyncSshStateRead(const AppDatabase::AutomationTool &tool,
                                            bool updateVisibleState)
{
    if (stateReadsInFlight_.contains(tool.id)) {
        return;
    }

    auto *process = new QProcess(this);
    process->setProgram(QStringLiteral("ssh"));
    process->setArguments({
        QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
        QStringLiteral("-o"), QStringLiteral("ConnectTimeout=10"),
        tool.sshHost,
        QStringLiteral("cat -- %1").arg(shellQuote(tool.statePath))
    });
    process->setProcessChannelMode(QProcess::SeparateChannels);
    startStateReadProcess(tool, updateVisibleState, process);
}

void AutomationPage::startStateReadProcess(const AppDatabase::AutomationTool &tool,
                                            bool updateVisibleState, QProcess *process, int timeoutMs)
{
    process->setParent(this);
    if (stateReadsInFlight_.contains(tool.id)) {
        process->deleteLater();
        return;
    }
    stateReadsInFlight_.insert(tool.id);
    if (updateVisibleState && selectedToolId_ == tool.id && !stateCache_.contains(tool.id))
        renderStateMessage(QStringLiteral("读取中"), QStringLiteral("loading"), QStringLiteral("正在通过 SSH 读取服务器状态…"));

    struct ReadBuffer { QByteArray output; QByteArray errors; bool completed = false; };
    const auto buffer = std::make_shared<ReadBuffer>();
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    const auto complete = [this, tool, updateVisibleState, process, timer, buffer]
                          (bool success, const QJsonObject &state, const QString &error) {
        if (buffer->completed) return;
        buffer->completed = true;
        timer->stop();
        stateReadsInFlight_.remove(tool.id);
        if (process->state() != QProcess::NotRunning) process->kill();
        else process->deleteLater();
        // A removed or reconfigured registration must not receive an old request's result.
        AppDatabase::AutomationTool current;
        if (findTool(tool.id, &current) && current.externalId == tool.externalId
            && current.statePath == tool.statePath && current.sshHost == tool.sshHost)
            applyStateResult(tool.id, success, state, error, updateVisibleState);
        else if (current.id > 0)
            refreshStateForTool(current.id, updateVisibleState);
    };
    const auto read = [process, buffer, complete] {
        if (buffer->completed) return;
        for (auto channel : {QProcess::StandardOutput, QProcess::StandardError}) {
            process->setReadChannel(channel);
            auto &destination = channel == QProcess::StandardOutput ? buffer->output : buffer->errors;
            while (process->bytesAvailable() > 0) {
                destination += process->read(qMin<qint64>(64 * 1024, ToolState::maxBytes + 1 - destination.size()));
                if (destination.size() > ToolState::maxBytes) {
                    complete(false, {}, QStringLiteral("SSH 状态读取超过 1 MiB 上限。"));
                    return;
                }
            }
        }
    };
    connect(process, &QProcess::readyReadStandardOutput, this, read);
    connect(process, &QProcess::readyReadStandardError, this, read);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [process, buffer, read, complete, tool](int exitCode, QProcess::ExitStatus exitStatus) {
        if (buffer->completed) { process->deleteLater(); return; }
        read();
        if (buffer->completed) return;
        QJsonObject state;
        QString error;
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            error = decodeOutput(buffer->errors).trimmed();
            if (error.isEmpty()) error = QStringLiteral("SSH 命令失败（退出码 %1）。").arg(exitCode);
            complete(false, {}, error);
            return;
        }
        const bool success = ToolState::parse(buffer->output, tool.externalId, &state, &error);
        complete(success, state, error);
    });
    connect(process, &QProcess::errorOccurred, this, [process, complete](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete(false, {}, process->errorString());
    });
    connect(timer, &QTimer::timeout, this, [complete] {
        complete(false, {}, QStringLiteral("SSH 状态读取超时，已结束本次读取；可重新刷新。"));
    });
    timer->start(timeoutMs);
    process->start();
}

void AutomationPage::refreshStateForTool(int toolId, bool updateVisibleState)
{
    AppDatabase::AutomationTool tool;
    if (!findTool(toolId, &tool)) {
        return;
    }
    if (!tool.sshHost.isEmpty()) {
        startAsyncSshStateRead(tool, updateVisibleState);
        return;
    }
    QJsonObject state;
    QString error;
    const bool success = readToolState(tool, &state, &error);
    applyStateResult(tool.id, success, state, error, updateVisibleState);
}

void AutomationPage::refreshState()
{
    if (selectedToolId_ > 0) {
        refreshStateForTool(selectedToolId_, true);
    }
}

void AutomationPage::refreshAllStates()
{
    if (database_ == nullptr) {
        return;
    }
    QString error;
    const QList<AppDatabase::AutomationTool> tools = database_->automationTools(0, &error);
    if (!error.isEmpty()) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    for (const AppDatabase::AutomationTool &tool : tools) {
        refreshStateForTool(tool.id, tool.id == selectedToolId_);
        if (tool.refreshEnabled) {
            lastAutoRefreshAt_.insert(tool.id, now);
        }
    }
}

void AutomationPage::refreshDueStates()
{
    if (database_ == nullptr) {
        return;
    }
    QString error;
    const QList<AppDatabase::AutomationTool> tools = database_->automationTools(0, &error);
    if (!error.isEmpty()) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    // Expiry also applies to cached snapshots when polling is disabled.
    for (auto it = stateCache_.cbegin(); it != stateCache_.cend(); ++it) {
        if (ToolState::expired(it.value(), now)) {
            updateToolIcon(it.key());
            if (it.key() == selectedToolId_) renderState(it.value());
        }
    }
    for (const AppDatabase::AutomationTool &tool : tools) {
        if (!tool.refreshEnabled) {
            lastAutoRefreshAt_.remove(tool.id);
            continue;
        }
        bool due = false;
        const QDateTime last = lastAutoRefreshAt_.value(tool.id);
        if (tool.refreshMode == QStringLiteral("daily")) {
            const QTime target = QTime::fromString(tool.dailyRefreshTime, QStringLiteral("HH:mm"));
            if (target.isValid()) {
                const QDateTime targetToday(now.date(), target);
                // Startup refresh is separate from the daily schedule: if the app
                // starts before today's target, today's scheduled refresh remains due.
                due = now >= targetToday && (!last.isValid() || last < targetToday);
            }
        } else {
            const int seconds = qMax(60, tool.refreshIntervalSeconds);
            due = !last.isValid() || last.secsTo(now) >= seconds;
        }
        if (due) {
            refreshStateForTool(tool.id, tool.id == selectedToolId_);
            lastAutoRefreshAt_.insert(tool.id, now);
        }
    }
}

void AutomationPage::configureRefreshControls(const AppDatabase::AutomationTool &tool)
{
    loadingRefreshSettings_ = true;
    {
        const QSignalBlocker enabledBlocker(refreshEnabledCheck_);
        const QSignalBlocker modeBlocker(refreshModeCombo_);
        const QSignalBlocker intervalBlocker(refreshIntervalCombo_);
        const QSignalBlocker customBlocker(customIntervalMinutes_);
        const QSignalBlocker dailyBlocker(dailyRefreshTimeEdit_);
        refreshEnabledCheck_->setChecked(tool.refreshEnabled);
        const int modeIndex = refreshModeCombo_->findData(tool.refreshMode);
        refreshModeCombo_->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
        int intervalIndex = refreshIntervalCombo_->findData(tool.refreshIntervalSeconds);
        if (intervalIndex < 0) {
            intervalIndex = refreshIntervalCombo_->findData(0);
            customIntervalMinutes_->setValue(qMax(1, tool.refreshIntervalSeconds / 60));
        }
        refreshIntervalCombo_->setCurrentIndex(intervalIndex >= 0 ? intervalIndex : 1);
        const QTime dailyTime = QTime::fromString(tool.dailyRefreshTime, QStringLiteral("HH:mm"));
        dailyRefreshTimeEdit_->setTime(dailyTime.isValid() ? dailyTime : QTime(8, 0));
    }
    loadingRefreshSettings_ = false;
    updateRefreshControlVisibility();
}

void AutomationPage::updateRefreshControlVisibility()
{
    const bool enabled = refreshEnabledCheck_->isChecked();
    const bool daily = refreshModeCombo_->currentData().toString() == QStringLiteral("daily");
    const bool custom = refreshIntervalCombo_->currentData().toInt() == 0;
    refreshOptions_->setVisible(enabled);
    intervalRow_->setVisible(!daily);
    dailyRow_->setVisible(daily);
    customIntervalMinutes_->setVisible(custom);
    if (!enabled) {
        refreshHint_->setText(QStringLiteral("未启用。Orchestrate 启动、手动刷新和命令运行结束后仍会读取一次状态。"));
    } else if (daily) {
        refreshHint_->setText(QStringLiteral("每天 %1 读取一次状态 JSON，不会运行工具命令。")
                                  .arg(dailyRefreshTimeEdit_->time().toString(QStringLiteral("HH:mm"))));
    } else {
        const int seconds = refreshIntervalCombo_->currentData().toInt();
        const int minutes = seconds > 0 ? seconds / 60 : customIntervalMinutes_->value();
        refreshHint_->setText(QStringLiteral("每 %1 分钟读取一次状态 JSON，不会运行工具命令。").arg(minutes));
    }
}

void AutomationPage::saveRefreshSettings()
{
    if (loadingRefreshSettings_ || database_ == nullptr || selectedToolId_ <= 0) {
        return;
    }
    const bool enabled = refreshEnabledCheck_->isChecked();
    const QString mode = refreshModeCombo_->currentData().toString().isEmpty()
        ? QStringLiteral("interval")
        : refreshModeCombo_->currentData().toString();
    int intervalSeconds = refreshIntervalCombo_->currentData().toInt();
    if (intervalSeconds <= 0) {
        intervalSeconds = customIntervalMinutes_->value() * 60;
    }
    const QString dailyTime = dailyRefreshTimeEdit_->time().toString(QStringLiteral("HH:mm"));
    QString error;
    if (!database_->updateToolRefreshSettings(selectedToolId_, enabled, mode, intervalSeconds, dailyTime, &error)) {
        showError(error);
        return;
    }
    if (enabled) {
        lastAutoRefreshAt_.insert(selectedToolId_, QDateTime::currentDateTime());
    } else {
        lastAutoRefreshAt_.remove(selectedToolId_);
    }
}

QString AutomationPage::remoteCommandLine(const AppDatabase::AutomationTool &tool,
                                          const QString &executable,
                                          const QStringList &arguments) const
{
    QStringList commandParts;
    commandParts << QStringLiteral("exec") << shellQuote(executable);
    for (const QString &argument : arguments) {
        commandParts << shellQuote(argument);
    }
    QString commandLine = commandParts.join(QLatin1Char(' '));
    if (!tool.workingDirectory.isEmpty()) {
        commandLine = QStringLiteral("cd %1 && %2")
                          .arg(shellQuote(tool.workingDirectory), commandLine);
    }
    return commandLine;
}

void AutomationPage::renderRunRecord()
{
    if (!runRecords_.contains(selectedToolId_)) {
        runPanel_->hide();
        return;
    }
    const RunRecord &record = runRecords_[selectedToolId_];
    runTitle_->setText(QStringLiteral("最近运行：%1").arg(record.title));
    QString color = QStringLiteral("#4ade80");
    if (record.running) {
        color = QStringLiteral("#93c5fd");
    } else if (record.failed) {
        color = QStringLiteral("#f87171");
    }
    runStatus_->setText(record.status);
    runStatus_->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(color));
    runOutput_->setPlainText((record.outputTruncated ? QStringLiteral("[较早输出已截断，仅保留最近 256 Ki 字符]\n") : QString())
                            + record.output);
    runOutput_->verticalScrollBar()->setValue(runOutput_->verticalScrollBar()->maximum());
    runPanel_->show();
}

void AutomationPage::appendRunOutput(int toolId, const QString &text)
{
    auto &record = runRecords_[toolId];
    record.output += text;
    if (record.output.size() > maxRunOutputChars) {
        record.output = record.output.right(maxRunOutputChars);
        if (!record.output.isEmpty() && record.output.front().isLowSurrogate()) record.output.remove(0, 1);
        record.outputTruncated = true;
    }
}

void AutomationPage::runCommand(QListWidgetItem *item)
{
    if (database_ == nullptr || item == nullptr || selectedToolId_ <= 0 || !(item->flags() & Qt::ItemIsEnabled)) {
        return;
    }
    const int toolId = selectedToolId_;
    if (!runningProcesses_.value(toolId).isNull()) {
        QMessageBox::information(this, QStringLiteral("命令正在运行"),
                                 QStringLiteral("这个工具还有命令在运行，请等它结束后再运行其他命令。"));
        return;
    }

    AppDatabase::AutomationTool tool;
    if (!findTool(toolId, &tool)) {
        showError(QStringLiteral("找不到当前工具注册信息。"));
        return;
    }
    const QString expectedTarget = tool.sshHost.isEmpty() ? QStringLiteral("windows-local") : QStringLiteral("ssh");
    if (tool.targetType != expectedTarget) {
        showError(QStringLiteral("当前注册的执行目标不受支持，请修正 target_type 后重新读取声明。"));
        return;
    }
    const int commandId = item->data(Qt::UserRole).toInt();
    QString error;
    AppDatabase::ToolCommand command;
    for (const AppDatabase::ToolCommand &candidate : database_->toolCommands(toolId, &error)) {
        if (candidate.id == commandId) {
            command = candidate;
            break;
        }
    }
    if (command.id == 0) {
        showError(error.isEmpty() ? QStringLiteral("找不到这条命令，请重新读取声明。") : error);
        return;
    }

    if (command.interactive) {
        QMessageBox::information(this, QStringLiteral("交互式命令"),
                                 QStringLiteral("该命令需要交互输入，第一版先不由 Orchestrate 直接启动：\n%1")
                                     .arg(command.executable));
        return;
    }

    QHash<QString, QString> values;
    if (!command.parameters.isEmpty()) {
        // The parameter dialog doubles as the confirmation for high-risk commands.
        if (!promptParameters(this, command, stateCache_.value(toolId), &values)) {
            return;
        }
    } else if (command.highRisk
               && QMessageBox::question(this, QStringLiteral("高风险命令"),
                                        QStringLiteral("“%1”被工具声明为高风险，确定运行吗？").arg(command.name),
                                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                      != QMessageBox::Yes) {
        return;
    }
    const QStringList arguments = CommandArguments::expand(command.arguments, values);

    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    QString commandLine;
    if (!tool.sshHost.isEmpty()) {
        const QString remoteLine = remoteCommandLine(tool, command.executable, arguments);
        process->setProgram(QStringLiteral("ssh"));
        process->setArguments({
            QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
            QStringLiteral("-o"), QStringLiteral("ConnectTimeout=10"),
            tool.sshHost,
            remoteLine
        });
        commandLine = QStringLiteral("[%1] %2").arg(tool.sshHost, remoteLine);
    } else {
        process->setProgram(command.executable);
        process->setArguments(arguments);
        process->setWorkingDirectory(tool.workingDirectory.isEmpty()
                                         ? QFileInfo(tool.registrationPath).absolutePath()
                                         : tool.workingDirectory);
        QStringList parts {command.executable};
        for (const QString &argument : arguments) {
            parts << displayArgument(argument);
        }
        commandLine = parts.join(QLatin1Char(' '));
    }

    RunRecord record;
    record.title = command.name;
    record.running = true;
    record.status = QStringLiteral("运行中…");
    runRecords_.insert(toolId, record);
    appendRunOutput(toolId, QStringLiteral("$ %1\n").arg(commandLine));
    runningProcesses_.insert(toolId, process);
    renderRunRecord();

    const auto decoder = std::make_shared<QStringDecoder>(QStringDecoder::Utf8);
    auto *outputTimer = new QTimer(process);
    outputTimer->setSingleShot(true);
    outputTimer->setInterval(100);
    connect(outputTimer, &QTimer::timeout, this, [this, toolId] {
        if (selectedToolId_ == toolId) renderRunRecord();
    });
    const auto drain = [this, process, toolId, decoder] {
        while (process->bytesAvailable() > 0) appendRunOutput(toolId, (*decoder)(process->read(64 * 1024)));
    };
    connect(process, &QProcess::readyRead, this, [drain, outputTimer] {
        drain();
        if (!outputTimer->isActive()) outputTimer->start();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, toolId, outputTimer](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        RunRecord &failed = runRecords_[toolId];
        failed.running = false;
        failed.failed = true;
        failed.status = QStringLiteral("启动失败");
        appendRunOutput(toolId, process->errorString());
        outputTimer->stop();
        runningProcesses_.remove(toolId);
        if (selectedToolId_ == toolId) {
            renderRunRecord();
        }
        process->deleteLater();
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this, process, toolId, decoder, drain, outputTimer](int exitCode, QProcess::ExitStatus status) {
        drain();
        QChar tail[8];
        const auto final = decoder->finalize(tail, 8);
        appendRunOutput(toolId, QString(tail, final.next - tail));
        outputTimer->stop();
        RunRecord &finished = runRecords_[toolId];
        finished.running = false;
        finished.failed = status != QProcess::NormalExit || exitCode != 0;
        finished.status = finished.failed
            ? QStringLiteral("失败（退出码 %1）").arg(exitCode)
            : QStringLiteral("已完成 · %1").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
        runningProcesses_.remove(toolId);
        if (selectedToolId_ == toolId) {
            renderRunRecord();
        }
        refreshStateForTool(toolId, true);
        process->deleteLater();
    });
    process->start();
}

void AutomationPage::showError(const QString &message)
{
    QMessageBox::critical(this, QStringLiteral("自动化工具操作失败"), message);
}
