/**
 * @file activityDialog.cpp
 * @brief Окно новой активности и правки её состава.
 */

#include "activityDialog.h"

#include "../icons.h"
#include "../labels.h"
#include "../nameCheck.h"
#include "../modern/activityIcon.h"
#include "../modern/remotePanel.h"
#include "../modern/remoteView.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace harmony::app {

namespace {

constexpr int kDeviceIdRole = Qt::UserRole + 1; ///< Id устройства (QString)

/// Типы устройств, которые обычно показывают картинку / отвечают за звук.
bool isDisplay(const std::string& type) {
    return type == "Television" || type == "Projector" || type == "Monitor";
}
bool isAudio(const std::string& type) {
    return type == "Receiver" || type == "Amplifier" || type == "AudioVideoReceiver" ||
           type == "SoundBar";
}

/// Первое устройство из списка, подходящее под условие; иначе — пусто.
std::string firstWhere(const model::RemoteConfig& config, const std::vector<std::string>& ids,
                       bool (*predicate)(const std::string&), bool expected) {
    for (const auto& id : ids) {
        const model::Device* device = config.findDevice(id);
        if (device != nullptr && predicate(device->type) == expected) {
            return id;
        }
    }
    return {};
}

/// Заголовок группы полей окна.
QLabel* sectionLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName("sectionHeading");
    return label;
}

} // namespace

ActivityDialog::ActivityDialog(const model::RemoteConfig& config, const model::Activity* existing,
                               QWidget* parent)
    : QDialog(parent), config_(config), existing_(existing) {
    const QString caption = existing_ != nullptr ? tr("Edit activity") : tr("Add activity");
    setWindowTitle(caption);
    setMinimumSize(720, 560);

    auto* title = new QLabel(caption, this);
    title->setObjectName("pageTitle");
    auto* hint = new QLabel(existing_ != nullptr
                                ? tr("Ticked devices turn on at start, the others turn off. Buttons and "
                                     "start steps of removed devices will be deleted.")
                                : tr("Choose the devices and what each is responsible for — the remote's "
                                     "buttons are assigned automatically from the devices' commands."),
                            this);
    hint->setObjectName("pageMeta");
    hint->setWordWrap(true);

    // --- Имя и тип ---
    name_ = new QLineEdit(this);
    type_ = new QComboBox(this);
    type_->setIconSize(QSize(28, 22));
    for (const auto& type : activityTypes()) {
        type_->addItem(activityIcon(QString::fromStdString(type)), labels::activityType(type),
                       QString::fromStdString(type));
    }
    name_->setText(type_->currentText());
    auto* general = new QFormLayout;
    general->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    general->addRow(tr("Name:"), name_);
    nameProblem_ = new QLabel(tr("Another activity already has this name: the remote shows only names."), this);
    nameProblem_->setObjectName("fieldProblem");
    nameProblem_->setWordWrap(true);
    nameProblem_->setVisible(false);
    general->addRow(QString(), nameProblem_);
    general->addRow(tr("Type:"), type_);

    // --- Устройства ---
    devices_ = new QListWidget(this);
    devices_->setToolTip(tr("Ticked devices turn on when the activity starts, the others turn off"));
    for (int i = 0; i < static_cast<int>(config_.devices.size()); ++i) {
        const auto& device = config_.devices[static_cast<std::size_t>(i)];
        // Цветная метка — тот же цвет, что у кнопок устройства на пульте.
        auto* item = new QListWidgetItem(icons::make(icons::Icon::Dot, RemotePanel::deviceColour(i)),
                                         QString::fromStdString(device.displayName()) + "  ·  " +
                                             labels::deviceType(device.type));
        item->setData(kDeviceIdRole, QString::fromStdString(device.id));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        devices_->addItem(item);
    }

    // --- Роли ---
    control_ = new QComboBox(this);
    volume_ = new QComboBox(this);
    display_ = new QComboBox(this);
    auto* roles = new QFormLayout;
    roles->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    roles->addRow(tr("Control:"), control_);
    roles->addRow(tr("Volume:"), volume_);
    roles->addRow(tr("Picture:"), display_);
    control_->setToolTip(tr("Menu, arrows, OK, playback, channels and numbers"));
    volume_->setToolTip(tr("Volume + and −, mute"));
    display_->setToolTip(tr("The device that shows the picture"));

    // --- Входы ---
    inputsForm_ = new QFormLayout;
    inputsForm_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    inputsHint_ = new QLabel(tr("The ticked devices have no switchable inputs."), this);
    inputsHint_->setObjectName("pageMeta");

    preview_ = new QLabel(this);
    preview_->setObjectName("pageMeta");
    preview_->setWordWrap(true);

    if (existing_ != nullptr) {
        reassign_ = new QCheckBox(tr("Reassign remote buttons by role"), this);
        reassign_->setToolTip(tr("Hardware buttons will be assigned as for a new activity: volume to "
                                 "the volume device, the rest to the control device"));
    }

    auto* cancel = new QPushButton(tr("Cancel"), this);
    create_ = new QPushButton(existing_ != nullptr ? tr("Save") : tr("Create"), this);
    create_->setObjectName("primaryButton");
    create_->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(create_);

    // Слева — что и из чего, справа — кто за что отвечает.
    auto* left = new QVBoxLayout;
    left->addLayout(general);
    left->addSpacing(6);
    left->addWidget(sectionLabel(tr("DEVICES"), this));
    left->addWidget(devices_, 1);
    auto* right = new QVBoxLayout;
    right->addWidget(sectionLabel(tr("ROLES"), this));
    right->addLayout(roles);
    right->addSpacing(6);
    right->addWidget(sectionLabel(tr("INPUTS AT START"), this));
    right->addLayout(inputsForm_);
    right->addWidget(inputsHint_);
    right->addStretch(1);
    auto* columns = new QHBoxLayout;
    columns->setSpacing(20);
    columns->addLayout(left, 1);
    columns->addLayout(right, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(8);
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addSpacing(6);
    layout->addLayout(columns, 1);
    if (reassign_ != nullptr) {
        layout->addWidget(reassign_);
    }
    layout->addWidget(preview_);
    layout->addLayout(buttons);

    connect(name_, &QLineEdit::textEdited, this, [this] { nameEdited_ = true; });
    connect(name_, &QLineEdit::textChanged, this, &ActivityDialog::updatePreview);
    connect(type_, &QComboBox::currentIndexChanged, this, [this] {
        if (!nameEdited_) {
            name_->setText(type_->currentText());
        }
    });
    connect(devices_, &QListWidget::itemChanged, this, &ActivityDialog::devicesChanged);
    connect(devices_, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* item) {
        item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    });
    for (QComboBox* combo : {control_, volume_, display_}) {
        connect(combo, &QComboBox::currentIndexChanged, this, &ActivityDialog::updatePreview);
        // activated — только выбор мышью или клавиатурой, не заполнение программой.
        connect(combo, &QComboBox::activated, combo, [combo] { combo->setProperty("chosen", true); });
    }
    if (reassign_ != nullptr) {
        connect(reassign_, &QCheckBox::toggled, this, &ActivityDialog::updatePreview);
    }
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(create_, &QPushButton::clicked, this, &QDialog::accept);

    devicesChanged();
    if (existing_ != nullptr) {
        prefill(draftOf(*existing_));
    }
}

void ActivityDialog::prefill(const ActivityDraft& draft) {
    nameEdited_ = true; // имя уже есть — тип его больше не подставляет
    {
        const QSignalBlocker blockName(name_);
        const QSignalBlocker blockType(type_);
        name_->setText(QString::fromStdString(draft.label));
        type_->setCurrentIndex(std::max(type_->findData(QString::fromStdString(draft.type)), 0));
    }
    {
        const QSignalBlocker blocker(devices_);
        for (int row = 0; row < devices_->count(); ++row) {
            QListWidgetItem* item = devices_->item(row);
            const std::string id = item->data(kDeviceIdRole).toString().toStdString();
            item->setCheckState(std::ranges::find(draft.devices, id) != draft.devices.end() ? Qt::Checked
                                                                                              : Qt::Unchecked);
        }
    }
    devicesChanged();

    // Роли и входы — как сейчас в активности; считаются выбором пользователя.
    auto setRole = [](QComboBox* combo, const std::string& id) {
        const QSignalBlocker blocker(combo);
        combo->setCurrentIndex(std::max(combo->findData(QString::fromStdString(id)), 0));
        combo->setProperty("chosen", true);
    };
    setRole(control_, draft.controlDevice);
    setRole(volume_, draft.volumeDevice.empty() ? draft.controlDevice : draft.volumeDevice);
    setRole(display_, draft.displayDevice);
    for (const auto& [deviceId, input] : draft.inputs) {
        const auto found = inputs_.find(deviceId);
        if (found != inputs_.end()) {
            found->second->setCurrentIndex(std::max(found->second->findData(QString::fromStdString(input)), 0));
        }
    }
    updatePreview();
}

bool ActivityDialog::reassignButtons() const {
    return reassign_ != nullptr && reassign_->isChecked();
}

std::vector<std::string> ActivityDialog::checkedDevices() const {
    std::vector<std::string> ids;
    for (int row = 0; row < devices_->count(); ++row) {
        const QListWidgetItem* item = devices_->item(row);
        if (item->checkState() == Qt::Checked) {
            ids.push_back(item->data(kDeviceIdRole).toString().toStdString());
        }
    }
    return ids;
}

void ActivityDialog::fillRole(QComboBox* combo, const std::vector<std::string>& ids,
                                 const std::string& preferred, bool allowNone) const {
    const QSignalBlocker blocker(combo);
    const std::string previous = combo->currentData().toString().toStdString();
    combo->clear();
    if (allowNone) {
        combo->addItem(tr("— none —"), QString());
    }
    for (const auto& id : ids) {
        combo->addItem(QString::fromStdString(config_.findDevice(id)->displayName()),
                       QString::fromStdString(id));
    }
    // Выбор пользователя сохраняется, пока устройство отмечено; иначе — догадка по типам.
    const bool keep = combo->property("chosen").toBool() && std::ranges::find(ids, previous) != ids.end();
    const int index = combo->findData(QString::fromStdString(keep ? previous : preferred));
    combo->setCurrentIndex(std::max(index, 0));
    combo->setEnabled(!ids.empty());
}

void ActivityDialog::devicesChanged() {
    const auto ids = checkedDevices();

    // Догадки по типам: управляет источник (не экран и не звук), звук — ресивер, картинка — экран.
    std::string control = firstWhere(config_, ids, isDisplay, false);
    if (!control.empty() && isAudio(config_.findDevice(control)->type)) {
        control.clear();
        for (const auto& id : ids) {
            const auto& type = config_.findDevice(id)->type;
            if (!isDisplay(type) && !isAudio(type)) {
                control = id;
                break;
            }
        }
    }
    if (control.empty() && !ids.empty()) {
        control = ids.front();
    }
    fillRole(control_, ids, control, false);
    // Громкость — у звукового устройства, а если его нет — у того, кто реально управляет.
    std::string volume = firstWhere(config_, ids, isAudio, true);
    if (volume.empty()) {
        volume = control_->currentData().toString().toStdString();
    }
    fillRole(volume_, ids, volume, false);
    fillRole(display_, ids, firstWhere(config_, ids, isDisplay, true), true);

    // Входы: по строке на каждое отмеченное устройство, у которого они есть.
    std::map<std::string, QString> previous;
    for (const auto& [id, combo] : inputs_) {
        previous[id] = combo->currentData().toString();
    }
    while (inputsForm_->rowCount() > 0) {
        inputsForm_->removeRow(0);
    }
    inputs_.clear();
    for (const auto& id : ids) {
        const model::Device* device = config_.findDevice(id);
        if (device->inputs.empty()) {
            continue;
        }
        auto* combo = new QComboBox(this);
        combo->addItem(tr("don't switch"), QString());
        for (const auto& input : device->inputs) {
            combo->addItem(QString::fromStdString(input), QString::fromStdString(input));
        }
        combo->setCurrentIndex(std::max(combo->findData(previous[id]), 0));
        inputsForm_->addRow(QString::fromStdString(device->displayName()) + ":", combo);
        inputs_[id] = combo;
    }
    inputsHint_->setVisible(inputs_.empty() && !ids.empty());
    updatePreview();
}

ActivityDraft ActivityDialog::draft() const {
    ActivityDraft draft;
    draft.label = name_->text().trimmed().toStdString();
    draft.type = type_->currentData().toString().toStdString();
    draft.devices = checkedDevices();
    draft.controlDevice = control_->currentData().toString().toStdString();
    draft.volumeDevice = volume_->currentData().toString().toStdString();
    draft.displayDevice = display_->currentData().toString().toStdString();
    for (const auto& id : draft.devices) {
        const auto found = inputs_.find(id);
        if (found != inputs_.end() && !found->second->currentData().toString().isEmpty()) {
            draft.inputs.emplace_back(id, found->second->currentData().toString().toStdString());
        }
    }
    return draft;
}

void ActivityDialog::updatePreview() {
    const ActivityDraft current = draft();
    const bool nameTaken = activityNameTaken(config_, QString::fromStdString(current.label),
                                             existing_ != nullptr ? existing_->id : std::string());
    nameProblem_->setVisible(nameTaken);
    if (current.devices.empty()) {
        preview_->setText(tr("Tick the devices needed in this activity."));
        create_->setEnabled(false);
        return;
    }
    const auto plan = planHardButtons(config_, current);
    const auto total = static_cast<qsizetype>(RemoteView::hardKeyIds().size());
    QStringList keys;
    for (const auto& assignment : plan) {
        keys << RemoteView::keyName(QString::fromStdString(assignment.key)) + " → " +
                    QString::fromStdString(assignment.command);
    }
    QString text;
    const QStringList removed = removedDevices(current);
    if (existing_ != nullptr && !reassignButtons()) {
        // Правка без переназначения: важно, что пропадёт вместе с убранными устройствами.
        const int lost = buttonsLostBy(*existing_, current);
        text = !removed.isEmpty()
                   ? tr("Removed: %1. Their buttons (%2) and start steps in this activity, including input "
                        "switching, will be deleted.")
                         .arg(removed.join(", "))
                         .arg(lost)
               : lost > 0 ? tr("Buttons of removed devices will be deleted: %1.").arg(lost)
                          : tr("The activity's buttons stay as they are.");
    } else {
        text = plan.empty() ? tr("The control device has no commands with suitable names — the remote's "
                                 "buttons won't be assigned, the activity will only turn devices on.")
                            : tr("Remote buttons will be assigned automatically: %1 of %2.")
                                  .arg(plan.size())
                                  .arg(total);
        if (!removed.isEmpty()) {
            text = tr("Removed: %1. Their start steps in this activity, including input switching, will be "
                      "deleted.")
                       .arg(removed.join(", ")) +
                   QLatin1Char(' ') + text;
        }
    }
    preview_->setText(text);
    preview_->setToolTip(keys.join("\n"));
    create_->setEnabled(!current.label.empty() && !nameTaken);
}

QStringList ActivityDialog::removedDevices(const ActivityDraft& draft) const {
    QStringList names;
    if (existing_ == nullptr) {
        return names;
    }
    for (const auto& id : existing_->powerOn) {
        if (std::ranges::find(draft.devices, id) == draft.devices.end()) {
            const model::Device* device = config_.findDevice(id);
            names << "«" + QString::fromStdString(device != nullptr ? device->displayName() : id) + "»";
        }
    }
    return names;
}

void ActivityDialog::accept() {
    // Убрать устройство из активности — правка заметная: переспрашиваем, как при удалении.
    const QStringList removed = removedDevices(draft());
    if (!removed.isEmpty()) {
        const auto answer = QMessageBox::question(
            this, windowTitle(),
            tr("Remove %1 from the activity “%2”?\n\nTheir buttons and start steps in this activity, "
               "including input switching, will be deleted.")
                .arg(removed.join(", "), QString::fromStdString(existing_->label)),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    QDialog::accept();
}

} // namespace harmony::app
