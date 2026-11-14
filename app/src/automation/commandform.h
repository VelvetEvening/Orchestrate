#pragma once

#include <QFileDialog>

#include "commandparameters.h"
#include "toolstate.h"
#include "../data/appdatabase.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QProcess>
#include <QTimeEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>

// A form owns one explicit state snapshot. Background page refreshes cannot
// replace its options while the user is selecting a value.
class CommandForm final : public QDialog
{
public:
    using StateCallback = std::function<void(bool, const QJsonObject &, const QString &)>;
    using StateReader = std::function<void(QObject *, StateCallback)>;
    using OptionsCallback = std::function<void(bool, const QList<CommandParameters::Option> &, const QString &)>;
    using OptionsReader = std::function<void(QObject *, const QJsonObject &, const QHash<QString, QString> &, OptionsCallback)>;
    using OptionsEditor = std::function<bool(const QString &,QJsonObject *,const QHash<QString,QString> &,QWidget *)>;

    CommandForm(const AppDatabase::ToolCommand &command, StateReader reader, QWidget *parent = nullptr, bool localPaths = true, OptionsReader optionsReader = {}, OptionsEditor optionsEditor = {})
        : QDialog(parent), reader_(std::move(reader)), optionsReader_(std::move(optionsReader)),optionsEditor_(std::move(optionsEditor))
    {
        setObjectName(QStringLiteral("commandParametersDialog"));
        setWindowTitle(command.name);
        setMinimumWidth(460);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(26, 22, 26, 20);
        layout->setSpacing(12);
        auto *title = new QLabel(command.name, this);
        title->setTextFormat(Qt::PlainText);
        title->setObjectName(QStringLiteral("cardTitle"));
        title->setWordWrap(true);
        layout->addWidget(title);
        if (!command.description.isEmpty()) layout->addWidget(textLabel(command.description, QStringLiteral("muted")));
        if (command.highRisk) layout->addWidget(textLabel(
            QStringLiteral("该命令被工具声明为高风险，请确认参数后再运行。"), QStringLiteral("warningText")));

        auto *scroll = new QScrollArea(this);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *content = new QWidget(scroll);
        auto *body = new QVBoxLayout(content);
        body->setContentsMargins(0, 0, 0, 0);
        body->setSpacing(12);
        auto *context = new QFormLayout;
        auto *form = new QFormLayout;
        for (auto *rows : {context, form}) {
            rows->setContentsMargins(0, 4, 0, 0);
            rows->setHorizontalSpacing(14);
            rows->setVerticalSpacing(12);
            rows->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
            rows->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            rows->setRowWrapPolicy(QFormLayout::WrapLongRows);
        }

        for (const auto &value : command.parameters) {
            Input input;
            input.definition = value.toObject();
            input.key = input.definition.value(QStringLiteral("key")).toString().trimmed();
            input.type = CommandParameters::type(input.definition);
            input.label = input.definition.value(QStringLiteral("label")).toString(input.key);
            input.required = input.definition.value(QStringLiteral("required")).toBool();
            input.dynamic = input.definition.contains(QStringLiteral("options_state_key"));
            input.source = input.definition.value("options_source").toObject();
            needsState_ |= input.dynamic || input.definition.contains(QStringLiteral("state_key"));
            const auto initial = input.definition.value(QStringLiteral("default"));
            if (input.type == QStringLiteral("readonly")) {
                auto *label = textLabel(QStringLiteral("读取中…"), QStringLiteral("contextValue_") + input.key);
                label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
                input.widget = label;
                context->addRow(fieldLabel(input.label), label);
            } else {
                if (input.type == QStringLiteral("number")) {
                    auto *spin = new QDoubleSpinBox(content); spin->setDecimals(6);
                    spin->setRange(input.definition.value("min").toDouble(0), input.definition.value("max").toDouble(1000000));
                    spin->setSingleStep(input.definition.value("step").toDouble(1));
                    const auto unit = input.definition.value("unit").toString(); if (!unit.isEmpty()) spin->setSuffix(" " + unit);
                    input.widget = spin;
                } else if (input.type == QStringLiteral("integer")) {
                    auto *spin = new QSpinBox(content);
                    spin->setRange(input.definition.value(QStringLiteral("min")).toInt(0),
                                   input.definition.value(QStringLiteral("max")).toInt(1000000));
                    spin->setSingleStep(qMax(1, input.definition.value(QStringLiteral("step")).toInt(1)));
                    const QString unit = input.definition.value(QStringLiteral("unit")).toString();
                    if (!unit.isEmpty()) spin->setSuffix(QLatin1Char(' ') + unit);
                    input.widget = spin;
                } else if (input.type == QStringLiteral("time")) {
                    auto *time = new QTimeEdit(content);
                    time->setDisplayFormat(QStringLiteral("HH:mm"));
                    input.widget = time;
                } else if (input.type == QStringLiteral("boolean")) {
                    input.widget = new QCheckBox(input.definition.value(QStringLiteral("text")).toString(), content);
                } else if (input.type == QStringLiteral("choice")) {
                    auto *combo = new QComboBox(content);
                    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                    combo->setMinimumContentsLength(18);
                    combo->setPlaceholderText(input.dynamic ? QStringLiteral("正在读取选项…") : QStringLiteral("请选择…"));
                    combo->setEnabled(!input.dynamic);
                    input.widget = combo;
                    input.hint = textLabel({}, QStringLiteral("choiceHint_") + input.key);
                    input.hint->setProperty("formHint", true);
                    if (!input.dynamic && input.source.isEmpty()) {
                        CommandParameters::readOptions(input.definition.value(QStringLiteral("options")),
                            false, &input.options, &input.optionsError);
                        populateChoice(input, CommandParameters::scalarText(initial),
                                       !input.definition.contains(QStringLiteral("state_key")));
                    }
                } else {
                    auto *edit = new QLineEdit(content);
                    edit->setPlaceholderText(input.definition.value(QStringLiteral("placeholder")).toString());
                    input.widget = edit;
                }
                input.widget->setObjectName(QStringLiteral("parameter_") + input.key);
                input.widget->setAccessibleName(input.label);
                if (input.type != QStringLiteral("choice")) setValue(input, initial);
                auto *field = new QWidget(content);
                auto *column = new QVBoxLayout(field);
                column->setContentsMargins(0, 0, 0, 0);
                column->setSpacing(4);
                if (input.type == QStringLiteral("file") || input.type == QStringLiteral("directory")) {
                    auto *pathRow = new QHBoxLayout;
                    pathRow->addWidget(input.widget, 1);
                    auto *edit = qobject_cast<QLineEdit *>(input.widget);
                    if (localPaths) {
                        auto *browse = new QPushButton(QStringLiteral("选择…"), field);
                        browse->setAutoDefault(false);
                        pathRow->addWidget(browse);
                        connect(browse, &QPushButton::clicked, this, [this, edit, directory = input.type == QStringLiteral("directory")] {
                            const QString path = directory ? QFileDialog::getExistingDirectory(this, QStringLiteral("选择目录"), edit->text())
                                : QFileDialog::getOpenFileName(this, QStringLiteral("选择文件"), edit->text());
                            if (!path.isEmpty()) { edit->setText(path); emit edit->textEdited(path); }
                        });
                    } else edit->setPlaceholderText(QStringLiteral("填写目标环境中的路径"));
                    column->addLayout(pathRow);
                } else column->addWidget(input.widget);
                const QString help = input.definition.value(QStringLiteral("description")).toString();
                if (!help.isEmpty()) column->addWidget(textLabel(help, QStringLiteral("fieldHelp")));
                if (input.hint) column->addWidget(input.hint);
                if (!input.source.isEmpty()) {
                    auto *refresh = new QPushButton(QStringLiteral("刷新选项"), field); refresh->setAutoDefault(false);
                    auto *actions=new QHBoxLayout;actions->addStretch();column->addLayout(actions);
                    if(optionsEditor_){
                        auto *configure=new QPushButton(QStringLiteral("设置下拉"),field);configure->setAutoDefault(false);configure->setObjectName("configure_options_"+input.key);actions->addWidget(configure);
                        connect(configure,&QPushButton::clicked,this,[this,key=input.key]{
                            for(auto &field:inputs_)if(field.key==key){auto source=field.source;
                                if(optionsEditor_(key,&source,values(),this)){field.source=source;field.definition["options_source"]=source;invalidateSources(key,true);}break;}
                        });
                    }
                    actions->addWidget(refresh);
                    connect(refresh, &QPushButton::clicked, this, [this, key = input.key] { invalidateSources(key, true); });
                }
                form->addRow(fieldLabel(input.label + (input.required ? QStringLiteral(" *") : QString())), field);
            }
            inputs_.append(input);
        }

        if (context->rowCount() > 0) {
            body->addWidget(textLabel(QStringLiteral("当前信息"), QStringLiteral("commandContextTitle")));
            body->addLayout(context);
            if (form->rowCount() > 0) body->addSpacing(4);
        } else delete context;
        auto *stateRow = new QHBoxLayout;
        stateStatus_ = textLabel(QStringLiteral("正在读取工具状态…"), QStringLiteral("commandStateStatus"));
        stateStatus_->setProperty("formHint", true);
        stateRow->addWidget(stateStatus_, 1);
        refresh_ = new QPushButton(QStringLiteral("刷新状态"), content);
        refresh_->setObjectName(QStringLiteral("commandStateRefresh"));
        refresh_->setAutoDefault(false);
        stateRow->addWidget(refresh_, 0, Qt::AlignTop);
        if (needsState_) body->addLayout(stateRow);
        else {
            stateStatus_->hide();
            refresh_->hide();
            delete stateRow;
        }
        if (form->rowCount() > 0) body->addLayout(form);
        else delete form;
        body->addStretch();
        scroll->setWidget(content);
        // QScrollArea enables an opaque palette fill on its content by default;
        // let this form inherit the dialog's surface instead of a gray inset.
        content->setAutoFillBackground(false);
        scroll->viewport()->setAutoFillBackground(false);
        scroll->setMinimumHeight(qMin(420, content->sizeHint().height() + 12));
        layout->addWidget(scroll, 1);
        error_ = textLabel({}, QStringLiteral("commandParameterError"));
        error_->hide();
        layout->addWidget(error_);
        auto *buttons = new QHBoxLayout;
        buttons->addStretch();
        auto *cancel = new QPushButton(QStringLiteral("取消"), this);
        cancel->setAutoDefault(false);
        buttons->addWidget(cancel);
        run_ = new QPushButton(command.highRisk ? QStringLiteral("确认运行") : QStringLiteral("运行"), this);
        run_->setObjectName(QStringLiteral("primaryButton"));
        run_->setDefault(true);
        buttons->addWidget(run_);
        layout->addLayout(buttons);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(run_, &QPushButton::clicked, this, [this] { validateAndAccept(); });
        connect(refresh_, &QPushButton::clicked, this, [this] { refreshState(); });
        for (qsizetype i = 0; i < inputs_.size(); ++i) {
            auto &input = inputs_[i];
            const auto edited = [this, i] {
                auto &field = inputs_[i];
                field.edited = true;
                if (auto *combo = qobject_cast<QComboBox *>(field.widget)) {
                    field.selection = combo->currentData().toString();
                    updateChoiceHint(field, {});
                }
                error_->hide();
                invalidateSources(field.key);
            };
            if (auto *edit = qobject_cast<QLineEdit *>(input.widget)) connect(edit, &QLineEdit::textEdited, this, edited);
            else if (auto *spin = qobject_cast<QSpinBox *>(input.widget)) connect(spin, &QSpinBox::valueChanged, this, edited);
            else if (auto *spin = qobject_cast<QDoubleSpinBox *>(input.widget)) connect(spin, &QDoubleSpinBox::valueChanged, this, edited);
            else if (auto *time = qobject_cast<QTimeEdit *>(input.widget)) connect(time, &QTimeEdit::timeChanged, this, edited);
            else if (auto *check = qobject_cast<QCheckBox *>(input.widget)) connect(check, &QCheckBox::toggled, this, edited);
            else if (auto *combo = qobject_cast<QComboBox *>(input.widget)) connect(combo, &QComboBox::currentIndexChanged, this, edited);
        }
        updateRunState();
        resize(520, qMin(700, sizeHint().height()));
        if (needsState_) QTimer::singleShot(0, this, [this] { refreshState(); });
        sourceTimer_ = new QTimer(this); sourceTimer_->setSingleShot(true); sourceTimer_->setInterval(200);
        connect(sourceTimer_, &QTimer::timeout, this, [this] { refreshSources(); });
        sourceTimer_->start(0);
    }

    QHash<QString, QString> values() const
    {
        QHash<QString, QString> values;
        for (const auto &input : inputs_) {
            if (input.type == QStringLiteral("readonly")) continue;
            QString value;
            if (auto *spin = qobject_cast<QSpinBox *>(input.widget)) value = QString::number(spin->value());
            else if (auto *spin = qobject_cast<QDoubleSpinBox *>(input.widget)) value = QString::number(spin->value(), 'g', 15);
            else if (auto *time = qobject_cast<QTimeEdit *>(input.widget)) value = time->time().toString(QStringLiteral("HH:mm"));
            else if (auto *combo = qobject_cast<QComboBox *>(input.widget)) value = combo->currentData().toString();
            else if (auto *check = qobject_cast<QCheckBox *>(input.widget)) value = check->isChecked()
                ? input.definition.value(QStringLiteral("true_value")).toString(QStringLiteral("true"))
                : input.definition.value(QStringLiteral("false_value")).toString(QStringLiteral("false"));
            else if (auto *edit = qobject_cast<QLineEdit *>(input.widget)) value = edit->text().trimmed();
            values.insert(input.key, value);
        }
        return values;
    }

private:
    struct Input {
        QJsonObject definition;
        QString key, type, label, selection, optionsError;
        QWidget *widget = nullptr;
        QLabel *hint = nullptr;
        QList<CommandParameters::Option> options;
        bool required = false, dynamic = false, edited = false, optionsLoaded = false, stateDefaultApplied = false;
        QJsonObject source;
        QPointer<QObject> sourceRequest;
        bool sourceReady = false, sourceLoading = false;
        quint64 generation = 0;
    };

    QLabel *textLabel(const QString &text, const QString &name)
    {
        auto *label = new QLabel(text, this);
        label->setObjectName(name);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        return label;
    }

    QLabel *fieldLabel(const QString &text)
    {
        return textLabel(text + QStringLiteral("："), QStringLiteral("parameterLabel"));
    }

    static void setValue(Input &input, const QJsonValue &value)
    {
        const QSignalBlocker blocker(input.widget);
        const QString text = CommandParameters::scalarText(value);
        if (auto *spin = qobject_cast<QSpinBox *>(input.widget)) {
            bool ok = false;
            const int number = text.toInt(&ok);
            spin->setValue(ok ? number : spin->minimum());
        } else if (auto *spin = qobject_cast<QDoubleSpinBox *>(input.widget)) {
            bool ok = false; const double number = text.toDouble(&ok); spin->setValue(ok ? number : spin->minimum());
        } else if (auto *time = qobject_cast<QTimeEdit *>(input.widget)) {
            QTime parsed = QTime::fromString(text, QStringLiteral("HH:mm"));
            if (!parsed.isValid()) parsed = QTime::fromString(text, QStringLiteral("H:mm"));
            time->setTime(parsed.isValid() ? parsed : QTime(0, 0));
        } else if (auto *check = qobject_cast<QCheckBox *>(input.widget)) check->setChecked(text == QStringLiteral("true"));
        else if (auto *edit = qobject_cast<QLineEdit *>(input.widget)) edit->setText(text);
    }

    static void updateChoiceHint(Input &input, const QString &message)
    {
        QString text = message;
        if (text.isEmpty()) {
            const auto *combo = qobject_cast<QComboBox *>(input.widget);
            for (const auto &option : input.options)
                if (combo->currentIndex() >= 0 && option.value == combo->currentData().toString()) text = option.description;
        }
        input.hint->setText(text);
        input.hint->setVisible(!text.isEmpty());
    }

    static void populateChoice(Input &input, const QString &desired, bool legacyDefault = false)
    {
        auto *combo = qobject_cast<QComboBox *>(input.widget);
        const QSignalBlocker blocker(combo);
        combo->clear();
        if ((input.dynamic || !input.source.isEmpty()) && !input.required && !input.options.isEmpty())
            combo->addItem(QStringLiteral("不选择"), QString());
        for (const auto &option : input.options) {
            combo->addItem(option.label, option.value);
            if (!option.description.isEmpty()) combo->setItemData(combo->count() - 1,
                QStringLiteral("<qt>%1</qt>").arg(option.description.toHtmlEscaped().replace(
                    QLatin1Char('\n'), QStringLiteral("<br/>"))), Qt::ToolTipRole);
        }
        int index = combo->findData(desired);
        if (index < 0 && legacyDefault && combo->count() > 0) index = 0;
        combo->setCurrentIndex(index);
        input.selection = index >= 0 ? combo->currentData().toString() : QString();
        input.optionsLoaded = true;
        combo->setPlaceholderText(input.options.isEmpty() ? QStringLiteral("暂无可选项") : QStringLiteral("请选择…"));
        combo->setEnabled(!input.options.isEmpty());
        updateChoiceHint(input, input.options.isEmpty() ? QStringLiteral("暂无可选项，请先在工具中添加后刷新。")
            : index < 0 && !desired.isEmpty() ? QStringLiteral("之前的选择已失效，请重新选择。") : QString());
    }

    void refreshState()
    {
        if (loading_) return;
        loading_ = true;
        stateReady_ = false;
        error_->hide();
        stateStatus_->setText(QStringLiteral("正在读取工具状态…"));
        refresh_->setText(QStringLiteral("刷新中…"));
        refresh_->setEnabled(false);
        for (auto &input : inputs_) if (input.dynamic) {
            input.widget->setEnabled(false);
            updateChoiceHint(input, QStringLiteral("正在读取选项…"));
        }
        updateRunState();
        if (!reader_) { applyState(false, {}, QStringLiteral("状态读取通道不可用。")); return; }
        const QPointer<CommandForm> guard(this);
        reader_(this, [guard](bool ok, const QJsonObject &state, const QString &error) {
            if (guard) guard->applyState(ok, state, error);
        });
    }

    void applyState(bool success, const QJsonObject &state, const QString &error)
    {
        const auto oldValues = values();
        loading_ = false;
        refresh_->setEnabled(true);
        const bool expired = success && ToolState::expired(state);
        stateReady_ = success && !expired;
        if (!stateReady_) {
            stateStatus_->setText(expired ? QStringLiteral("状态已过期，请更新工具状态后重试。")
                : QStringLiteral("读取失败：%1").arg(error.isEmpty() ? QStringLiteral("请检查工具状态文件后重试。") : error));
            refresh_->setText(QStringLiteral("重试"));
            for (auto &input : inputs_) {
                if (input.type == QStringLiteral("readonly")) qobject_cast<QLabel *>(input.widget)->setText(QStringLiteral("读取失败"));
                if (input.dynamic) {
                    auto *combo = qobject_cast<QComboBox *>(input.widget);
                    const QSignalBlocker blocker(combo);
                    combo->clear();
                    combo->setPlaceholderText(QStringLiteral("选项读取失败"));
                    updateChoiceHint(input, QStringLiteral("请重试读取选项。"));
                }
            }
            updateRunState();
            return;
        }
        snapshot_ = state;
        bool invalidOptions = false;
        for (auto &input : inputs_) {
            if (input.type == QStringLiteral("readonly")) {
                const auto value = CommandParameters::stateValue(state, input.definition.value(QStringLiteral("state_key")).toString());
                QString text = CommandParameters::scalarText(value);
                if (text.isEmpty()) text = input.definition.value(QStringLiteral("placeholder")).toString(QStringLiteral("未提供"));
                qobject_cast<QLabel *>(input.widget)->setText(text);
            } else if (input.dynamic) {
                const auto source = CommandParameters::stateValue(state, input.definition.value(QStringLiteral("options_state_key")).toString());
                if (!CommandParameters::readOptions(source, true, &input.options, &input.optionsError)) {
                    invalidOptions = true;
                    auto *combo = qobject_cast<QComboBox *>(input.widget);
                    const QSignalBlocker blocker(combo);
                    combo->clear();
                    combo->setPlaceholderText(QStringLiteral("选项读取失败"));
                    combo->setEnabled(false);
                    updateChoiceHint(input, input.optionsError);
                } else {
                    const QString desired = input.optionsLoaded ? input.selection
                        : CommandParameters::scalarText(CommandParameters::initialValue(input.definition, state, true));
                    populateChoice(input, desired);
                }
            } else if (input.definition.contains(QStringLiteral("state_key")) && !input.edited) {
                const auto initial = CommandParameters::initialValue(input.definition, state, input.type == QStringLiteral("choice"));
                if (input.type == QStringLiteral("choice")) {
                    if (!input.stateDefaultApplied) populateChoice(input, CommandParameters::scalarText(initial));
                    input.stateDefaultApplied = true;
                } else setValue(input, initial);
            }
        }
        const auto updated = ToolState::timestamp(state.value(QStringLiteral("updated_at")).toString());
        stateStatus_->setText(invalidOptions ? QStringLiteral("部分选项读取失败，请检查工具状态后重试。")
            : updated.isValid() ? QStringLiteral("状态更新于 %1").arg(updated.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
            : QStringLiteral("已读取工具状态。"));
        refresh_->setText(invalidOptions ? QStringLiteral("重试") : QStringLiteral("刷新状态"));
        updateRunState();
        const auto newValues = values();
        for (auto it = newValues.begin(); it != newValues.end(); ++it) if (oldValues.value(it.key()) != it.value()) invalidateSources(it.key());
        if (sourceTimer_) sourceTimer_->start(0);
    }

    void updateRunState()
    {
        bool ready = !needsState_ || (stateReady_ && !loading_);
        for (const auto &input : inputs_) if (input.type == QStringLiteral("choice"))
            ready &= input.optionsError.isEmpty() && (!input.required || !input.options.isEmpty())
                && (input.source.isEmpty() || (input.sourceReady && !input.sourceLoading));
        run_->setEnabled(ready);
    }

    void validateAndAccept()
    {
        updateRunState(); if (!run_->isEnabled()) return;
        if (needsState_ && (!stateReady_ || loading_)) return;
        if (needsState_ && ToolState::expired(snapshot_)) {
            applyState(false, {}, QStringLiteral("状态已过期，请刷新后再运行。"));
            return;
        }
        const auto current = values();
        QHash<QString,int> groupCounts;QHash<QString,QStringList> groupLabels;QSet<QString> requiredGroups;
        for (const auto &input : inputs_) {
            const auto group=input.definition.value("exclusive_group").toString();if(group.isEmpty())continue;
            groupLabels[group]<<input.label;groupCounts[group]+=current.value(input.key).isEmpty()?0:1;
            if(input.definition.value("exclusive_required").toBool())requiredGroups.insert(group);
        }
        for(auto it=groupLabels.begin();it!=groupLabels.end();++it)if(groupCounts.value(it.key())>1||(requiredGroups.contains(it.key())&&groupCounts.value(it.key())!=1)) {
            error_->setText(QStringLiteral("%1：%2。").arg(requiredGroups.contains(it.key())?QStringLiteral("请选择并填写其中一项"):QStringLiteral("只能填写其中一项"),it.value().join(QStringLiteral("、"))));error_->show();return;
        }
        for (auto &input : inputs_) {
            QString problem;
            if (input.type == QStringLiteral("readonly")) continue;
            if (input.required && current.value(input.key).isEmpty()) problem = QStringLiteral("请%1“%2”。")
                .arg(input.type == QStringLiteral("choice") ? QStringLiteral("选择") : QStringLiteral("填写"), input.label);
            if (input.type == QStringLiteral("choice")) {
                bool found = false;
                const auto *combo = qobject_cast<QComboBox *>(input.widget);
                for (const auto &option : input.options) if (combo->currentIndex() >= 0
                    && option.value == current.value(input.key)) found = true;
                if (!input.optionsError.isEmpty() || (!found && (!current.value(input.key).isEmpty() || input.required)))
                    problem = QStringLiteral("请重新选择有效的“%1”。").arg(input.label);
            }
            if (!problem.isEmpty()) {
                error_->setText(problem);
                error_->show();
                input.widget->setFocus();
                return;
            }
        }
        accept();
    }

    void invalidateSources(const QString &key, bool includeSelf = false)
    {
        QSet<QString> changed {key};
        bool more = true;
        while (more) { more = false; for (const auto &input : inputs_) {
            if (changed.contains(input.key)) continue;
            for (const auto &dependency : CommandParameters::sourceDependencies(input.source)) if (changed.contains(dependency)) {
                changed.insert(input.key); more = true; break;
            }
        } }
        for (auto &input : inputs_) {
            if (input.source.isEmpty() || !changed.contains(input.key) || (input.key == key && !includeSelf)) continue;
            ++input.generation; input.sourceLoading = false; input.sourceReady = false; input.optionsError.clear();
            if (input.sourceRequest) {
                for (auto *process : input.sourceRequest->findChildren<QProcess *>()) process->kill();
                input.sourceRequest->deleteLater(); input.sourceRequest = nullptr;
            }
            auto *combo = qobject_cast<QComboBox *>(input.widget); const QSignalBlocker blocker(combo);
            if (includeSelf && input.key == key) input.selection = combo->currentData().toString();
            else input.selection.clear();
            input.options.clear(); combo->clear(); combo->setEnabled(false);
            updateChoiceHint(input, QStringLiteral("等待选择上级参数或读取选项…"));
        }
        if (sourceTimer_) sourceTimer_->start(); updateRunState();
    }

    void refreshSources()
    {
        if (needsState_ && (!stateReady_ || loading_)) return;
        const auto current = values();
        for (qsizetype i = 0; i < inputs_.size(); ++i) {
            auto &input = inputs_[i];
            if (input.source.isEmpty() || input.sourceReady || input.sourceLoading || !input.optionsError.isEmpty()) continue;
            const auto dependencies = CommandParameters::sourceDependencies(input.source);
            bool ready = true; for (const auto &key : dependencies) ready &= !current.value(key).isEmpty();
            if (!ready) { updateChoiceHint(input, QStringLiteral("请先填写：%1").arg(dependencies.join(QStringLiteral("、")))); continue; }
            input.sourceLoading = true; input.widget->setEnabled(false);
            updateChoiceHint(input, QStringLiteral("正在读取选项…"));
            const quint64 generation = ++input.generation;
            auto *context = new QObject(this); input.sourceRequest = context;
            const QPointer<CommandForm> guard(this);
            auto callback = [guard, context = QPointer<QObject>(context), i, generation](bool ok, const QList<CommandParameters::Option> &options, const QString &error) {
                if (context) context->deleteLater();
                if (!guard || !context || guard->inputs_[i].generation != generation) return;
                auto &field = guard->inputs_[i]; field.sourceRequest = nullptr; field.sourceLoading = false; field.sourceReady = ok;
                field.options = options; field.optionsError = ok ? QString() : error.isEmpty() ? QStringLiteral("读取失败，请重试。") : error;
                if (ok) {
                    const QString desired = field.optionsLoaded ? field.selection : CommandParameters::scalarText(field.definition.value("default"));
                    populateChoice(field, desired);
                    guard->invalidateSources(field.key);
                } else updateChoiceHint(field, error.isEmpty() ? QStringLiteral("读取失败，点击刷新选项重试。") : error);
                guard->updateRunState(); guard->sourceTimer_->start(0);
            };
            if (optionsReader_) optionsReader_(context, input.source, current, callback);
            else callback(false, {}, QStringLiteral("选项查询通道不可用。"));
        }
        updateRunState();
    }

    StateReader reader_;
    OptionsReader optionsReader_;
    OptionsEditor optionsEditor_;
    QTimer *sourceTimer_ = nullptr;
    QList<Input> inputs_;
    QJsonObject snapshot_;
    QLabel *stateStatus_ = nullptr;
    QLabel *error_ = nullptr;
    QPushButton *refresh_ = nullptr;
    QPushButton *run_ = nullptr;
    bool needsState_ = false, stateReady_ = false, loading_ = false;
};
