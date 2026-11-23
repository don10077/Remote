/**
 * @file mainWindowButtons.cpp
 * @brief Главное окно: правка активностей и кнопок, состояния и замена устройств, копирование кнопок.
 */

#include "mainWindow.h"
#include "assignmentGate.h"
#include "labels.h"
#include "modelKeysQt.h"
#include "modern/remoteView.h"
#include "widgets/activityDialog.h"
#include "widgets/buttonDialog.h"
#include "widgets/configView.h"
#include "widgets/deviceStatesDialog.h"
#include "widgets/compareDevicesDialog.h"
#include "widgets/copyButtonsDialog.h"
#include "widgets/replaceDeviceDialog.h"

#include <QKeySequence>
#include <QMessageBox>
#include <QStatusBar>
#include <QPushButton>

#include <optional>
#include <utility>

namespace harmony::app {

void MainWindow::addActivity() {
    if (!document_) {
        return;
    }
    if (document_->model().devices.empty()) {
        const auto answer = QMessageBox::question(
            this, tr("Add activity"),
            tr("An activity turns devices on, but the configuration has none "
               "yet.\n\nAdd a device?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
        if (answer == QMessageBox::Yes) {
            addDevices();
        }
        return;
    }
    ActivityDialog dialog(document_->model(), document_->targetRemote(), nullptr, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const ActivityDraft draft = dialog.draft();
    std::string id;
    const bool added = applyEdit(tr("Add “%1”").arg(QString::fromStdString(draft.label)),
                                 [&](ConfigDocument& document) -> Result<void> {
                                     auto newId = document.addActivity(draft);
                                     if (!newId) {
                                         return std::unexpected(std::move(newId.error()));
                                     }
                                     id = *newId;
                                     return {};
                                 });
    if (added) {
        configView_->select(ConfigView::Item::Activity, id);
    }
}

namespace {

/// Название кнопки для истории и сообщений: «Меню» или «экранная кнопка 3».
QString buttonTitle(const ButtonRef& button) {
    return button.soft ? QObject::tr("screen button %1").arg(button.position + 1)
                       : RemoteView::keyName(button.key);
}

/// Назначенная кнопка владельца: аппаратная по имени, экранная по месту; nullptr — свободна.
const model::Button* findButton(const model::RemoteConfig& config, const ButtonOwner& owner, const ButtonRef& ref) {
    const model::Device* device = owner.isDevice() ? config.findDevice(owner.id) : nullptr;
    const model::Activity* activity = owner.isDevice() ? nullptr : config.findActivity(owner.id);
    if (device == nullptr && activity == nullptr) {
        return nullptr;
    }
    const auto& groups = device != nullptr ? device->controlGroups : activity->controlGroups;
    for (const auto& group : groups) {
        for (const auto& button : group.buttons) {
            const bool match = ref.soft ? button.name.empty() && button.position == ref.position
                                        : !ref.key.isEmpty() && button.name == ref.key.toStdString();
            if (match) {
                return &button;
            }
        }
    }
    return nullptr;
}

} // namespace

bool MainWindow::assignmentAllowed(const ButtonRef& button, ButtonAction action,
                                   bool emptyNameAllowed) const {
    if (!gateOpen(document_ != nullptr)) {
        return false;
    }
    // Список строится на каждый вызов: в A1 кеша нет, а вызовы идут из гейтов и обновлений
    // таблиц, не из отрисовки.
    const auto keys = modelKeysFor(QString::fromStdString(document_->targetRemote()), &document_->model());
    return buttonAllowed(button, keys, action, emptyNameAllowed);
}

void MainWindow::editButton(const ButtonOwner& owner, const ButtonRef& button) {
    // A1: гейт — есть документ. Пустое имя разрешено: клавишу ещё выбирается в окне.
    // Существующее назначение клавиши вне списка модели правке не препятствует — иначе
    // предупреждение C1 нельзя исправить.
    if (!assignmentAllowed(button, ButtonAction::TakeFrom, true)) {
        return;
    }
    // Макрос кнопки (если она его выполняет) — для правки шагов.
    Macro initialMacro;
    {
        const model::Device* ownerDevice = owner.isDevice() ? document_->model().findDevice(owner.id) : nullptr;
        const model::Activity* ownerActivity = owner.isDevice() ? nullptr : document_->model().findActivity(owner.id);
        static const std::vector<model::ControlGroup> kNone;
        const auto& groups = ownerDevice != nullptr     ? ownerDevice->controlGroups
                             : ownerActivity != nullptr ? ownerActivity->controlGroups
                                                        : kNone;
        for (const auto& group : groups) {
            for (const auto& item : group.buttons) {
                const bool match = button.soft ? item.name.empty() && item.position == button.position
                                               : !button.key.isEmpty() && item.name == button.key.toStdString();
                if (match && isMacroAction(item.actionId)) {
                    if (auto steps = document_->macro(item.actionId)) {
                        initialMacro = std::move(*steps);
                    }
                }
            }
        }
    }
    ButtonDialog dialog(document_->model(), document_->targetRemote(), owner, button, this, initialMacro);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (dialog.removeChosen()) {
        removeButton(owner, button);
        return;
    }

    ButtonRef target = dialog.button();
    if (target.soft && target.position < 0) {
        target.position = document_->freeSoftPosition(owner);
    }
    const std::string deviceId = dialog.deviceId();
    const std::string command = dialog.command();
    const std::string label = dialog.label();
    const std::string icon = dialog.icon();
    const bool macro = dialog.macroChosen();
    const Macro steps = dialog.macro();
    const bool applied = applyEdit(
        tr("Assign “%1”").arg(buttonTitle(target)), [&](ConfigDocument& document) -> Result<void> {
            if (target.soft) {
                // У кнопки со значком подпись не видна; макросу нужна хоть какая-то.
                const std::string text = macro && label.empty() ? std::string("Macro") : label;
                auto set = macro ? document.setSoftMacro(owner, target.position, text, steps)
                                 : document.setSoftButton(owner, target.position, text, deviceId, command);
                if (!set) {
                    return set;
                }
                return document.setSoftButtonIcon(owner, target.position, icon);
            }
            // Аппаратную кнопку в окне сменили на другую — прежняя освобождается.
            if (!button.key.isEmpty() && button.key != target.key) {
                if (auto cleared = document.setHardButton(owner, button.key.toStdString(), {}, {}); !cleared) {
                    return cleared;
                }
            }
            return macro ? document.setHardMacro(owner, target.key.toStdString(), steps)
                         : document.setHardButton(owner, target.key.toStdString(), deviceId, command);
        });
    if (applied) {
        configView_->selectButton(target);
    }
}

void MainWindow::removeButton(const ButtonOwner& owner, const ButtonRef& button) {
    // Снять назначение можно и с клавиши вне списка модели (правило A1.3).
    if (!assignmentAllowed(button, ButtonAction::TakeFrom, false)) {
        return;
    }
    const QString title = buttonTitle(button);
    const bool applied = applyEdit(
        button.soft ? tr("Delete “%1”").arg(title) : tr("Clear assignment of “%1”").arg(title),
        [&](ConfigDocument& document) {
            return button.soft ? document.removeSoftButton(owner, button.position)
                               : document.setHardButton(owner, button.key.toStdString(), {}, {});
        });
    if (applied) {
        const QString undoKey = labels::shortcut(QKeySequence::Undo);
        statusBar()->showMessage(button.soft ? tr("Deleted “%1”. Undo — %2").arg(title, undoKey)
                                             : tr("Assignment of “%1” cleared. Undo — %2").arg(title, undoKey),
                                 6000);
    }
}

void MainWindow::moveButton(const ButtonOwner& owner, const ButtonRef& from, const ButtonRef& to, bool copy) {
    if (!document_ || from.soft != to.soft) {
        return;
    }
    // Перенести можно с любой клавиши, но только на клавишу модели, когда список исчерпывающий.
    if (!assignmentAllowed(from, ButtonAction::TakeFrom, false) ||
        !assignmentAllowed(to, ButtonAction::AssignTo, false)) {
        return;
    }
    // Перетащили свободную кнопку — переносить нечего, ошибка в окне тут ни к чему.
    if (findButton(document_->model(), owner, from) == nullptr) {
        statusBar()->showMessage(tr("“%1” has no assignment: nothing to move.").arg(buttonTitle(from)), 6000);
        return;
    }
    // На занятое место кнопки меняются местами (копия заменяет прежнее назначение).
    const bool swap = !copy && findButton(document_->model(), owner, to) != nullptr;
    const QString text = (copy   ? tr("Copy “%1” → “%2”")
                          : swap ? tr("Swap “%1” and “%2”")
                                 : tr("Move “%1” → “%2”"))
                             .arg(buttonTitle(from), buttonTitle(to));
    const bool applied = applyEdit(text, [&](ConfigDocument& document) {
        return from.soft ? document.moveSoftButton(owner, from.position, to.position)
                         : document.moveHardButton(owner, from.key.toStdString(), to.key.toStdString(), copy);
    });
    if (applied) {
        configView_->selectButton(to);
        statusBar()->showMessage(tr("%1. Undo — %2").arg(text, labels::shortcut(QKeySequence::Undo)), 6000);
    }
}

void MainWindow::assignCommand(const ButtonOwner& owner, const ButtonRef& button, const QString& deviceId,
                               const QString& command) {
    // Место экранной кнопки проверяется здесь, как прежде: -1 — новая кнопка.
    if (!assignmentAllowed(button, ButtonAction::AssignTo, false) || (button.soft && button.position < 0)) {
        return;
    }
    // Подпись экранной кнопки: своя (или значок) остаётся, а сделанная из имени
    // прежней команды сменилась бы на неверную — её заменяем именем новой.
    std::string label;
    if (const model::Button* existing = findButton(document_->model(), owner, button);
        button.soft && existing != nullptr && existing->icon.empty() &&
        existing->label == splitActionId(existing->actionId).second) {
        label = command.toStdString();
    }
    const QString text = tr("Assign “%1” to “%2”").arg(command, buttonTitle(button));
    const bool applied = applyEdit(text, [&](ConfigDocument& document) {
        return button.soft
                   ? document.setSoftButton(owner, button.position, label, deviceId.toStdString(), command.toStdString())
                   : document.setHardButton(owner, button.key.toStdString(), deviceId.toStdString(),
                                            command.toStdString());
    });
    if (applied) {
        statusBar()->showMessage(tr("%1. Undo — %2").arg(text, labels::shortcut(QKeySequence::Undo)), 6000);
    }
}

void MainWindow::editDeviceStates(const QString& deviceId, bool inputs) {
    if (!document_) {
        return;
    }
    const std::string id = deviceId.toStdString();
    const model::Device* device = document_->model().findDevice(id);
    auto states = document_->deviceStates(id);
    if (device == nullptr || !states) {
        QMessageBox::warning(this, inputs ? tr("Inputs") : tr("Power"),
                             states ? tr("The device is not found.") : labels::errorText(states.error()));
        return;
    }
    DeviceStatesDialog dialog(*device, *states,
                              inputs ? DeviceStatesDialog::Section::Inputs : DeviceStatesDialog::Section::Power,
                              this);
    if (dialog.exec() != QDialog::Accepted || dialog.states() == *states) {
        return;
    }
    const DeviceStates changed = dialog.states();
    const QString name = QString::fromStdString(device->displayName());
    if (applyEdit(inputs ? tr("Inputs of “%1”").arg(name) : tr("Power of “%1”").arg(name),
                  [&](ConfigDocument& document) { return document.setDeviceStates(id, changed); })) {
        configView_->select(ConfigView::Item::Device, id);
    }
}

void MainWindow::replaceDevice(const QString& deviceId) {
    if (!document_) {
        return;
    }
    const std::string id = deviceId.toStdString();
    const model::Device* device = document_->model().findDevice(id);
    if (device == nullptr) {
        return;
    }
    const QString oldName = QString::fromStdString(device->displayName());
    ReplaceDeviceDialog dialog(*document_, id, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const DeviceReplacement replacement = dialog.replacement();
    const model::Device* target = document_->model().findDevice(replacement.newId);
    const QString newName = target != nullptr ? QString::fromStdString(target->displayName()) : QString();
    DeviceReplacementReport report;
    const bool applied = applyEdit(tr("Replace “%1” with “%2”").arg(oldName, newName),
                                   [&](ConfigDocument& document) -> Result<void> {
                                       auto done = document.replaceDevice(replacement);
                                       if (!done) {
                                           return std::unexpected(std::move(done.error()));
                                       }
                                       report = *done;
                                       return {};
                                   });
    if (!applied) {
        return;
    }
    configView_->select(ConfigView::Item::Device, replacement.newId);
    // Итог: что перешло, что убрано.
    QStringList lines;
    lines << tr("Activities changed: %1.").arg(report.activities);
    lines << tr("Buttons moved: %1, start steps: %2.").arg(report.buttons).arg(report.steps);
    if (report.macroSteps > 0) {
        lines << tr("Macro steps moved: %1.").arg(report.macroSteps);
    }
    if (report.removed > 0) {
        lines << tr("Removed without a matching command: %1.").arg(report.removed);
    }
    if (report.inputsLost > 0) {
        lines << tr("Input switches removed (“%1” has no such input): %2.").arg(newName).arg(report.inputsLost);
    }
    if (report.oldRemoved) {
        lines << tr("“%1” is deleted.").arg(oldName);
    }
    lines << tr("Undo — %1.").arg(labels::shortcut(QKeySequence::Undo));
    QMessageBox::information(this, tr("Replace device"), lines.join('\n'));
}

void MainWindow::compareDevices(const QString& deviceId) {
    if (!document_ || document_->model().findDevice(deviceId.toStdString()) == nullptr) {
        return;
    }
    CompareDevicesDialog dialog(*document_, deviceId.toStdString(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const std::string from = dialog.fromId();
    const std::string to = dialog.toId();
    const std::vector<std::string> names = dialog.marked();
    const model::Device* target = document_->model().findDevice(to);
    const QString targetName = target != nullptr ? QString::fromStdString(target->displayName()) : QString();
    const QString commands = labels::plural(static_cast<qsizetype>(names.size()), "command", "commands");
    if (applyEdit(tr("Copy %1 to “%2”").arg(commands, targetName),
                  [&](ConfigDocument& document) -> Result<void> {
                      auto copied = document.copyCommands(from, to, names);
                      if (!copied) {
                          return std::unexpected(std::move(copied.error()));
                      }
                      return {};
                  })) {
        configView_->select(ConfigView::Item::Device, to);
        statusBar()->showMessage(
            tr("Copied %1 to “%2”. Undo — %3").arg(commands, targetName, labels::shortcut(QKeySequence::Undo)), 6000);
    }
}

void MainWindow::setNotSent(ConfigView::Item item, const QString& id, bool notSent) {
    if (!document_) {
        return;
    }
    const std::string key = id.toStdString();
    const bool device = item == ConfigView::Item::Device;
    const model::Device* found = device ? document_->model().findDevice(key) : nullptr;
    const model::Activity* activity = device ? nullptr : document_->model().findActivity(key);
    if (found == nullptr && activity == nullptr) {
        return;
    }
    const QString name = QString::fromStdString(device ? found->displayName() : activity->label);
    if (device && notSent) {
        // Активности, которым устройство нужно, тоже перестанут уходить на пульт — показываем это до правки.
        const auto& config = document_->model();
        QStringList affected;
        for (const auto& other : config.activities) {
            if (!other.isPowerOff() && !config.isActivityNotSent(other) &&
                model::RemoteConfig::activityNeedsDevice(other, key)) {
                affected << "«" + QString::fromStdString(other.label) + "»";
            }
        }
        if (!affected.isEmpty()) {
            QMessageBox box(QMessageBox::Question, tr("Make the device inactive"),
                            tr("The device “%1” is used in activities: %2.\n\nThey will not be sent to the "
                               "remote either. When the device is made active again, they are sent again.")
                                .arg(name, affected.join(", ")),
                            QMessageBox::Cancel, this);
            QPushButton* confirm =
                box.addButton(tr("Make the device and these activities inactive"), QMessageBox::AcceptRole);
            box.setDefaultButton(QMessageBox::Cancel);
            box.exec();
            if (box.clickedButton() != confirm) {
                configView_->refresh(); // переключатель — обратно
                return;
            }
        }
    }
    const QString text = notSent ? tr("Do not send “%1” to the remote").arg(name)
                                 : tr("Send “%1” to the remote").arg(name);
    if (!applyEdit(text, [&](ConfigDocument& document) {
            return device ? document.setDeviceNotSent(key, notSent) : document.setActivityNotSent(key, notSent);
        })) {
        configView_->refresh(); // флажок в меню — обратно
        return;
    }
    // Открытая страница остаётся: устройство включают и с полосы на странице активности.
    // Сколько активностей теперь не уходит на пульт.
    const auto held = document_->activitiesNotSent();
    const QString undoKey = labels::shortcut(QKeySequence::Undo);
    statusBar()->showMessage(
        held.empty() ? tr("%1. Undo — %2").arg(text, undoKey)
                     : tr("%1. Not sent to the remote: %2. Undo — %3")
                           .arg(text,
                                labels::plural(static_cast<qsizetype>(held.size()), "activity", "activities"),
                                undoKey),
        8000);
}

void MainWindow::copyButtons(const QString& activityId, bool hasButton, const ButtonRef& button) {
    if (!gateOpen(document_ != nullptr) ||
        document_->model().findActivity(activityId.toStdString()) == nullptr) {
        return;
    }
    CopyButtonsDialog dialog(document_->model(), activityId.toStdString(),
                             hasButton ? std::optional(button) : std::nullopt, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const ButtonCopy copy = dialog.copy();
    ButtonCopyReport report;
    const bool applied = applyEdit(tr("Copy buttons to %1").arg(labels::plural(
                                       static_cast<qsizetype>(copy.toActivities.size()), "activity", "activities")),
                                   [&](ConfigDocument& document) -> Result<void> {
                                       auto done = document.copyButtons(copy);
                                       if (!done) {
                                           return std::unexpected(std::move(done.error()));
                                       }
                                       report = *done;
                                       return {};
                                   });
    if (!applied) {
        return;
    }
    QStringList parts;
    parts << tr("Remote buttons: %1, screen buttons: %2.").arg(report.hard).arg(report.soft);
    if (report.kept > 0) {
        parts << tr("Already taken, kept: %1.").arg(report.kept);
    }
    if (report.skipped > 0) {
        parts << tr("Skipped (the device is not turned on there): %1.").arg(report.skipped);
    }
    // Пропуски стоит показать окном — иначе достаточно строки состояния.
    const QString undoKey = labels::shortcut(QKeySequence::Undo);
    if (report.skipped > 0 || report.hard + report.soft == 0) {
        QMessageBox::information(this, tr("Copy buttons"),
                                 (parts + QStringList{tr("Undo — %1.").arg(undoKey)}).join('\n'));
    } else {
        statusBar()->showMessage(tr("%1 Undo — %2").arg(parts.join(' '), undoKey), 8000);
    }
}

void MainWindow::editActivity(const QString& activityId) {
    if (!document_) {
        return;
    }
    const std::string id = activityId.toStdString();
    const model::Activity* activity = document_->model().findActivity(id);
    if (activity == nullptr) {
        return;
    }
    ActivityDialog dialog(document_->model(), document_->targetRemote(), activity, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const ActivityDraft draft = dialog.draft();
    const bool reassign = dialog.reassignButtons();
    applyEdit(tr("Edit “%1”").arg(QString::fromStdString(draft.label)),
              [&](ConfigDocument& document) { return document.updateActivity(id, draft, reassign); });
}

void MainWindow::moveActivity(const QString& id, int index) {
    if (!document_ || index < 0) {
        return;
    }
    const std::string key = id.toStdString();
    const auto* activity = document_->model().findActivity(key);
    const QString name = activity != nullptr ? QString::fromStdString(activity->label) : id;
    applyEdit(tr("Move “%1”").arg(name), [&](ConfigDocument& document) {
        return document.moveActivity(key, static_cast<std::size_t>(index));
    });
}

} // namespace harmony::app
