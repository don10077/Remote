/**
 * @file buttonDialog.cpp
 * @brief Окно кнопки активности или устройства.
 */

#include "buttonDialog.h"

#include "../icons.h"
#include "../labels.h"
#include "../modern/remotePanel.h"
#include "../modern/remoteView.h"
#include "harmony/core/buttonIcons.h"


#include <QComboBox>
#include <QPainter>
#include <QHash>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace harmony::app {

namespace {

/// Условный предпросмотр значка прошивки; неизвестный id не подменяем выдуманной картинкой.
QIcon buttonIconPreview(const QString& id, const QPalette& palette) {
    static const QHash<QString, QString> symbols = {
        {"play", "▶"}, {"pause", "Ⅱ"}, {"stop", "■"}, {"stop2", "■"}, {"record", "●"},
        {"fastforward", "▶▶"}, {"rewind", "◀◀"}, {"skipBack", "|◀"}, {"skipForward", "▶|"},
        {"eject", "⏏"}, {"heart", "♥"}, {"star", "★"}, {"circle", "○"}, {"square", "□"},
        {"triangle", "△"}, {"PS-Circle", "○"}, {"PS-Cross", "×"}, {"PS-Square", "□"},
        {"PS_Triangle", "△"}, {"cbox_circle_default", "○"}, {"cbox_square_default", "□"},
        {"cbox_triangle_default", "△"}, {"back", "←"}, {"repeat", "↻"}, {"sbRepeat", "↻"},
        {"replay", "↶"}, {"shuffle", "⤨"}, {"sbShuffle", "⤨"}, {"3musicNote", "♫"},
        {"myMusic", "♫"}, {"SoundEffect", "♫"}, {"SoundMode", "♫"}, {"mail", "✉"},
        {"powerOnOff", "⏻"}, {"powerOFF", "⏻"}, {"powerON", "⏻"}, {"off", "⏻"}, {"on", "⏻"},
        {"clock", "◷"}, {"sleep", "☾"}, {"list", "☷"}, {"playList", "☷"}, {"sbFavorites", "★"},
        {"sbAdd", "+"}, {"A", "A"}, {"B", "B"}, {"X", "X"}, {"Y", "Y"},
        {"closedCaption", "CC"}, {"Subtitle", "CC"}, {"myTV", "TV"}, {"live", "TV"},
        {"topMenu", "☰"}, {"aspect", "▭"}, {"switchDisplay", "▭"}, {"start", "▶"},
        {"slowPlay", "▷"}, {"slowMotion", "▷"}
    };
    QString symbol = symbols.value(id);
    QColor color = palette.color(QPalette::Text);
    const QHash<QString, QColor> colors{{"red", QColor("#cc4444")}, {"green", QColor("#32975a")},
        {"yellow", QColor("#c39b28")}, {"blue", QColor("#3b79c4")}, {"purple", QColor("#9759bc")}};
    for (auto it = colors.cbegin(); it != colors.cend(); ++it) if (id.contains(it.key())) { symbol = "●"; color = it.value(); }
    if (symbol.isEmpty()) return {};
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font("Segoe UI Symbol");
    font.setPixelSize(symbol.size() > 1 ? 26 : 42);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, symbol);
    return QIcon(pixmap);
}


/// Индекс устройства в конфигурации (для цвета, как на пульте); -1 — нет.
int deviceIndex(const model::RemoteConfig& config, const std::string& id) {
    for (int i = 0; i < static_cast<int>(config.devices.size()); ++i) {
        if (config.devices[static_cast<std::size_t>(i)].id == id) {
            return i;
        }
    }
    return -1;
}

} // namespace

ButtonDialog::ButtonDialog(const model::RemoteConfig& config, const ButtonOwner& owner, ButtonRef button,
                           QWidget* parent, Macro initialMacro)
    : QDialog(parent), config_(config), initial_(std::move(button)), macro_(std::move(initialMacro)) {
    // Кнопки владельца и кому они могут слать команды.
    static const std::vector<model::ControlGroup> kNoGroups;
    groups_ = &kNoGroups;
    QString hintText;
    if (owner.isDevice()) {
        if (const model::Device* device = config_.findDevice(owner.id)) {
            groups_ = &device->controlGroups;
            devices_ = {device->id};
            hintText = tr("Device “%1” — its buttons in the remote's “Devices” mode.")
                           .arg(QString::fromStdString(device->displayName()));
        }
    } else if (const model::Activity* activity = config_.findActivity(owner.id)) {
        groups_ = &activity->controlGroups;
        devices_ = activity->powerOn;
        hintText = tr("Activity “%1”. The button sends a command to one of its devices.")
                       .arg(QString::fromStdString(activity->label));
    }

    const bool isNew = initial_.soft ? initial_.position < 0 : currentAction(initial_).empty();
    const QString caption = initial_.soft ? (isNew ? tr("New screen button") : tr("Screen button"))
                                          : tr("Remote button");
    setWindowTitle(caption);
    setMinimumSize(520, 560);

    auto* title = new QLabel(caption, this);
    title->setObjectName("pageTitle");
    auto* hint = new QLabel(hintText, this);
    hint->setObjectName("pageMeta");
    hint->setWordWrap(true);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    // Аппаратная — выбор кнопки (с текущим назначением), экранная — подпись.
    if (initial_.soft) {
        label_ = new QLineEdit(this);
        const model::Button* existing = nullptr;
        for (const auto& group : *groups_) {
            for (const auto& item : group.buttons) {
                if (item.name.empty() && item.position == initial_.position) {
                    existing = &item;
                }
            }
        }
        if (existing != nullptr) {
            label_->setText(QString::fromStdString(existing->label));
        }
        label_->setPlaceholderText(tr("empty — command name"));
        form->addRow(tr("Label:"), label_);

        // Значок вместо подписи: только из тех, что есть в прошивке пульта.
        icon_ = new QComboBox(this);
        icon_->addItem(tr("No icon — show the label"), QString());
        for (const std::string_view name : buttonIcons()) {
            const QString id = QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
            icon_->addItem(buttonIconPreview(id, palette()), id, id);
        }
        const QString current = existing != nullptr ? QString::fromStdString(existing->icon) : QString();
        if (!current.isEmpty() && icon_->findData(current) < 0) {
            icon_->addItem(tr("%1 (not in the remote)").arg(current), current); // показываем как есть
        }
        icon_->setCurrentIndex(std::max(0, icon_->findData(current)));
        icon_->setToolTip(tr("The icon replaces the label on the remote's screen"));
        icon_->setIconSize(QSize(28, 28));
        form->addRow(tr("Icon:"), icon_);
        auto* preview = new QLabel(this);
        preview->setMinimumHeight(48);
        auto updatePreview = [this, preview] {
            const auto image = icon_->itemIcon(icon_->currentIndex());
            if (icon().empty()) preview->clear();
            else if (image.isNull()) preview->setText(tr("Firmware icon; preview unavailable."));
            else preview->setPixmap(image.pixmap(48, 48));
        };
        connect(icon_, &QComboBox::currentIndexChanged, this, updatePreview);
        form->addRow(tr("Preview:"), preview);
        auto* previewHint = new QLabel(tr("Symbolic preview. The remote uses its own firmware artwork."), this);
        previewHint->setWordWrap(true);
        previewHint->setObjectName("pageMeta");
        form->addRow({}, previewHint);
        updatePreview();
        connect(icon_, &QComboBox::currentIndexChanged, this, [this] {
            label_->setEnabled(icon().empty());
            updateState();
        });
        label_->setEnabled(icon().empty());
    } else {
        key_ = new QComboBox(this);
        for (const QString& id : RemoteView::hardKeyIds(
                 QString::fromStdString(config_.property("HarmonyTargetRemote")))) {
            const std::string action = currentAction(ButtonRef::hard(id));
            QString text = RemoteView::keyName(id);
            if (!action.empty()) {
                text += "  —  " + labels::action(config_, action).section(" · ", 0, 1);
            }
            key_->addItem(text, id);
        }
        key_->setCurrentIndex(std::max(key_->findData(initial_.key), 0));
        form->addRow(tr("Button:"), key_);
    }

    // Кому можно слать команды; цвет — как у кнопок устройства на пульте.
    device_ = new QComboBox(this);
    for (const auto& id : devices_) {
        if (const model::Device* device = config_.findDevice(id)) {
            device_->addItem(icons::make(icons::Icon::Dot, RemotePanel::deviceColour(deviceIndex(config_, id))),
                             QString::fromStdString(device->displayName()), QString::fromStdString(id));
        }
    }
    form->addRow(tr("Device:"), device_);

    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(tr("Find command…"));
    filter_->setClearButtonEnabled(true);
    commands_ = new QListWidget(this);
    commands_->setSpacing(1);
    commands_->setAlternatingRowColors(true);
    form->addRow(tr("Command:"), filter_);

    // Одна команда или макрос: несколько шагов подряд (команды разным устройствам, паузы).
    single_ = new QRadioButton(tr("One command"), this);
    multi_ = new QRadioButton(tr("Several steps (macro)"), this);
    auto* modeRow = new QHBoxLayout;
    modeRow->addWidget(single_);
    modeRow->addWidget(multi_);
    modeRow->addStretch(1);
    form->insertRow(form->rowCount() - 2, tr("Sends:"), modeRow);

    macroBox_ = new QWidget(this);
    auto* macroLayout = new QVBoxLayout(macroBox_);
    macroLayout->setContentsMargins(0, 0, 0, 0);
    auto* macroHint = new QLabel(tr("Steps run one after another. Pick a command above and press “Add command”; "
                                    "add a pause where a device needs time."),
                                 macroBox_);
    macroHint->setObjectName("pageMeta");
    macroHint->setWordWrap(true);
    steps_ = new QListWidget(macroBox_);
    steps_->setAlternatingRowColors(true);
    auto* addCommand = new QPushButton(tr("Add command"), macroBox_);
    pause_ = new QSpinBox(macroBox_);
    pause_->setRange(100, 30000);
    pause_->setSingleStep(100);
    pause_->setValue(500);
    pause_->setSuffix(tr(" ms"));
    auto* addPause = new QPushButton(tr("Add pause"), macroBox_);
    auto* up = new QPushButton(tr("Up"), macroBox_);
    auto* down = new QPushButton(tr("Down"), macroBox_);
    auto* removeStep = new QPushButton(tr("Delete step"), macroBox_);
    auto* stepButtons = new QHBoxLayout;
    stepButtons->addWidget(addCommand);
    stepButtons->addWidget(pause_);
    stepButtons->addWidget(addPause);
    stepButtons->addStretch(1);
    stepButtons->addWidget(up);
    stepButtons->addWidget(down);
    stepButtons->addWidget(removeStep);
    macroLayout->addWidget(macroHint);
    macroLayout->addWidget(steps_, 1);
    macroLayout->addLayout(stepButtons);

    connect(addCommand, &QPushButton::clicked, this, [this] {
        if (command().empty()) return;
        MacroStep step;
        step.deviceId = deviceId();
        step.step = StateStep::press(command());
        macro_.push_back(std::move(step));
        fillSteps(static_cast<int>(macro_.size()) - 1);
    });
    connect(addPause, &QPushButton::clicked, this, [this] {
        // Пауза — у устройства предыдущего шага (так пишет её Logitech), иначе у выбранного.
        MacroStep step;
        step.deviceId = macro_.empty() ? deviceId() : macro_.back().deviceId;
        step.step = StateStep::pause(pause_->value());
        if (step.deviceId.empty()) return;
        macro_.push_back(std::move(step));
        fillSteps(static_cast<int>(macro_.size()) - 1);
    });
    auto moveStep = [this](int delta) {
        const int row = steps_->currentRow();
        const int to = row + delta;
        if (row < 0 || to < 0 || to >= static_cast<int>(macro_.size())) return;
        std::swap(macro_[static_cast<std::size_t>(row)], macro_[static_cast<std::size_t>(to)]);
        fillSteps(to);
    };
    connect(up, &QPushButton::clicked, this, [moveStep] { moveStep(-1); });
    connect(down, &QPushButton::clicked, this, [moveStep] { moveStep(1); });
    connect(removeStep, &QPushButton::clicked, this, [this] {
        const int row = steps_->currentRow();
        if (row < 0 || row >= static_cast<int>(macro_.size())) return;
        macro_.erase(macro_.begin() + row);
        fillSteps(std::min(row, static_cast<int>(macro_.size()) - 1));
    });

    note_ = new QLabel(this);
    note_->setObjectName("pageMeta");
    note_->setWordWrap(true);

    auto* remove = new QPushButton(initial_.soft ? tr("Delete button") : tr("Clear assignment"), this);
    remove->setObjectName("pageActionDanger");
    remove->setVisible(!isNew);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    ok_ = new QPushButton(tr("Done"), this);
    ok_->setObjectName("primaryButton");
    ok_->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(remove);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(ok_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(8);
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addSpacing(6);
    layout->addLayout(form);
    layout->addWidget(commands_, 1);
    layout->addWidget(macroBox_, 1);
    layout->addWidget(note_);
    layout->addLayout(buttons);

    // Начальный выбор — текущее назначение кнопки.
    const auto [deviceId, command] = splitActionId(currentAction(initial_));
    if (const int index = device_->findData(QString::fromStdString(deviceId)); index >= 0) {
        device_->setCurrentIndex(index);
    }
    selectedCommand_ = command;
    fillCommands();

    connect(device_, &QComboBox::currentIndexChanged, this, &ButtonDialog::fillCommands);
    connect(filter_, &QLineEdit::textChanged, this, &ButtonDialog::fillCommands);
    connect(commands_, &QListWidget::currentTextChanged, this, [this](const QString& text) {
        if (!text.isEmpty()) {
            selectedCommand_ = text.toStdString();
        }
        updateState();
    });
    // Двойной щелчок: одна команда — готово; макрос — добавить шаг.
    connect(commands_, &QListWidget::itemDoubleClicked, this, [this, addCommand] {
        if (macroChosen()) {
            addCommand->click();
        } else {
            accept();
        }
    });
    if (key_ != nullptr) {
        connect(key_, &QComboBox::currentIndexChanged, this, &ButtonDialog::updateState);
    }
    connect(remove, &QPushButton::clicked, this, [this] {
        remove_ = true;
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok_, &QPushButton::clicked, this, &QDialog::accept);

    const bool startMacro = isMacroAction(currentAction(initial_));
    (startMacro ? multi_ : single_)->setChecked(true);
    macroBox_->setVisible(startMacro);
    fillSteps(-1);
    connect(multi_, &QRadioButton::toggled, this, [this](bool on) {
        macroBox_->setVisible(on);
        updateState();
    });
    if (label_ != nullptr) {
        connect(label_, &QLineEdit::textChanged, this, &ButtonDialog::updateState);
    }
    updateState();
    filter_->setFocus();
}

bool ButtonDialog::macroChosen() const {
    return multi_ != nullptr && multi_->isChecked();
}

QString ButtonDialog::stepText(const MacroStep& item) const {
    const model::Device* device = config_.findDevice(item.deviceId);
    const QString name = device != nullptr ? QString::fromStdString(device->displayName())
                                           : QString::fromStdString(item.deviceId);
    const StateStep& step = item.step;
    switch (step.kind) {
    case StateStep::Kind::Command:
        return name + " · " + QString::fromStdString(step.command) +
               (step.durationMs > 0 ? tr(" (held %1 ms)").arg(step.durationMs) : QString());
    case StateStep::Kind::Delay:
        return tr("Pause %1 ms").arg(step.delayMs);
    case StateStep::Kind::SetValue:
        return name + " · " + QString::fromStdString(step.state) + " = " + QString::fromStdString(step.value);
    case StateStep::Kind::Other:
        break;
    }
    return tr("Other step (kept as is)");
}

void ButtonDialog::fillSteps(int select) {
    steps_->clear();
    for (std::size_t i = 0; i < macro_.size(); ++i) {
        steps_->addItem(QString::number(i + 1) + ". " + stepText(macro_[i]));
    }
    if (select >= 0 && select < steps_->count()) {
        steps_->setCurrentRow(select);
    }
    updateState();
}

std::string ButtonDialog::currentAction(const ButtonRef& button) const {
    for (const auto& group : *groups_) {
        for (const auto& item : group.buttons) {
            const bool match = button.soft ? item.name.empty() && item.position == button.position
                                           : !button.key.isEmpty() && item.name == button.key.toStdString();
            if (match) {
                return item.actionId;
            }
        }
    }
    return {};
}

void ButtonDialog::fillCommands() {
    const QSignalBlocker blocker(commands_);
    commands_->clear();
    const model::Device* device = config_.findDevice(deviceId());
    if (device == nullptr) {
        updateState();
        return;
    }
    const QString filter = filter_->text().trimmed();
    for (const auto& command : device->commands) {
        const QString name = QString::fromStdString(command.name);
        if (filter.isEmpty() || name.contains(filter, Qt::CaseInsensitive)) {
            commands_->addItem(name);
            if (command.name == selectedCommand_) {
                commands_->setCurrentRow(commands_->count() - 1);
            }
        }
    }
    // Одна подходящая команда после поиска — сразу выбрана.
    if (commands_->currentRow() < 0 && commands_->count() == 1) {
        commands_->setCurrentRow(0);
    }
    if (commands_->currentItem() != nullptr) {
        commands_->scrollToItem(commands_->currentItem());
    }
    updateState();
}

void ButtonDialog::updateState() {
    if (ok_ == nullptr || multi_ == nullptr) {
        return; // окно ещё собирается
    }
    if (macroChosen()) {
        const bool labelled = label_ == nullptr || !label().empty() || !icon().empty();
        ok_->setEnabled(!macro_.empty() && labelled && (key_ == nullptr || key_->currentIndex() >= 0));
        note_->setText(macro_.empty() ? tr("Add at least one step.")
                       : !labelled    ? tr("A screen button with several steps needs a label.")
                                      : QString());
        return;
    }
    ok_->setEnabled(!command().empty() && (key_ == nullptr || key_->currentIndex() >= 0));
    if (device_->count() == 0) {
        note_->setText(tr("Commands have nowhere to go: the activity includes no devices."));
        return;
    }
    if (key_ != nullptr) {
        const ButtonRef chosen = button();
        const std::string action = currentAction(chosen);
        if (chosen.key != initial_.key && !action.empty()) {
            note_->setText(tr("Currently “%1” sends: %2 — the assignment will be replaced.")
                               .arg(RemoteView::keyName(chosen.key), labels::action(config_, action)));
            return;
        }
    }
    note_->setText(command().empty() ? tr("Select a command in the list.") : QString());
}

ButtonRef ButtonDialog::button() const {
    if (initial_.soft) {
        return initial_;
    }
    return ButtonRef::hard(key_->currentData().toString());
}

std::string ButtonDialog::label() const {
    return label_ != nullptr && icon().empty() ? label_->text().trimmed().toStdString() : std::string();
}

std::string ButtonDialog::icon() const {
    return icon_ != nullptr ? icon_->currentData().toString().toStdString() : std::string();
}

std::string ButtonDialog::deviceId() const {
    return device_->currentData().toString().toStdString();
}

std::string ButtonDialog::command() const {
    const auto* item = commands_->currentItem();
    return item != nullptr ? item->text().toStdString() : std::string();
}

} // namespace harmony::app
