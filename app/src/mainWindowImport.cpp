/**
 * @file mainWindowImport.cpp
 * @brief Главное окно: импорт из базы ИК-кодов и сопоставление устройств Harmony Hub.
 */

#include "mainWindow.h"
#include "irDatabaseService.h"
#include "labels.h"
#include "widgets/configView.h"
#include "journal.h"

#include <QInputDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QThread>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace harmony::app {

void MainWindow::resolveHubCodes() {
    // У команд хаба есть только KeyCode: IR-коды берём из базы (одна правка, отменяется).
    const irdb::IrDatabase* database = irDatabase_->database();
    if (!document_ || database == nullptr) {
        statusBar()->showMessage(
            tr("Hub configuration opened without IR codes: the IR database is not available."), 10000);
        return;
    }
    HubCodesReport report;
    const bool applied = applyEdit(tr("Resolve hub IR codes"), [&](ConfigDocument& document) -> Result<void> {
        auto resolved = document.resolveHubCodes(database->protocols());
        if (!resolved) {
            return std::unexpected(std::move(resolved.error()));
        }
        report = std::move(*resolved);
        return {};
    });
    if (!applied) {
        return;
    }
    for (const std::string& skipped : report.skipped) {
        journal::warning(tr("IR code not resolved: %1").arg(QString::fromStdString(skipped)));
    }
    statusBar()->showMessage(tr("Hub IR codes: %1 as protocol, %2 as signal, %3 skipped")
                                 .arg(report.asProtocol)
                                 .arg(report.asSignal)
                                 .arg(report.skipped.size()),
                             10000);
    matchStockHubDevices(*database);
}

void MainWindow::matchStockHubDevices(const irdb::IrDatabase& database) {
    // Стоковый хаб отдаёт только имена команд: коды берём у такой же модели из базы.
    struct Pending {
        std::string id;
        QString name;
        std::string query;
        std::string modelOnly;
        std::vector<std::string> commandNames; ///< Имена команд для ранжирования кандидатов
    };
    std::vector<Pending> pending;
    for (const model::Device& device : document_->model().devices) {
        const bool needsCodes = std::ranges::any_of(device.commands, [](const model::Command& command) {
            return command.code.empty() && command.native.empty() &&
                   (command.transport == model::Transport::Ir || command.transport == model::Transport::Unknown);
        });
        if (needsCodes) {
            std::vector<std::string> names;
            for (const model::Command& command : device.commands) {
                names.push_back(command.name);
            }
            pending.push_back({device.id, QString::fromStdString(device.displayName()),
                               device.manufacturer + " " + device.model, device.model, std::move(names)});
        }
    }
    int matched = 0;
    int unmatched = 0;
    for (const Pending& item : pending) {
        std::vector<std::size_t> found = database.search(item.query, 30);
        if (found.empty() && !item.modelOnly.empty()) {
            found = database.search(item.modelOnly, 30);
        }
        if (found.empty()) {
            ++unmatched;
            journal::warning(tr("No device in the IR database for “%1”").arg(item.name));
            continue;
        }
        // Лучше подходит та модель, у которой совпало больше имён команд: она первая в списке.
        const std::vector<irdb::CandidateScore> ranked =
            irdb::rankByCommandNames(database, found, item.commandNames);
        if (!ranked.empty()) {
            found.clear();
            for (const irdb::CandidateScore& score : ranked) {
                found.push_back(score.entry);
            }
        }
        std::size_t entry = found.front();
        if (found.size() > 1) {
            // Несколько подходящих моделей: выбирает пользователь, а не программа.
            QStringList choices;
            for (std::size_t i = 0; i < found.size(); ++i) {
                const irdb::CatalogueEntry& catalogue = database.entries()[found[i]];
                QString text = QString::fromStdString(catalogue.manufacturer + " " + catalogue.model);
                if (i < ranked.size()) {
                    text += tr(" — %1 of %2 commands match").arg(ranked[i].matched).arg(ranked[i].total);
                }
                choices << text;
            }
            const QString skip = tr("— skip this device —");
            choices << skip;
            bool accepted = false;
            const QString chosen =
                QInputDialog::getItem(this, tr("IR codes for “%1”").arg(item.name),
                                      tr("Several models in the IR database fit “%1”. Choose the one that matches "
                                         "your device:")
                                          .arg(item.name),
                                      choices, 0, false, &accepted);
            if (!accepted || chosen == skip) {
                ++unmatched;
                continue;
            }
            entry = found[static_cast<std::size_t>(choices.indexOf(chosen))];
        }
        auto record = database.device(entry);
        if (!record) {
            ++unmatched;
            journal::warning(labels::errorText(record.error()));
            continue;
        }
        HubCodesReport report;
        const bool applied = applyEdit(tr("IR codes from the database for “%1”").arg(item.name),
                                       [&](ConfigDocument& document) -> Result<void> {
                                           auto result = document.matchHubDevice(item.id, *record, database.protocols());
                                           if (!result) {
                                               return std::unexpected(std::move(result.error()));
                                           }
                                           report = std::move(*result);
                                           return {};
                                       });
        if (!applied) {
            continue;
        }
        matched += report.asProtocol + report.asSignal;
        for (const std::string& skipped : report.skipped) {
            journal::warning(tr("IR code not resolved: %1").arg(QString::fromStdString(skipped)));
        }
    }
    if (!pending.empty()) {
        statusBar()->showMessage(tr("IR codes from the database: %1 commands filled, %2 devices without a match. "
                                    "These are not the hub's own codes and may differ.")
                                     .arg(matched)
                                     .arg(unmatched),
                                 15000);
        if (matched > 0) {
            journal::warning(tr("%1 commands got IR codes picked from the database by device model and command "
                                "name (highlighted in the command list). The stock hub does not report its codes, "
                                "so the real ones may differ.")
                                 .arg(matched));
        }
    }
}

void MainWindow::addFromDatabase(int entry) {
    const irdb::IrDatabase* database = irDatabase_->database();
    if (!document_ || database == nullptr || entry < 0 || dbImportThread_ != nullptr) {
        return;
    }
    auto record = database->device(static_cast<std::size_t>(entry));
    if (!record) {
        QMessageBox::warning(this, tr("Add device"), labels::errorText(record.error()));
        return;
    }
    const QString name = QString::fromStdString(record->manufacturer + " " + record->model);
    const QString text = tr("Add “%1”").arg(name);

    // Сигналы всех команд собираются из протоколов — на медленных машинах это секунды.
    // Поэтому в фоне устройство собирается во временной конфигурации, а в открытую
    // переносится готовым (importDevice) — документ меняется только в GUI-потоке.
    struct Prepared {
        std::optional<ConfigDocument> scratch;
        Result<DbImportReport> report;
    };
    auto prepared = std::make_shared<Prepared>();
    auto source = std::make_shared<const irdb::DbDevice>(std::move(*record));
    const irdb::ProtocolLibrary* protocols = &database->protocols();
    QThread* worker = QThread::create([prepared, source, protocols] {
        ConfigDocument scratch = ConfigDocument::createNew("import");
        prepared->report = scratch.importDbDevice(*source, *protocols);
        prepared->scratch.emplace(std::move(scratch));
    });
    worker->setParent(this); // closeEvent дожидается потока
    dbImportThread_ = worker;

    // Окно модальное и не закрывается: пока идёт сборка, базу нельзя закрыть или пересобрать.
    auto* busy = new GuardedProgressDialog(tr("Converting the device's commands…"), QString(), 0, 0, this);
    busy->setCancellable(false);
    busy->setWindowTitle(tr("Add device"));
    busy->setWindowModality(Qt::WindowModal);
    busy->setMinimumDuration(0);
    busy->setMinimumWidth(420);
    busy->show();

    const int serial = documentSerial_;
    connect(worker, &QThread::finished, this, [this, worker, busy, prepared, serial, text, name] {
        busy->deleteLater();
        worker->deleteLater();
        dbImportThread_ = nullptr;
        if (!document_ || documentSerial_ != serial) {
            return; // конфигурацию успели закрыть или сменить
        }
        if (!prepared->report) {
            QMessageBox::warning(this, tr("Add device"), labels::errorText(prepared->report.error()));
            return;
        }
        DbImportReport report = std::move(*prepared->report);
        const ConfigDocument& scratch = *prepared->scratch;
        const bool added = applyEdit(text, [&](ConfigDocument& document) -> Result<void> {
            auto newId = document.importDevice(scratch, report.deviceId);
            if (!newId) {
                return std::unexpected(std::move(newId.error()));
            }
            report.deviceId = *newId;
            return {};
        });
        if (!added) {
            return;
        }
        configView_->select(ConfigView::Item::Device, report.deviceId);
        QStringList parts;
        parts << labels::plural(report.commands, "command", "commands")
              << labels::plural(report.hardButtons + report.softButtons, "button", "buttons");
        parts << (report.power ? tr("the remote turns it on and off") : tr("power: manual"));
        if (report.inputs) {
            parts << tr("inputs described");
        }
        if (!report.skipped.empty()) {
            parts << tr("not converted: %1").arg(report.skipped.size());
        }
        statusBar()->showMessage(tr("Added “%1”: %2. In existing activities it is turned off.")
                                     .arg(name, parts.join(", ")),
                                 10000);
    });
    worker->start();
}

} // namespace harmony::app
