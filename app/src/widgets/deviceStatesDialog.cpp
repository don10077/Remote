/**
 * @file deviceStatesDialog.cpp
 * @brief Окно «Питание и входы» устройства.
 */

#include "deviceStatesDialog.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace harmony::app {

namespace {

constexpr int kSteps = 0; ///< Пункт «несколько шагов» в списке (если есть)
constexpr int kSeconds = 1000; ///< мс в секунде

/// Данные пункта «несколько шагов» (не может совпасть с именем команды).
QString stepsMarker() {
    return QStringLiteral("\u0001steps");
}

/// Шаг словами: «PowerOn», «пауза 1,5 с», «InputType → HDMI1».
QString stepText(const StateStep& step) {
    switch (step.kind) {
    case StateStep::Kind::Command:
        return step.durationMs > 0
                   ? QObject::tr("%1 (hold %2 s)")
                         .arg(QString::fromStdString(step.command))
                         .arg(QLocale().toString(step.durationMs / double(kSeconds)))
                   : QString::fromStdString(step.command);
    case StateStep::Kind::Delay:
        return QObject::tr("pause %1 s").arg(QLocale().toString(step.delayMs / double(kSeconds)));
    case StateStep::Kind::SetValue:
        return QObject::tr("%1 → %2").arg(QString::fromStdString(step.state),
                                          QString::fromStdString(step.value));
    case StateStep::Kind::Other:
        return QObject::tr("other step");
    }
    return {};
}

QString sequenceText(const StateSequence& steps) {
    QStringList parts;
    for (const StateStep& step : steps) {
        parts << stepText(step);
    }
    return parts.join(QStringLiteral(" · "));
}

/// Одна команда (возможно, с длительностью нажатия) — правится прямо в списке.
bool isSimple(const StateSequence& steps) {
    return steps.size() == 1 && steps.front().kind == StateStep::Kind::Command;
}

/// Окно шагов действия: команды, паузы, перестановка.
class StepsDialog final : public QDialog {
public:
    StepsDialog(const QStringList& commands, const StateSequence& steps, QWidget* parent)
        : QDialog(parent), steps_(steps) {
        setWindowTitle(tr("Steps"));
        setMinimumSize(460, 360);
        auto* hint = new QLabel(tr("The remote performs the steps one after another."), this);
        hint->setObjectName("pageMeta");
        list_ = new QListWidget(this);

        auto* command = new QComboBox(this);
        command->addItems(commands);
        auto* addCommand = new QPushButton(tr("Add command"), this);
        auto* pause = new QDoubleSpinBox(this);
        pause->setRange(0.1, 60);
        pause->setSingleStep(0.5);
        pause->setValue(1);
        pause->setSuffix(tr(" s"));
        auto* addPause = new QPushButton(tr("Add pause"), this);
        auto* addRow = new QHBoxLayout;
        addRow->addWidget(command, 1);
        addRow->addWidget(addCommand);
        addRow->addSpacing(12);
        addRow->addWidget(pause);
        addRow->addWidget(addPause);

        auto* up = new QPushButton(tr("Up"), this);
        auto* down = new QPushButton(tr("Down"), this);
        auto* remove = new QPushButton(tr("Delete"), this);
        auto* cancel = new QPushButton(tr("Cancel"), this);
        auto* ok = new QPushButton(tr("OK"), this);
        ok->setObjectName("primaryButton");
        ok->setDefault(true);
        auto* buttons = new QHBoxLayout;
        buttons->addWidget(up);
        buttons->addWidget(down);
        buttons->addWidget(remove);
        buttons->addStretch(1);
        buttons->addWidget(cancel);
        buttons->addWidget(ok);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 18, 20, 16);
        layout->addWidget(hint);
        layout->addWidget(list_, 1);
        layout->addLayout(addRow);
        layout->addLayout(buttons);

        connect(addCommand, &QPushButton::clicked, this, [this, command] {
            if (!command->currentText().isEmpty()) {
                steps_.push_back(StateStep::press(command->currentText().toStdString()));
                refresh(static_cast<int>(steps_.size()) - 1);
            }
        });
        connect(addPause, &QPushButton::clicked, this, [this, pause] {
            steps_.push_back(StateStep::pause(static_cast<int>(pause->value() * kSeconds)));
            refresh(static_cast<int>(steps_.size()) - 1);
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            const int row = list_->currentRow();
            if (row >= 0) {
                steps_.erase(steps_.begin() + row);
                refresh(std::min(row, static_cast<int>(steps_.size()) - 1));
            }
        });
        auto move = [this](int delta) {
            const int row = list_->currentRow();
            const int to = row + delta;
            if (row >= 0 && to >= 0 && to < static_cast<int>(steps_.size())) {
                std::swap(steps_[static_cast<std::size_t>(row)],
                          steps_[static_cast<std::size_t>(to)]);
                refresh(to);
            }
        };
        connect(up, &QPushButton::clicked, this, [move] { move(-1); });
        connect(down, &QPushButton::clicked, this, [move] { move(1); });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(ok, &QPushButton::clicked, this, &QDialog::accept);
        refresh(0);
    }

    [[nodiscard]] const StateSequence& steps() const { return steps_; }

private:
    void refresh(int current) {
        list_->clear();
        for (const StateStep& step : steps_) {
            list_->addItem(stepText(step));
        }
        list_->setCurrentRow(current);
    }

    StateSequence steps_;
    QListWidget* list_ = nullptr;
};

/// Радиокнопки режима в столбик; номер кнопки — значение режима.
QButtonGroup* modeButtons(QWidget* parent, QVBoxLayout* layout, const QStringList& labels) {
    auto* group = new QButtonGroup(parent);
    for (int i = 0; i < labels.size(); ++i) {
        auto* radio = new QRadioButton(labels[i], parent);
        group->addButton(radio, i);
        layout->addWidget(radio);
    }
    return group;
}

QDoubleSpinBox* secondsBox(QWidget* parent) {
    auto* box = new QDoubleSpinBox(parent);
    box->setRange(0, 60);
    box->setSingleStep(0.5);
    box->setDecimals(1);
    box->setSuffix(QObject::tr(" s"));
    box->setFixedWidth(110); // число — короткое; на всю ширину поле выглядит пустым
    return box;
}

} // namespace

// --- SequenceEdit ---------------------------------------------------------------

SequenceEdit::SequenceEdit(const QStringList& commands, bool optional, QWidget* parent)
    : QWidget(parent), commands_(commands), optional_(optional) {
    combo_ = new QComboBox(this);
    combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo_->setMinimumContentsLength(16);
    more_ = new QToolButton(this);
    more_->setText(QStringLiteral("…"));
    more_->setToolTip(tr("Several steps: commands, pauses"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(combo_, 1);
    layout->addWidget(more_);

    connect(combo_, &QComboBox::activated, this, [this](int index) {
        const QString command = combo_->itemData(index).toString();
        if (command == stepsMarker()) {
            return; // «несколько шагов» — как было
        }
        if (command.isEmpty()) {
            steps_.clear();
        } else {
            // Длительность нажатия прежней команды сохраняется (её задал Logitech).
            const int duration = isSimple(steps_) ? steps_.front().durationMs : 0;
            steps_ = {StateStep::press(command.toStdString())};
            steps_.front().durationMs = duration;
        }
        refresh();
        emit changed();
    });
    connect(more_, &QToolButton::clicked, this, [this] {
        StepsDialog dialog(commands_, steps_, this);
        if (dialog.exec() == QDialog::Accepted) {
            steps_ = dialog.steps();
            refresh();
            emit changed();
        }
    });
    refresh();
}

void SequenceEdit::setSequence(const StateSequence& steps) {
    steps_ = steps;
    refresh();
}

void SequenceEdit::refresh() {
    const QSignalBlocker blocker(combo_);
    combo_->clear();
    if (!steps_.empty() && !isSimple(steps_)) {
        combo_->addItem(sequenceText(steps_), stepsMarker());
        combo_->setItemData(kSteps, sequenceText(steps_), Qt::ToolTipRole);
    }
    combo_->addItem(optional_ ? tr("—") : tr("(choose a command)"), QString());
    for (const QString& command : commands_) {
        combo_->addItem(command, command);
    }
    if (steps_.empty()) {
        combo_->setCurrentIndex(combo_->findData(QString()));
    } else if (isSimple(steps_)) {
        const QString command = QString::fromStdString(steps_.front().command);
        int index = combo_->findData(command);
        if (index < 0) { // команды у устройства нет — показываем как есть (проверка подскажет)
            combo_->addItem(command, command);
            index = combo_->count() - 1;
        }
        combo_->setCurrentIndex(index);
    } else {
        combo_->setCurrentIndex(kSteps);
    }
    combo_->setToolTip(sequenceText(steps_));
}

// --- DeviceStatesDialog ---------------------------------------------------------

DeviceStatesDialog::DeviceStatesDialog(const model::Device& device, const DeviceStates& states,
                                       Section section, QWidget* parent)
    : QDialog(parent), deviceType_(device.type), section_(section) {
    const bool powerOnly = section == Section::Power;
    const QString deviceName = QString::fromStdString(device.displayName());
    setWindowTitle(powerOnly ? tr("Power — “%1”").arg(deviceName) : tr("Inputs — “%1”").arg(deviceName));
    setMinimumSize(560, powerOnly ? 380 : 560);
    for (const auto& command : device.commands) {
        if (!commands_.contains(QString::fromStdString(command.name))) {
            commands_ << QString::fromStdString(command.name);
        }
    }

    auto* title = new QLabel(windowTitle(), this);
    title->setObjectName("pageTitle");
    title->setWordWrap(true);
    auto* hint = new QLabel(
        powerOnly ? tr("How the remote switches the device on and off when an activity starts or ends.")
                  : tr("How the remote selects the device's input that an activity needs."),
        this);
    hint->setObjectName("pageMeta");
    hint->setWordWrap(true);

    // --- Питание ---
    auto* power = new QGroupBox(tr("Power"), this);
    auto* powerLayout = new QVBoxLayout(power);
    powerMode_ =
        modeButtons(power, powerLayout,
                    {tr("By hand — the remote does not switch it"),
                     tr("Separate commands for on and off"), tr("One button switches on and off")});
    powerDiscrete_ = new QWidget(power);
    auto* discreteForm = new QFormLayout(powerDiscrete_);
    discreteForm->setContentsMargins(24, 0, 0, 0);
    powerOn_ = new SequenceEdit(commands_, false, powerDiscrete_);
    powerOff_ = new SequenceEdit(commands_, false, powerDiscrete_);
    discreteForm->addRow(tr("On:"), powerOn_);
    discreteForm->addRow(tr("Off:"), powerOff_);
    powerToggle_ = new QWidget(power);
    auto* toggleForm = new QFormLayout(powerToggle_);
    toggleForm->setContentsMargins(24, 0, 0, 0);
    toggle_ = new SequenceEdit(commands_, false, powerToggle_);
    toggleForm->addRow(tr("Button:"), toggle_);
    powerDelayRow_ = new QWidget(power);
    auto* delayForm = new QFormLayout(powerDelayRow_);
    delayForm->setContentsMargins(24, 0, 0, 0);
    powerDelay_ = secondsBox(powerDelayRow_);
    powerDelay_->setToolTip(
        tr("Time the device needs to start before it reacts to other commands"));
    delayForm->addRow(tr("Wait after switching on:"), powerDelay_);
    powerLayout->addWidget(powerDiscrete_);
    powerLayout->addWidget(powerToggle_);
    powerLayout->addWidget(powerDelayRow_);

    // --- Входы ---
    auto* input = new QGroupBox(tr("Inputs"), this);
    auto* inputLayout = new QVBoxLayout(input);
    inputMode_ =
        modeButtons(input, inputLayout,
                    {tr("The remote does not switch inputs"), tr("Each input has its own command"),
                     tr("One command switches inputs in a circle")});
    inputBox_ = new QWidget(input);
    auto* boxLayout = new QVBoxLayout(inputBox_);
    boxLayout->setContentsMargins(24, 0, 0, 0);
    inputs_ = new QTableWidget(0, 2, inputBox_);
    inputs_->setHorizontalHeaderLabels({tr("Input"), tr("Command")});
    inputs_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    inputs_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    inputs_->setColumnWidth(0, 160);
    inputs_->verticalHeader()->hide();
    inputs_->setSelectionBehavior(QAbstractItemView::SelectRows);
    inputs_->setSelectionMode(QAbstractItemView::SingleSelection);
    auto* add = new QPushButton(tr("Add"), inputBox_);
    auto* remove = new QPushButton(tr("Delete"), inputBox_);
    auto* up = new QPushButton(tr("Up"), inputBox_);
    auto* down = new QPushButton(tr("Down"), inputBox_);
    auto* rowButtons = new QHBoxLayout;
    for (QPushButton* button : {add, remove, up, down}) {
        rowButtons->addWidget(button);
    }
    rowButtons->addStretch(1);
    cycle_ = new QWidget(inputBox_);
    auto* cycleForm = new QFormLayout(cycle_);
    cycleForm->setContentsMargins(0, 0, 0, 0);
    next_ = new SequenceEdit(commands_, false, cycle_);
    prev_ = new SequenceEdit(commands_, true, cycle_);
    cycleForm->addRow(tr("Next input:"), next_);
    cycleForm->addRow(tr("Previous input:"), prev_);
    inputDelay_ = secondsBox(inputBox_);
    auto* inputDelayForm = new QFormLayout;
    inputDelayForm->setContentsMargins(0, 0, 0, 0);
    inputDelayForm->addRow(tr("Wait after switching:"), inputDelay_);
    boxLayout->addWidget(inputs_, 1);
    boxLayout->addLayout(rowButtons);
    boxLayout->addWidget(cycle_);
    boxLayout->addLayout(inputDelayForm);
    inputLayout->addWidget(inputBox_, 1);

    // --- Кнопки ---
    auto* guess = new QPushButton(tr("Fill from commands"), this);
    guess->setToolTip(powerOnly ? tr("Guess from the command names: PowerOn/PowerOff or PowerToggle")
                                : tr("Guess from the command names: InputHdmi1, InputAv… or InputNext"));
    status_ = new QLabel(this);
    status_->setObjectName("pageMeta");
    status_->setWordWrap(true);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    save_ = new QPushButton(tr("Save"), this);
    save_->setObjectName("primaryButton");
    save_->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(guess);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(save_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(hint);
    // Показываем один раздел; заголовок группы повторял бы название окна — убираем.
    power->setVisible(powerOnly);
    input->setVisible(!powerOnly);
    power->setTitle(QString());
    input->setTitle(QString());
    layout->addWidget(power);
    layout->addWidget(input, 1);
    if (powerOnly) {
        layout->addStretch(1);
    }
    layout->addWidget(status_);
    layout->addLayout(buttons);

    connect(powerMode_, &QButtonGroup::idClicked, this, &DeviceStatesDialog::updateVisibility);
    connect(inputMode_, &QButtonGroup::idClicked, this, &DeviceStatesDialog::updateVisibility);
    for (SequenceEdit* edit : {powerOn_, powerOff_, toggle_, next_, prev_}) {
        connect(edit, &SequenceEdit::changed, this, &DeviceStatesDialog::updateVisibility);
    }
    connect(inputs_, &QTableWidget::itemChanged, this, &DeviceStatesDialog::updateVisibility);
    connect(add, &QPushButton::clicked, this, [this] {
        addInputRow({tr("Input %1").arg(inputs_->rowCount() + 1).toStdString(), {}});
        inputs_->setCurrentCell(inputs_->rowCount() - 1, 0);
        inputs_->editItem(inputs_->item(inputs_->rowCount() - 1, 0));
        updateVisibility();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (const int row = inputs_->currentRow(); row >= 0) {
            inputs_->removeRow(row);
            updateVisibility();
        }
    });
    auto move = [this](int delta) {
        const int row = inputs_->currentRow();
        const int to = row + delta;
        if (row < 0 || to < 0 || to >= inputs_->rowCount()) {
            return;
        }
        // Меняем местами содержимое строк (у второй колонки — виджет).
        auto* a = static_cast<SequenceEdit*>(inputs_->cellWidget(row, 1));
        auto* b = static_cast<SequenceEdit*>(inputs_->cellWidget(to, 1));
        const StateSequence steps = a->sequence();
        a->setSequence(b->sequence());
        b->setSequence(steps);
        const QString name = inputs_->item(row, 0)->text();
        inputs_->item(row, 0)->setText(inputs_->item(to, 0)->text());
        inputs_->item(to, 0)->setText(name);
        inputs_->setCurrentCell(to, 0);
    };
    connect(up, &QPushButton::clicked, this, [move] { move(-1); });
    connect(down, &QPushButton::clicked, this, [move] { move(1); });
    connect(guess, &QPushButton::clicked, this, [this] {
        std::vector<std::string> names;
        for (const QString& command : commands_) {
            names.push_back(command.toStdString());
        }
        // Заготовка только для открытого раздела: второй остаётся как был.
        const DeviceStates all = guessDeviceStates(names, deviceType_);
        DeviceStates guessed = this->states(); // параметр конструктора states перекрывает метод
        bool found = false;
        if (section_ == Section::Power) {
            guessed.power = all.power;
            found = all.power.mode != PowerSetup::Mode::Manual;
        } else {
            guessed.input = all.input;
            found = all.input.mode != InputSetup::Mode::None;
        }
        if (!found) {
            status_->setText(
                tr("No familiar command names (PowerOn, PowerToggle, InputHdmi1…) — choose the "
                   "commands yourself."));
            return;
        }
        load(guessed);
        status_->setText(tr("Filled from the command names — check and save."));
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save_, &QPushButton::clicked, this, [this] {
        if (problem().isEmpty()) {
            accept();
        }
    });

    load(states);
    resize(powerOnly ? QSize(620, 440) : QSize(680, 640));
}

void DeviceStatesDialog::addInputRow(const InputSetup::Input& input) {
    const int row = inputs_->rowCount();
    const QSignalBlocker blocker(inputs_);
    inputs_->insertRow(row);
    inputs_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(input.name)));
    auto* edit = new SequenceEdit(commands_, false, inputs_);
    edit->setSequence(input.select);
    connect(edit, &SequenceEdit::changed, this, &DeviceStatesDialog::updateVisibility);
    inputs_->setCellWidget(row, 1, edit);
}

void DeviceStatesDialog::load(const DeviceStates& states) {
    powerMode_->button(static_cast<int>(states.power.mode))->setChecked(true);
    powerOn_->setSequence(states.power.on);
    powerOff_->setSequence(states.power.off);
    toggle_->setSequence(states.power.toggle);
    powerDelay_->setValue(states.power.delayMs / double(kSeconds));

    inputMode_->button(static_cast<int>(states.input.mode))->setChecked(true);
    inputs_->setRowCount(0);
    for (const auto& item : states.input.inputs) {
        addInputRow(item);
    }
    next_->setSequence(states.input.next);
    prev_->setSequence(states.input.prev);
    inputDelay_->setValue(states.input.delayMs / double(kSeconds));
    updateVisibility();
}

void DeviceStatesDialog::updateVisibility() {
    const auto power = static_cast<PowerSetup::Mode>(powerMode_->checkedId());
    powerDiscrete_->setVisible(power == PowerSetup::Mode::Discrete);
    powerToggle_->setVisible(power == PowerSetup::Mode::Toggle);
    powerDelayRow_->setVisible(power != PowerSetup::Mode::Manual);

    const auto input = static_cast<InputSetup::Mode>(inputMode_->checkedId());
    inputBox_->setVisible(input != InputSetup::Mode::None);
    inputs_->setColumnHidden(1, input != InputSetup::Mode::Discrete);
    // По кругу — только имена: колонка имени на всю ширину.
    inputs_->horizontalHeader()->setSectionResizeMode(
        0, input == InputSetup::Mode::Discrete ? QHeaderView::Interactive : QHeaderView::Stretch);
    cycle_->setVisible(input == InputSetup::Mode::Cycle);

    const QString text = problem();
    save_->setEnabled(text.isEmpty());
    status_->setText(text);
}

QString DeviceStatesDialog::problem() const {
    // Проверяем только открытый раздел: второй не виден, и править его здесь нельзя.
    if (section_ == Section::Power) {
        const auto power = static_cast<PowerSetup::Mode>(powerMode_->checkedId());
        if (power == PowerSetup::Mode::Discrete &&
            (powerOn_->sequence().empty() || powerOff_->sequence().empty())) {
            return tr("Choose the commands for on and off.");
        }
        if (power == PowerSetup::Mode::Toggle && toggle_->sequence().empty()) {
            return tr("Choose the command that switches the device on and off.");
        }
        return {};
    }
    const auto input = static_cast<InputSetup::Mode>(inputMode_->checkedId());
    if (input == InputSetup::Mode::None) {
        return {};
    }
    if (inputs_->rowCount() == 0) {
        return tr("Add the device's inputs.");
    }
    QSet<QString> names;
    for (int row = 0; row < inputs_->rowCount(); ++row) {
        const QString name = inputs_->item(row, 0)->text().trimmed();
        if (name.isEmpty()) {
            return tr("An input has no name.");
        }
        if (names.contains(name)) {
            return tr("Two inputs are called “%1”.").arg(name);
        }
        names.insert(name);
        if (input == InputSetup::Mode::Discrete &&
            static_cast<SequenceEdit*>(inputs_->cellWidget(row, 1))->sequence().empty()) {
            return tr("Choose the command for the input “%1”.").arg(name);
        }
    }
    if (input == InputSetup::Mode::Cycle && next_->sequence().empty()) {
        return tr("Choose the command that switches to the next input.");
    }
    return {};
}

DeviceStates DeviceStatesDialog::states() const {
    DeviceStates states;
    states.power.mode = static_cast<PowerSetup::Mode>(powerMode_->checkedId());
    if (states.power.mode == PowerSetup::Mode::Discrete) {
        states.power.on = powerOn_->sequence();
        states.power.off = powerOff_->sequence();
    } else if (states.power.mode == PowerSetup::Mode::Toggle) {
        states.power.toggle = toggle_->sequence();
    }
    if (states.power.mode != PowerSetup::Mode::Manual) {
        states.power.delayMs = static_cast<int>(powerDelay_->value() * kSeconds);
    }
    states.input.mode = static_cast<InputSetup::Mode>(inputMode_->checkedId());
    if (states.input.mode != InputSetup::Mode::None) {
        for (int row = 0; row < inputs_->rowCount(); ++row) {
            InputSetup::Input item;
            item.name = inputs_->item(row, 0)->text().trimmed().toStdString();
            if (states.input.mode == InputSetup::Mode::Discrete) {
                item.select = static_cast<SequenceEdit*>(inputs_->cellWidget(row, 1))->sequence();
            }
            states.input.inputs.push_back(std::move(item));
        }
        if (states.input.mode == InputSetup::Mode::Cycle) {
            states.input.next = next_->sequence();
            states.input.prev = prev_->sequence();
        }
        states.input.delayMs = static_cast<int>(inputDelay_->value() * kSeconds);
    }
    return states;
}

} // namespace harmony::app
