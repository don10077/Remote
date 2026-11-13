/**
 * @file mainWindowClipboard.cpp
 * @brief Копирование и вставка кнопок и ИК-команд.
 */
#include "mainWindow.h"
#include "labels.h"
#include "modern/remoteView.h"
#include "widgets/configView.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QStatusBar>

namespace harmony::app {

bool MainWindow::canCopy(bool cut) const {
    if (!document_) {
        return false;
    }
    const auto target = configView_->clipboardTarget();
    if (target.kind == ConfigView::ClipboardTarget::Kind::Buttons) {
        return target.button.has_value() &&
               (!cut || !RemoteView::hardKeyIds(QString::fromStdString(document_->targetRemote())).isEmpty());
    }
    // Команду вырезать нельзя: на неё ссылаются кнопки и питание — только копировать.
    return target.kind == ConfigView::ClipboardTarget::Kind::Commands && !target.commands.empty() && !cut;
}

bool MainWindow::canPaste() const {
    if (!document_) {
        return false;
    }
    const auto target = configView_->clipboardTarget();
    if (target.kind == ConfigView::ClipboardTarget::Kind::Buttons) {
        const auto keys = RemoteView::hardKeyIds(QString::fromStdString(document_->targetRemote()));
        const auto* button = std::get_if<ClipButton>(&clip_);
        if (keys.isEmpty() || button == nullptr) return false;
        const QString key = target.button && !target.button->soft
            ? target.button->key : QString::fromStdString(button->key);
        return button->soft || keys.contains(key);
    }
    return target.kind == ConfigView::ClipboardTarget::Kind::Commands && std::holds_alternative<ClipCommands>(clip_);
}

void MainWindow::copySelection(bool cut) {
    if (!canCopy(cut)) {
        statusBar()->showMessage(tr("Select a button on the “Buttons” tab or commands on the “Commands” tab"), 5000);
        return;
    }
    const auto target = configView_->clipboardTarget();
    const model::RemoteConfig& config = document_->model();
    if (target.kind == ConfigView::ClipboardTarget::Kind::Buttons) {
        // Кнопка — из модели владельца.
        const auto& groups = target.owner.isDevice() ? config.findDevice(target.owner.id)->controlGroups
                                                     : config.findActivity(target.owner.id)->controlGroups;
        const ButtonRef& ref = *target.button;
        for (const auto& group : groups) {
            for (const auto& button : group.buttons) {
                const bool hard = group.name == "HardButtons";
                if (hard == ref.soft || (hard ? button.name != ref.key.toStdString() : button.position != ref.position)) {
                    continue;
                }
                const auto [deviceId, command] = splitActionId(button.actionId);
                clip_ = ClipButton{ref.soft, button.name, button.label, deviceId, command};
                const QString text = labels::button(button) + QStringLiteral(" → ") + labels::action(config, button.actionId);
                QGuiApplication::clipboard()->setText(text);
                if (cut) {
                    removeButton(target.owner, ref);
                } else {
                    statusBar()->showMessage(tr("Copied: %1. Paste — %2 on the buttons of another activity")
                                                 .arg(text, labels::shortcut(QKeySequence::Paste)),
                                             6000);
                }
                return;
            }
        }
        return;
    }
    // Команды: имена и сигналы; в системный буфер — Pronto Hex (для других программ).
    ClipCommands commands;
    commands.documentSerial = documentSerial_;
    commands.deviceId = target.deviceId;
    QStringList lines;
    for (const auto& name : target.commands) {
        if (auto signal = document_->commandSignal(target.deviceId, name)) {
            lines << QString::fromStdString(name) + '\t' + QString::fromStdString(ir::prontoHex(*signal));
            commands.commands.emplace_back(name, std::move(*signal));
        }
    }
    if (commands.commands.empty()) {
        statusBar()->showMessage(tr("The signals of these commands are not readable"), 5000);
        return;
    }
    QGuiApplication::clipboard()->setText(lines.join('\n'));
    statusBar()->showMessage(
        tr("Copied %1. Paste — %2 on the commands of another device (also in another configuration)")
            .arg(labels::plural(static_cast<qsizetype>(commands.commands.size()), "command", "commands"),
                 labels::shortcut(QKeySequence::Paste)),
        6000);
    clip_ = std::move(commands);
}

void MainWindow::pasteClipboard() {
    if (!canPaste()) {
        statusBar()->showMessage(std::holds_alternative<std::monostate>(clip_)
                                     ? tr("Nothing copied yet")
                                     : (std::holds_alternative<ClipButton>(clip_)
                                            ? tr("A button is pasted on the “Buttons” tab")
                                            : tr("Commands are pasted on a device")),
                                 5000);
        return;
    }
    const auto target = configView_->clipboardTarget();
    if (const auto* button = std::get_if<ClipButton>(&clip_)) {
        // Аппаратная — на выбранную аппаратную (или на ту же клавишу); экранная — на
        // выбранное место экрана (замена) или на первое свободное.
        const ButtonOwner owner = target.owner;
        const ClipButton clip = *button;
        const std::string key = target.button && !target.button->soft ? target.button->key.toStdString() : clip.key;
        const int position = target.button && target.button->soft ? target.button->position
                                                                   : document_->freeSoftPosition(owner);
        const ButtonRef placed = clip.soft ? ButtonRef::screen(position) : ButtonRef::hard(QString::fromStdString(key));
        if (applyEdit(tr("Paste “%1”").arg(QString::fromStdString(clip.soft ? clip.label : key)),
                      [&](ConfigDocument& document) {
                          return clip.soft ? document.setSoftButton(owner, position, clip.label, clip.deviceId,
                                                                    clip.command)
                                           : document.setHardButton(owner, key, clip.deviceId, clip.command);
                      })) {
            configView_->selectButton(placed);
            statusBar()->showMessage(tr("Pasted. Undo — %1").arg(labels::shortcut(QKeySequence::Undo)), 5000);
        }
        return;
    }
    const auto& clip = std::get<ClipCommands>(clip_);
    const std::string to = target.deviceId;
    const bool sameDocument = clip.documentSerial == documentSerial_ &&
                              document_->model().findDevice(clip.deviceId) != nullptr;
    if (sameDocument && clip.deviceId == to) {
        statusBar()->showMessage(tr("These commands are already here — choose another device"), 5000);
        return;
    }
    std::vector<std::string> names;
    for (const auto& [name, signal] : clip.commands) {
        names.push_back(name);
    }
    const QString commands = labels::plural(static_cast<qsizetype>(names.size()), "command", "commands");
    const model::Device* device = document_->model().findDevice(to);
    const QString deviceName = device != nullptr ? QString::fromStdString(device->displayName()) : QString();
    // В той же конфигурации — записи как есть; из другой — сигналами, как выученные.
    if (applyEdit(tr("Paste %1 into “%2”").arg(commands, deviceName), [&](ConfigDocument& document) -> Result<void> {
            if (sameDocument) {
                auto copied = document.copyCommands(clip.deviceId, to, names);
                return copied ? Result<void>{} : std::unexpected(std::move(copied.error()));
            }
            for (const auto& [name, signal] : clip.commands) {
                if (auto learned = document.learnCommand(to, name, signal); !learned) {
                    return learned;
                }
            }
            return {};
        })) {
        configView_->select(ConfigView::Item::Device, to);
        statusBar()->showMessage(
            tr("Pasted %1 into “%2”. Undo — %3").arg(commands, deviceName, labels::shortcut(QKeySequence::Undo)),
            6000);
    }
}

} // namespace harmony::app
