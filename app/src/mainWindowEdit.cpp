/**
 * @file mainWindowEdit.cpp
 * @brief Главное окно: правка устройств и команд (переименование, удаление, добавление, обучение).
 */

#include "mainWindow.h"
#include "irDatabaseService.h"
#include "documentSource.h"
#include "remoteSession.h"
#include "labels.h"
#include "nameCheck.h"
#include "widgets/addDeviceDialog.h"
#include "widgets/commandDialog.h"
#include "widgets/newDeviceDialog.h"
#include "widgets/configView.h"
#include "widgets/learnWizardDialog.h"
#include "widgets/propertiesDialog.h"
#include "widgets/protocolCommandDialog.h"
#include "journal.h"

#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QMessageBox>
#include <QStatusBar>
#include <QPushButton>

#include <algorithm>
#include <map>
#include <optional>
#include <utility>

namespace harmony::app {

void MainWindow::renameItem(ConfigView::Item item, const QString& id) {
    if (!document_) {
        return;
    }
    const std::string key = id.toStdString();
    const bool isActivity = item == ConfigView::Item::Activity;
    QString current;
    if (isActivity) {
        if (const auto* activity = document_->model().findActivity(key)) {
            current = QString::fromStdString(activity->label);
        }
    } else if (const auto* device = document_->model().findDevice(key)) {
        // Без своего имени в списке видно «Производитель Модель» — его и предлагаем.
        current = QString::fromStdString(device->displayName());
    }

    const QString title = isActivity ? tr("Rename activity") : tr("Rename device");
    QString label = current;
    for (;;) {
        bool ok = false;
        label = QInputDialog::getText(this, title, tr("New name:"), QLineEdit::Normal, label, &ok).trimmed();
        if (!ok || label.isEmpty() || label == current) {
            return;
        }
        const bool taken = isActivity ? activityNameTaken(document_->model(), label, key)
                                      : deviceNameTaken(document_->model(), label, key);
        if (!taken) {
            break;
        }
        QMessageBox::warning(this, title,
                             isActivity ? tr("Another activity is already called “%1”. The remote shows only "
                                             "names, so choose a different one.")
                                              .arg(label)
                                        : tr("Another device is already called “%1”. The remote shows only "
                                             "names, so choose a different one.")
                                              .arg(label));
    }
    applyEdit(tr("Rename “%1”").arg(current), [&](ConfigDocument& document) {
        return isActivity ? document.setActivityLabel(key, label.toStdString())
                          : document.setDeviceLabel(key, label.toStdString());
    });
}

void MainWindow::removeItem(ConfigView::Item item, const QString& id) {
    if (!document_) {
        return;
    }
    const std::string key = id.toStdString();
    const bool isActivity = item == ConfigView::Item::Activity;
    QString name;
    if (isActivity) {
        if (const auto* activity = document_->model().findActivity(key)) {
            // «Выключить всё» удалить нельзя (пункт меню отключён); Delete не должен спрашивать зря.
            if (activity->isPowerOff()) {
                return;
            }
            name = QString::fromStdString(activity->label);
        }
    } else if (const auto* device = document_->model().findDevice(key)) {
        name = QString::fromStdString(device->displayName());
    }

    // Используемое устройство: сразу предлагаем убрать его из активностей или заменить другим.
    if (!isActivity && !document_->activitiesUsingDevice(key).empty()) {
        removeUsedDevice(key, name);
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Delete"),
        isActivity ? tr("Delete the activity “%1”?").arg(name)
                   : tr("Delete the device “%1” with its commands?").arg(name),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return;
    }
    if (applyEdit(tr("Delete “%1”").arg(name), [&](ConfigDocument& document) {
            return isActivity ? document.removeActivity(key) : document.removeDevice(key);
        })) {
        statusBar()->showMessage(tr("Deleted “%1”. Undo — %2").arg(name, labels::shortcut(QKeySequence::Undo)), 6000);
    }
}

void MainWindow::removeUsedDevice(const std::string& deviceId, const QString& name) {
    QStringList usedIn;
    QStringList emptied; // в них нет других устройств — удалятся целиком
    for (const auto& activity : document_->model().activities) {
        if (activity.isPowerOff() || !model::RemoteConfig::activityNeedsDevice(activity, deviceId)) {
            continue;
        }
        const QString label = "«" + QString::fromStdString(activity.label) + "»";
        usedIn << label;
        if (activity.powerOn.size() == 1 && activity.powerOn.front() == deviceId) {
            emptied << label;
        }
    }
    QString text = tr("The device “%1” is used in activities: %2.\n\nIt can be removed from them: its buttons, "
                      "input switching and start steps in these activities will be deleted.")
                       .arg(name, usedIn.join(", "));
    if (!emptied.isEmpty()) {
        text += "\n\n" + tr("These activities have no other devices and will be deleted: %1.").arg(emptied.join(", "));
    }
    QMessageBox box(QMessageBox::Question, tr("Delete device"), text, QMessageBox::Cancel, this);
    QPushButton* removeAll = box.addButton(tr("Remove from activities and delete"), QMessageBox::DestructiveRole);
    QPushButton* replace = box.addButton(tr("Replace…"), QMessageBox::ActionRole);
    replace->setToolTip(tr("Put another device in its place in activities — e.g. a new TV"));
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == replace) {
        replaceDevice(QString::fromStdString(deviceId));
        return;
    }
    if (box.clickedButton() != removeAll) {
        return;
    }
    if (applyEdit(tr("Delete “%1”").arg(name), [&](ConfigDocument& document) -> Result<void> {
            if (auto detached = document.removeDeviceFromActivities(deviceId); !detached) {
                return std::unexpected(std::move(detached.error()));
            }
            return document.removeDevice(deviceId);
        })) {
        statusBar()->showMessage(
            tr("Deleted “%1”, it is removed from %2. Undo — %3")
                .arg(name, labels::plural(usedIn.size(), "activity", "activities"),
                     labels::shortcut(QKeySequence::Undo)),
            8000);
    }
}

void MainWindow::addDevices() {
    if (!document_) {
        return;
    }
    AddDeviceDialog dialog(document_->model(), fromPath(document_->folder()), irDatabase_, remote_, this);
    const int result = dialog.exec();
    if (result == AddDeviceDialog::kNewDevice) {
        createDevice();
        return;
    }
    if (result == AddDeviceDialog::kOpenSettings) {
        showSettings(SettingsPage::IrDatabase);
        return;
    }
    if (result == QDialog::Accepted && dialog.fromDatabase()) {
        addFromDatabase(dialog.databaseEntry());
        return;
    }
    if (result != QDialog::Accepted || dialog.source() == nullptr) {
        return;
    }
    const auto ids = dialog.selectedDevices();
    if (ids.empty()) {
        return;
    }
    const ConfigDocument& source = *dialog.source();
    const model::Device* first = source.model().findDevice(ids.front());
    const QString text = ids.size() == 1
                             ? tr("Add “%1”").arg(QString::fromStdString(first->displayName()))
                             : tr("Add devices (%1)").arg(ids.size());

    // Все устройства — одним шагом истории: одна отмена убирает их вместе.
    std::string lastId;
    const bool added = applyEdit(text, [&](ConfigDocument& document) -> Result<void> {
        for (const auto& id : ids) {
            auto newId = document.importDevice(source, id);
            if (!newId) {
                return std::unexpected(std::move(newId.error()));
            }
            lastId = *newId;
        }
        return {};
    });
    if (added) {
        configView_->select(ConfigView::Item::Device, lastId);
        statusBar()->showMessage(
            ids.size() == 1
                ? tr("Added “%1”. In existing activities it is turned off.")
                      .arg(QString::fromStdString(first->displayName()))
                : tr("Added: %1. In existing activities they are turned off.")
                      .arg(labels::plural(static_cast<qsizetype>(ids.size()), "device", "devices")),
            8000);
    }
}

void MainWindow::createDevice() {
    if (!document_) {
        return;
    }
    NewDeviceDialog dialog(document_->model(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const DeviceDraft draft = dialog.draft();
    std::string id;
    const bool created = applyEdit(tr("Create “%1”").arg(QString::fromStdString(draft.label)),
                                   [&](ConfigDocument& document) -> Result<void> {
                                       auto newId = document.createDevice(draft);
                                       if (!newId) {
                                           return std::unexpected(std::move(newId.error()));
                                       }
                                       id = *newId;
                                       return {};
                                   });
    if (!created) {
        return;
    }
    configView_->select(ConfigView::Item::Device, id);
    const auto answer = QMessageBox::question(
        this, tr("New device"),
        tr("The device is created but has no commands yet.\n\nLearn commands from "
           "the original remote now?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes) {
        learnCommands(QString::fromStdString(id));
    }
}

void MainWindow::learnCommands(const QString& deviceId, const QStringList& commands) {
    if (!document_) {
        return;
    }
    const model::Device* device = document_->model().findDevice(deviceId.toStdString());
    if (device == nullptr) {
        return;
    }
    // Один мастер и для одной команды, и для многих: выделенные в таблице — отмечены сразу.
    LearnWizardDialog dialog(remote_, *device, this);
    if (!commands.isEmpty()) {
        dialog.markOnly(commands);
    }
    // Каждая выученная команда — отдельный шаг истории (отменяется по одной).
    connect(&dialog, &LearnWizardDialog::commandLearned, this,
            [this, deviceId](const QString& name, const ir::IrSignal& signal) {
                bool native = false;
                if (applyEdit(tr("Learn “%1”").arg(name), [&](ConfigDocument& document) -> Result<void> {
                        auto stored = document.storeLearned(deviceId.toStdString(), name.toStdString(), signal);
                        if (!stored) {
                            return std::unexpected(std::move(stored.error()));
                        }
                        native = *stored;
                        return {};
                    })) {
                    if (native) {
                        journal::info(tr("Command “%1” recognised as %2 and stored as protocol "
                                         "(the remote sends repeats itself)")
                                          .arg(name, labels::signalSummary(signal)));
                    }
                    configView_->select(ConfigView::Item::Device, deviceId.toStdString());
                }
            });
    dialog.exec();
}

void MainWindow::protocolCommand(const QString& deviceId, const QString& command) {
    if (!document_) {
        return;
    }
    const std::string id = deviceId.toStdString();
    const model::Device* device = document_->model().findDevice(id);
    if (device == nullptr) {
        return;
    }
    QStringList existing;
    std::optional<ir::IrDecoded> selected; // распознанный сигнал выбранной команды
    std::optional<ir::IrDecoded> initial;
    // Чаще всего у устройства один протокол и адрес — их и предлагаем для новой команды.
    std::map<std::pair<std::string, std::uint32_t>, int> seen;
    for (const auto& item : device->commands) {
        existing << QString::fromStdString(item.name);
        if (auto details = document_->commandDetails(id, item.name); details && details->decoded) {
            if (item.name == command.toStdString()) {
                selected = details->decoded;
            }
            ++seen[{details->decoded->protocol, details->decoded->address}];
        }
    }
    // Правка — у выбранной распознанной команды; иначе новая (протокол и адрес — самые частые).
    QString name;
    if (selected) {
        name = command;
        initial = selected;
    } else if (!seen.empty()) {
        const auto common = std::ranges::max_element(seen, {}, &decltype(seen)::value_type::second);
        initial = ir::IrDecoded{common->first.first, common->first.second, 0, {}};
    }
    ProtocolCommandDialog dialog(QString::fromStdString(device->displayName()), existing, name, initial, this, remote_);
    connect(&dialog, &ProtocolCommandDialog::signalReady, this,
            [this, id](const QString& learnedName, const ir::IrSignal& signal) {
        if (applyEdit(tr("Learn “%1”").arg(learnedName), [&](ConfigDocument& document) -> Result<void> {
            auto stored = document.storeLearned(id, learnedName.toStdString(), signal);
            if (!stored) return std::unexpected(stored.error());
            return {};
        })) configView_->selectCommand(id, learnedName.toStdString());
    });
    connect(&dialog, &ProtocolCommandDialog::commandReady, this,
            [this, id](const QString& commandName, const QString& protocol, quint32 address, quint32 code) {
                bool native = false;
                if (applyEdit(tr("Command “%1” entered manually").arg(commandName), [&](ConfigDocument& document) -> Result<void> {
                        auto stored = document.setProtocolCommand(id, commandName.toStdString(),
                                                                  protocol.toStdString(), address, code);
                        if (!stored) {
                            return std::unexpected(std::move(stored.error()));
                        }
                        native = *stored;
                        return {};
                    })) {
                    journal::info(native ? tr("Command “%1” stored as protocol %2 (the remote sends repeats itself)")
                                               .arg(commandName, protocol)
                                         : tr("Command “%1” stored as a signal").arg(commandName));
                    configView_->selectCommand(id, commandName.toStdString());
                }
            });
    dialog.exec();
}

void MainWindow::editProperties(ConfigView::Item item, const QString& id) {
    if (!document_) {
        return;
    }
    const std::string key = id.toStdString();
    const bool activity = item == ConfigView::Item::Activity;
    QString title;
    if (activity) {
        const model::Activity* found = document_->model().findActivity(key);
        if (found == nullptr) return;
        title = QString::fromStdString(found->label);
    } else {
        const model::Device* found = document_->model().findDevice(key);
        if (found == nullptr) return;
        title = QString::fromStdString(found->displayName());
    }
    PropertiesDialog dialog(*document_, activity, key, title, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto changes = dialog.changes();
    if (changes.empty()) {
        return;
    }
    applyEdit(tr("Properties of “%1”").arg(title), [&](ConfigDocument& document) -> Result<void> {
        for (const auto& change : changes) {
            if (auto set = document.setProperty(change.scope, key, change.name, change.value); !set) {
                return set;
            }
        }
        return {};
    });
}

void MainWindow::showCommandDetails(const QString& deviceId, const QString& command) {
    if (!document_) {
        return;
    }
    const model::Device* device = document_->model().findDevice(deviceId.toStdString());
    auto details = document_->commandDetails(deviceId.toStdString(), command.toStdString());
    if (device == nullptr || !details) {
        QMessageBox::warning(this, tr("Command"),
                             details ? tr("Device not found.") : labels::errorText(details.error()));
        return;
    }
    CommandDialog dialog(QString::fromStdString(device->displayName()), command, *details, this);
    dialog.exec();
}

void MainWindow::removeCommand(const QString& deviceId, const QString& command) {
    if (!document_) {
        return;
    }
    const std::string device = deviceId.toStdString();
    const std::string name = command.toStdString();
    const auto usage = document_->commandUsage(device, name);
    bool clearable = true; // кнопки и запуск активностей можно почистить вместе с удалением
    if (!usage.empty()) {
        QStringList places;
        for (const auto& use : usage) {
            const QString where = QString::fromStdString(use.name);
            QString text;
            switch (use.place) {
            case ConfigDocument::CommandUse::Place::DeviceButtons: text = tr("device buttons"); break;
            case ConfigDocument::CommandUse::Place::ActivityButtons: text = tr("buttons of activity “%1”").arg(where); break;
            case ConfigDocument::CommandUse::Place::DeviceStates:
                text = tr("power or inputs of the device");
                clearable = false;
                break;
            case ConfigDocument::CommandUse::Place::ActivityStart: text = tr("start of activity “%1”").arg(where); break;
            case ConfigDocument::CommandUse::Place::MacroButton:
                text = tr("a macro button");
                clearable = false;
                break;
            }
            places << QStringLiteral("• ") + text;
        }
        if (!clearable) {
            // Шаг из питания или макроса без замены сломал бы их — правятся в своих окнах.
            QMessageBox::information(this, tr("Delete command"),
                                     tr("The command “%1” is used in:\n%2\n\nPower, inputs and macro buttons "
                                        "are changed in their own windows: remove the command there first.")
                                         .arg(command, places.join('\n')));
            return;
        }
        QMessageBox box(QMessageBox::Question, tr("Delete command"),
                        tr("The command “%1” is used in:\n%2\n\nThese buttons and start steps will be deleted "
                           "together with the command.")
                            .arg(command, places.join('\n')),
                        QMessageBox::Cancel, this);
        QPushButton* removeAll = box.addButton(tr("Remove from there and delete"), QMessageBox::DestructiveRole);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != removeAll) {
            return;
        }
    }
    // Неиспользуемую команду удаляем без вопроса: шаг отменяется.
    if (applyEdit(tr("Delete command “%1”").arg(command), [&](ConfigDocument& document) -> Result<void> {
            if (!usage.empty()) {
                if (auto cleared = document.clearCommandUses(device, name); !cleared) {
                    return cleared;
                }
            }
            return document.removeCommand(device, name);
        })) {
        configView_->select(ConfigView::Item::Device, device);
        statusBar()->showMessage(
            tr("Deleted command “%1”. Undo — %2").arg(command, labels::shortcut(QKeySequence::Undo)), 6000);
    }
}

} // namespace harmony::app
