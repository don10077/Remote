/**
 * @file mainWindowRemote.cpp
 * @brief Операции главного окна с пультом: чтение, резервная копия и запись.
 */
#include "mainWindow.h"
#include "configLibrary.h"
#include "documentSource.h"
#include "labels.h"
#include "remoteSession.h"
#include "widgets/writePreviewDialog.h"
#include "widgets/remoteProfileDialog.h"
#include "widgets/remoteSettingsDialog.h"
#include "appSettings.h"
#include "harmony/core/configDiff.h"
#include "harmony/core/remoteProfile.h"
#include "harmony/transport/remoteServices.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextCursor>
#include <QThread>
#include <QVBoxLayout>
#include <QDir>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include "journal.h"
#include <QStatusBar>
#include <algorithm>
#include <map>
#include <memory>

namespace harmony::app {

bool MainWindow::remoteReady(const std::function<void()>& retry) {
    if (!remote_->busy()) {
        return true;
    }
    if (remote_->probing()) {
        // Проверка подключения длится не дольше полутора секунд — действие выполнится после неё.
        pendingRemoteAction_ = retry;
        statusBar()->showMessage(tr("Checking the connection to the remote — continuing in a moment…"), 3000);
    } else {
        statusBar()->showMessage(tr("The remote is busy with another operation. Try again when it finishes."),
                                 6000);
    }
    return false;
}

void MainWindow::startRemoteProgress(const QString& title, const QString& failureTitle) {
    remoteFailureTitle_ = failureTitle;
    progress_ = new GuardedProgressDialog(this);
    progress_->setWindowTitle(title);
    progress_->setLabelText(tr("Connecting to the remote…"));
    progress_->setRange(0, 0);
    progress_->setMinimumDuration(0);
    progress_->setMinimumWidth(420);
    progress_->setAutoClose(false);
    progress_->setAutoReset(false);
    progress_->setWindowModality(Qt::WindowModal);
    connect(progress_, &QProgressDialog::canceled, this, [this] {
        if (progress_ != nullptr && progress_->cancellable()) {
            remote_->cancel();
        }
    });
    progress_->show();
}

void MainWindow::finishRemoteProgress() {
    if (progress_ != nullptr) {
        progress_->deleteLater();
        progress_ = nullptr;
    }
}

void MainWindow::showRemoteProgress(transport::Stage stage, qint64 done, qint64 total) {
    if (progress_ == nullptr) {
        return;
    }
    using transport::Stage;
    QString text;
    bool cancellable = false;
    switch (stage) {
    case Stage::Connect: text = tr("Connecting to the remote…"); break;
    case Stage::Read: text = tr("Reading the configuration from the remote…"); cancellable = true; break;
    case Stage::Write: text = tr("Writing the configuration to the remote…"); cancellable = true; break;
    case Stage::Verify: text = tr("Finishing the write…"); break;
    case Stage::Restart: text = tr("The remote is restarting and installing the configuration — do not "
                                   "disconnect it…"); break;
    case Stage::SetTime: text = tr("Setting the clock…"); break;
    case Stage::Learn: text = tr("Learning a command…"); cancellable = true; break;
    }
    progress_->setLabelText(text);
    // После передачи данных отменять поздно: пульт уже устанавливает конфигурацию.
    progress_->setCancelButtonText(cancellable ? tr("Cancel") : QString());
    progress_->setCancellable(cancellable); // и Esc с крестиком — тоже
    const int scale = total > 0 ? 1000 : 0;
    progress_->setRange(0, scale);
    progress_->setValue(total > 0 ? static_cast<int>(done * 1000 / total) : 0);
}

void MainWindow::readFromRemote() {
    // Открытую конфигурацию не трогаем до конца чтения: потом спросим, что с ней делать.
    if (!remoteReady([this] { readFromRemote(); })) {
        return;
    }
    const auto* profile = remoteProfile(selectedRemote_.toStdString());
    if (!profile || profile->driver == "none") return;
    startRemoteProgress(tr("Read from remote"), tr("Reading from the remote failed"));
    if (usesHarmony900(selectedRemote_.toStdString())) remote_->readConfig();
    else remote_->readHidConfig();
}

void MainWindow::chooseRemote() {
    if (!remoteReady([this] { chooseRemote(); })) return;
    reloadRemoteResources();
    RemoteProfileDialog dialog(selectedRemote_, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto selected = dialog.selectedRemote().toStdString();
    if (document_) {
        if (selected == document_->targetRemote()) return;
        if (!applyEdit(tr("Change target remote"), [&selected](ConfigDocument& document) {
                return document.setTargetRemote(selected);
        })) return;
    }
    selectedRemote_ = QString::fromStdString(selected);
    configView_->refresh();
    updateTitle();
    if (usesHarmony900(selectedRemote_.toStdString())) {
        remote_->probe(); // иначе в шапке до следующей проверки (10 с) — надпись про USB HID
    }
}

void MainWindow::saveHidBackup(const QByteArray& backup) {
    finishRemoteProgress();
    // Сначала автоматическая копия: отмена окна экспорта не теряет прочитанные данные.
    const QString file = QDir(library::backupFolder()).filePath(
        "Harmony-HID-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + ".hid.json");
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly) || out.write(backup) != backup.size() || !out.commit()) {
        QMessageBox::warning(this, tr("Read from remote"), tr("Could not save the file %1").arg(file));
        return;
    }
    journal::info(tr("Remote backup saved: %1").arg(QDir::toNativeSeparators(file)));
    QString destination = QFileDialog::getSaveFileName(this, tr("Save remote backup to send"),
        QDir(library::lastDialogDir()).filePath("Harmony-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") +
                                                ".hid.json"),
        tr("Harmony HID backup (*.hid.json)"));
    if (!destination.isEmpty()) {
        library::setLastDialogDir(QFileInfo(destination).absolutePath());
        if (!destination.endsWith(QLatin1String(".hid.json"), Qt::CaseInsensitive)) {
            destination += QLatin1String(".hid.json"); // диалог Qt суффикс сам не добавляет
        }
    }
    QString saved = file;
    if (!destination.isEmpty() && QFileInfo(destination).absoluteFilePath() != QFileInfo(file).absoluteFilePath()) {
        QSaveFile exportFile(destination);
        if (!exportFile.open(QIODevice::WriteOnly) || exportFile.write(backup) != backup.size() || !exportFile.commit()) {
            QMessageBox::warning(this, tr("Read from remote"),
                tr("Could not save the file %1\nCopy saved: %2").arg(destination, file));
            return;
        }
        saved = destination;
    }
    QMessageBox box(QMessageBox::Information, tr("Read from remote"),
        tr("The original configuration and remote information are saved in one file. Send this file for analysis. Editing and writing this format are not supported yet.\n\n%1")
            .arg(QDir::toNativeSeparators(saved)), QMessageBox::Ok, this);
    auto* folder = box.addButton(tr("Open folder"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == folder)
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(saved).absolutePath()));
}

void MainWindow::openFromRemote(const QByteArray& ezhex) {
    finishRemoteProgress();
    // Снятая конфигурация сразу ложится резервной копией — и открывается из неё.
    const QString file = library::newBackupFile();
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly) || out.write(ezhex) != ezhex.size() || !out.commit()) {
        QMessageBox::warning(this, tr("Read from remote"), tr("Could not save the file %1").arg(file));
        return;
    }
    const QString saved = tr("Copy saved: %1").arg(QDir::toNativeSeparators(file));
    // Открывается без места на диске: «Сохранить» не должно перезаписать резервную копию.
    auto openUnsaved = [this, &ezhex, &file, &saved] {
        const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(ezhex.constData()),
                                     static_cast<std::size_t>(ezhex.size()));
        auto document = ConfigDocument::fromEzHex(bytes, QFileInfo(file).completeBaseName().toStdString());
        if (!document) {
            QMessageBox::warning(this, tr("Read from remote"), labels::errorText(document.error()));
            return;
        }
        showDocument(std::move(*document));
        statusBar()->showMessage(tr("Configuration read from the remote. %1").arg(saved), 10000);
    };
    if (!document_) {
        openUnsaved();
        return;
    }
    auto remote = ConfigDocument::open(toPath(file));
    if (!remote) {
        QMessageBox::warning(this, tr("Read from remote"), labels::errorText(remote.error()));
        return;
    }

    // Открыта конфигурация: заменить её содержимое (с отменой) или открыть прочитанное отдельно.
    QMessageBox box(QMessageBox::Question, tr("Read from remote"), {}, QMessageBox::NoButton, this);
    box.setText(tr("Configuration read from the remote: %1, %2.")
                    .arg(labels::plural(static_cast<qsizetype>(remote->model().devices.size()), "device", "devices"),
                         labels::plural(static_cast<qsizetype>(remote->model().userActivityCount()), "activity", "activities")));
    const QString undoKey = labels::shortcut(QKeySequence::Undo);
    box.setInformativeText(tr("%1\n\n“Replace current” — the contents of “%2” will match the remote; "
                              "“Save” writes them to the same place, undo with %3.\n\n%4")
                               .arg(differenceText(document_->model(), remote->model()),
                                    QString::fromStdString(document_->name()), undoKey, saved));
    QPushButton* replace = box.addButton(tr("Replace current"), QMessageBox::AcceptRole);
    QPushButton* separate = box.addButton(tr("Open separately"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(replace);
    box.exec();
    if (box.clickedButton() == replace) {
        if (applyEdit(tr("Configuration from the remote"),
                      [&](ConfigDocument& document) { return document.replaceContent(*remote); })) {
            statusBar()->showMessage(
                tr("Contents replaced with the configuration from the remote. Undo — %1.").arg(undoKey),
                10000);
        }
    } else if (box.clickedButton() == separate && confirmDiscard()) {
        openUnsaved();
    }
}

QString MainWindow::differenceText(const model::RemoteConfig& current, const model::RemoteConfig& remote) {
    // Сравнение по именам: что есть только на пульте и что только в открытой конфигурации.
    auto names = [](const auto& items, auto name) {
        QStringList list;
        for (const auto& item : items) {
            list << name(item);
        }
        return list;
    };
    auto deviceName = [](const model::Device& device) { return QString::fromStdString(device.displayName()); };
    auto activityName = [](const model::Activity& activity) { return QString::fromStdString(activity.label); };
    const QStringList currentNames = names(current.devices, deviceName) + names(current.activities, activityName);
    const QStringList remoteNames = names(remote.devices, deviceName) + names(remote.activities, activityName);
    QStringList onlyRemote;
    QStringList onlyCurrent;
    for (const QString& name : remoteNames) {
        if (!currentNames.contains(name)) {
            onlyRemote << name;
        }
    }
    for (const QString& name : currentNames) {
        if (!remoteNames.contains(name)) {
            onlyCurrent << name;
        }
    }
    auto shortList = [](QStringList list) {
        const qsizetype rest = list.size() - 5;
        list = list.mid(0, 5);
        return rest > 0 ? QObject::tr("%1 and %2 more").arg(list.join(", "), QString::number(rest)) : list.join(", ");
    };
    QStringList lines;
    if (!onlyRemote.isEmpty()) {
        lines << tr("Only on the remote: %1.").arg(shortList(onlyRemote));
    }
    if (!onlyCurrent.isEmpty()) {
        lines << tr("Only in the open one: %1.").arg(shortList(onlyCurrent));
    }
    return lines.isEmpty() ? tr("Same devices and activities (buttons and commands may differ).")
                           : lines.join('\n');
}

void MainWindow::writeToRemote() {
    if (!document_ || !remoteReady([this] { writeToRemote(); })) {
        return;
    }
    const auto* profile = remoteProfile(document_->targetRemote());
    if (!profile || !profile->writable) return;
    // Сначала — проверка: с ошибками часть кнопок на пульте молча не сработает.
    const auto problems = document_->check();
    if (const auto errors = static_cast<qsizetype>(countProblems(problems, Problem::Severity::Error)); errors > 0) {
        const bool fixable = std::ranges::any_of(problems, [](const Problem& p) {
            return p.severity == Problem::Severity::Error && p.fixable();
        });
        QMessageBox box(this);
        box.setWindowTitle(tr("Write to remote"));
        box.setIcon(QMessageBox::Warning);
        box.setText(tr("The configuration has %1: some buttons or steps will not work on the remote.")
                        .arg(labels::plural(errors, "error", "errors")));
        QPushButton* fix = fixable ? box.addButton(tr("Fix and continue"), QMessageBox::AcceptRole) : nullptr;
        QPushButton* show = box.addButton(tr("Show problems"), QMessageBox::ActionRole);
        QPushButton* anyway = box.addButton(tr("Write anyway"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(fix != nullptr ? fix : show);
        box.exec();
        if (box.clickedButton() == show) {
            showCheck();
            return;
        }
        if (fix != nullptr && box.clickedButton() == fix) {
            applyEdit(tr("Fix all problems"), [](ConfigDocument& document) -> Result<void> {
                auto fixed = document.fixAll();
                return fixed ? Result<void>() : std::unexpected(std::move(fixed.error()));
            });
        } else if (box.clickedButton() != anyway) {
            return;
        }
    }
    // Читаем пульт (это и резервная копия), показываем, что изменится, и только потом пишем.
    startRemoteProgress(tr("Write to remote"), tr("Writing to the remote failed"));
    remote_->prepareWrite(library::newBackupFile());
}

void MainWindow::backupSaved(const QByteArray& ezhex, const QString& backupFile) {
    finishRemoteProgress();
    if (!document_) {
        return;
    }
    const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(ezhex.constData()),
                                 static_cast<std::size_t>(ezhex.size()));
    auto current = ConfigDocument::fromEzHex(bytes, "remote");
    std::vector<ConfigChange> changes;
    if (current) {
        changes = compareConfigurations(*current, *document_);
    }
    WritePreviewDialog preview(changes, backupFile, this);
    if (preview.exec() != QDialog::Accepted) {
        statusBar()->showMessage(tr("Writing cancelled. The backup is kept: %1")
                                     .arg(QDir::toNativeSeparators(backupFile)),
                                 8000);
        return;
    }
    // Настройки RF-бластеров — с пульта, как они сейчас: их меняют на самом пульте.
    auto next = current ? document_->toEzHexKeepingRemoteRf(*current) : document_->toEzHex(true);
    if (!current) {
        journal::warning(tr("The remote's configuration could not be read: RF blaster settings are written from "
                            "the open configuration"));
    }
    if (!next) {
        QMessageBox::warning(this, tr("Write to remote"), labels::errorText(next.error()));
        return;
    }
    pendingBackup_ = backupFile;
    startRemoteProgress(tr("Writing to remote"), tr("Writing to the remote failed"));
    remote_->writeConfig(std::move(*next), QString()); // копия уже сохранена
}

void MainWindow::remoteWritten(const QString& written) {
    finishRemoteProgress();
    const QString backupFile = written.isEmpty() ? pendingBackup_ : written;
    pendingBackup_.clear();
    QMessageBox::information(this, tr("Write to remote"),
                             tr("Configuration written, remote restarted.<br><br>The remote's previous "
                                "configuration is saved: %1")
                                 .arg(QDir::toNativeSeparators(backupFile)));
}

void MainWindow::showRemoteSettings() {
    auto apply = [this](const QMap<QString, QString>& values) {
        if (!document_) {
            return false;
        }
        const bool applied = applyEdit(tr("Remote settings"), [&values](ConfigDocument& document) -> Result<void> {
            for (auto it = values.cbegin(); it != values.cend(); ++it) {
                if (auto set = document.setRemotePreference(it.key().toStdString(), it.value().toStdString()); !set) {
                    return set;
                }
            }
            return {};
        });
        // Настройки пульта живут в архиве .EZHex; папка хранит только userconfig.
        if (applied && document_->ezhexFile().empty() && document_->hasFolder()) {
            QMessageBox::information(this, tr("Remote settings"),
                                     tr("The settings will be written to the remote with the configuration. "
                                        "A configuration folder does not store them: to keep them, save the "
                                        "configuration as an .EZHex file."));
        }
        return applied;
    };
    RemoteSettingsDialog dialog(document_.get(), apply, this);
    dialog.exec();
}

void MainWindow::showRemoteLog() {
    // Одно окно на программу: закрытое только прячется, повторный вызов читает журнал заново.
    if (remoteLogDialog_ == nullptr) {
        auto* dialog = new QDialog(this);
        remoteLogDialog_ = dialog;
        dialog->setWindowTitle(tr("Remote system log"));
        dialog->resize(900, 600);
        auto* layout = new QVBoxLayout(dialog);
        auto* text = new QPlainTextEdit(dialog);
        text->setReadOnly(true);
        text->setLineWrapMode(QPlainTextEdit::NoWrap);
        text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        layout->addWidget(text);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        QPushButton* copy = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
        QPushButton* save = buttons->addButton(tr("Save…"), QDialogButtonBox::ActionRole);
        copy->setObjectName("remoteLogCopy");
        save->setObjectName("remoteLogSave");
        connect(copy, &QPushButton::clicked, dialog,
                [text] { QGuiApplication::clipboard()->setText(text->toPlainText()); });
        connect(save, &QPushButton::clicked, dialog, [dialog, text] {
            QString file = QFileDialog::getSaveFileName(
                dialog, tr("Save remote log"),
                QDir(library::lastDialogDir())
                    .filePath("harmony-log-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".txt"),
                tr("Text (*.txt)"));
            if (file.isEmpty()) return;
            library::setLastDialogDir(QFileInfo(file).absolutePath());
            if (QFileInfo(file).suffix().isEmpty()) {
                file += QLatin1String(".txt"); // диалог Qt суффикс сам не добавляет
            }
            QSaveFile out(file);
            const QByteArray bytes = text->toPlainText().toUtf8();
            if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
                QMessageBox::warning(dialog, tr("Save remote log"), tr("Could not save the file %1").arg(file));
            }
        });
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
        layout->addWidget(buttons);
    }
    QDialog* dialog = remoteLogDialog_;
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    if (remoteLogThread_ != nullptr) {
        return; // журнал уже читается: результат придёт в это же окно
    }

    auto* text = dialog->findChild<QPlainTextEdit*>();
    auto* copy = dialog->findChild<QPushButton*>("remoteLogCopy");
    auto* save = dialog->findChild<QPushButton*>("remoteLogSave");
    text->setPlainText(tr("Reading the log from the remote…"));
    copy->setEnabled(false); // пока в окне заглушка, копировать и сохранять нечего
    save->setEnabled(false);

    // Чтение занимает секунды — в потоке окна; closeEvent дожидается его, итог приходит в GUI-поток.
    const std::string host = settings::remoteHost().toStdString();
    auto log = std::make_shared<Result<std::string>>();
    QThread* worker = QThread::create([host, log] { *log = transport::readRemoteLog(host); });
    worker->setParent(this);
    remoteLogThread_ = worker;
    connect(worker, &QThread::finished, this, [this, worker, log] {
        worker->deleteLater();
        remoteLogThread_ = nullptr;
        const bool ok = log->has_value();
        const QString shown = ok ? QString::fromUtf8((*log)->data(), static_cast<qsizetype>((*log)->size()))
                                 : labels::errorText(log->error());
        if (ok) {
            journal::info(QObject::tr("Remote system log read"));
        } else {
            journal::error(QObject::tr("Remote system log: %1").arg(shown));
        }
        if (remoteLogDialog_ == nullptr) {
            return;
        }
        if (auto* view = remoteLogDialog_->findChild<QPlainTextEdit*>()) {
            view->setPlainText(shown);
            view->moveCursor(QTextCursor::End);
        }
        for (const char* name : {"remoteLogCopy", "remoteLogSave"}) {
            if (auto* button = remoteLogDialog_->findChild<QPushButton*>(name)) {
                button->setEnabled(ok);
            }
        }
    });
    worker->start();
}

void MainWindow::remoteFailed(const QString& message, bool cancelled) {
    pendingBackup_.clear();
    // Ошибки обучения показывает окно обучения; здесь — только операции с прогрессом.
    if (progress_ == nullptr) {
        return;
    }
    finishRemoteProgress();
    if (cancelled) {
        statusBar()->showMessage(message, 8000);
        return;
    }
    QMessageBox box(QMessageBox::Warning, remoteFailureTitle_, message, QMessageBox::Close, this);
    QPushButton* help = box.addButton(tr("How to connect…"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == help) {
        showConnectionHelp();
    }
}

} // namespace harmony::app
