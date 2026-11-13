/**
 * @file replaceDeviceDialog.cpp
 * @brief Окно замены устройства в активностях.
 */

#include "replaceDeviceDialog.h"

#include "../icons.h"
#include "../labels.h"
#include "../modern/activityIcon.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace harmony::app {

namespace {

constexpr int kIdRole = Qt::UserRole + 1;

} // namespace

ReplaceDeviceDialog::ReplaceDeviceDialog(const ConfigDocument& document, const std::string& deviceId, QWidget* parent)
    : QDialog(parent), document_(document), oldId_(deviceId) {
    const model::RemoteConfig& config = document.model();
    const model::Device* old = config.findDevice(deviceId);
    const QString oldName = old != nullptr ? QString::fromStdString(old->displayName()) : QString();
    setWindowTitle(tr("Replace device"));
    setMinimumSize(640, 640);

    auto* title = new QLabel(tr("Replace “%1” in activities").arg(oldName), this);
    title->setObjectName("pageTitle");
    title->setWordWrap(true);
    auto* hint = new QLabel(tr("For example, a new TV: in the chosen activities it takes over switching on and off, "
                               "roles, inputs, start steps and buttons of “%1”. Commands are matched by name; "
                               "buttons whose command the new device does not have are removed.")
                                .arg(oldName),
                            this);
    hint->setObjectName("pageMeta");
    hint->setWordWrap(true);

    target_ = new QComboBox(this);
    for (const auto& device : config.devices) {
        if (device.id != deviceId) {
            target_->addItem(icons::deviceType(device.type), QString::fromStdString(device.displayName()),
                             QString::fromStdString(device.id));
        }
    }

    activities_ = new QListWidget(this);
    activities_->setMaximumHeight(140);
    for (const auto& activity : config.activities) {
        if (activity.isPowerOff()) {
            continue;
        }
        const auto used = document.usedDeviceCommands(deviceId, {activity.id});
        if (!activity.usesDevice(deviceId) && used.empty()) {
            continue;
        }
        auto* item = new QListWidgetItem(activityIcon(QString::fromStdString(activity.type)),
                                         QString::fromStdString(activity.label), activities_);
        item->setData(kIdRole, QString::fromStdString(activity.id));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }

    commands_ = new QTableWidget(0, 2, this);
    commands_->setHorizontalHeaderLabels({tr("Command of “%1”").arg(oldName), tr("Becomes")});
    commands_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    commands_->verticalHeader()->hide();
    commands_->setSelectionMode(QAbstractItemView::NoSelection);

    removeOld_ = new QCheckBox(tr("Delete “%1” if no activity needs it any more").arg(oldName), this);
    summary_ = new QLabel(this);
    summary_->setObjectName("pageMeta");
    summary_->setWordWrap(true);

    auto* form = new QFormLayout;
    form->addRow(tr("New device:"), target_);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    replace_ = new QPushButton(tr("Replace"), this);
    replace_->setObjectName("primaryButton");
    replace_->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(replace_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addLayout(form);
    layout->addWidget(new QLabel(tr("Activities:"), this));
    layout->addWidget(activities_);
    layout->addWidget(new QLabel(tr("Commands:"), this));
    layout->addWidget(commands_, 1);
    layout->addWidget(removeOld_);
    layout->addWidget(summary_);
    layout->addLayout(buttons);

    connect(target_, &QComboBox::currentIndexChanged, this, &ReplaceDeviceDialog::fillCommands);
    connect(activities_, &QListWidget::itemChanged, this, &ReplaceDeviceDialog::fillCommands);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(replace_, &QPushButton::clicked, this, &QDialog::accept);
    fillCommands();
    resize(720, 720);
}

std::vector<std::string> ReplaceDeviceDialog::chosenActivities() const {
    std::vector<std::string> ids;
    for (int row = 0; row < activities_->count(); ++row) {
        if (activities_->item(row)->checkState() == Qt::Checked) {
            ids.push_back(activities_->item(row)->data(kIdRole).toString().toStdString());
        }
    }
    return ids;
}

void ReplaceDeviceDialog::fillCommands() {
    commands_->setRowCount(0);
    const std::string target = target_->currentData().toString().toStdString();
    const model::Device* device = document_.model().findDevice(target);
    const auto chosen = chosenActivities();
    if (device != nullptr && !chosen.empty()) {
        for (const auto& [command, uses] : document_.usedDeviceCommands(oldId_, chosen)) {
            const int row = commands_->rowCount();
            commands_->insertRow(row);
            auto* name = new QTableWidgetItem(uses > 1 ? tr("%1  (×%2)").arg(QString::fromStdString(command)).arg(uses)
                                                       : QString::fromStdString(command));
            name->setData(kIdRole, QString::fromStdString(command));
            commands_->setItem(row, 0, name);
            auto* choice = new QComboBox(commands_);
            choice->addItem(tr("— remove"), QString());
            for (const auto& own : device->commands) {
                choice->addItem(QString::fromStdString(own.name), QString::fromStdString(own.name));
            }
            const std::string match = document_.matchDeviceCommand(target, command);
            choice->setCurrentIndex(std::max(0, choice->findData(QString::fromStdString(match))));
            connect(choice, &QComboBox::currentIndexChanged, this, &ReplaceDeviceDialog::updateSummary);
            commands_->setCellWidget(row, 1, choice);
        }
    }
    updateSummary();
}

void ReplaceDeviceDialog::updateSummary() {
    int kept = 0;
    int removed = 0;
    for (int row = 0; row < commands_->rowCount(); ++row) {
        const auto* choice = static_cast<QComboBox*>(commands_->cellWidget(row, 1));
        (choice->currentData().toString().isEmpty() ? removed : kept) += 1;
    }
    const bool ready = target_->count() > 0 && !chosenActivities().empty();
    replace_->setEnabled(ready);
    if (target_->count() == 0) {
        summary_->setText(tr("There is no other device to put in its place — add the new device first."));
    } else if (chosenActivities().empty()) {
        summary_->setText(tr("Choose at least one activity."));
    } else if (removed > 0) {
        summary_->setText(tr("Commands: %1 carried over, %2 without a match — their buttons and steps are removed.")
                              .arg(kept)
                              .arg(removed));
    } else {
        summary_->setText(tr("All commands have a match."));
    }
}

DeviceReplacement ReplaceDeviceDialog::replacement() const {
    DeviceReplacement result;
    result.oldId = oldId_;
    result.newId = target_->currentData().toString().toStdString();
    result.activities = chosenActivities();
    for (int row = 0; row < commands_->rowCount(); ++row) {
        const auto* choice = static_cast<QComboBox*>(commands_->cellWidget(row, 1));
        result.commands[commands_->item(row, 0)->data(kIdRole).toString().toStdString()] =
            choice->currentData().toString().toStdString();
    }
    result.removeOld = removeOld_->isChecked();
    return result;
}

} // namespace harmony::app
