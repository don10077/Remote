#include "configTransferDialog.h"
#include "../appSettings.h"
#include "../labels.h"
#include "../configLibrary.h"
#include "harmony/core/remoteCatalog.h"
#include "harmony/transport/concordTransfer.h"
#include "harmony/transport/hubConfigReader.h"

#include <QComboBox>
#include <QInputDialog>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QThread>
#include <QVBoxLayout>

namespace harmony::app {
ConfigTransferDialog::ConfigTransferDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Configuration files — all remote models"));
    resize(680, 350);
    auto* layout = new QVBoxLayout(this);
    auto* description = new QLabel(tr("Read a backup or write an existing configuration file from Concordance, Afterglow, re-harmony or Harmony. A photo and button zones are not required. Writing a file does not convert it to another model."), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    models_ = new QComboBox(this);
    models_->addItem(tr("Automatic detection"), "auto");
    for (const auto& model : remoteCatalog())
        models_->addItem(QString::fromUtf8(model.name.data(), static_cast<qsizetype>(model.name.size())),
                         QString::fromUtf8(model.id.data(), static_cast<qsizetype>(model.id.size())));
    layout->addWidget(models_);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    layout->addWidget(progress_);
    auto* buttons = new QHBoxLayout;
    read_ = new QPushButton(tr("Read configuration to file…"), this);
    write_ = new QPushButton(tr("Write configuration file…"), this);
    hub_ = new QPushButton(tr("Read Harmony Hub over network…"), this);
    hub_->setToolTip(tr("Elite, Ultimate, Ultimate Home, 950, Companion and other remotes paired with a Hub: "
                        "devices, commands and activities are read from the Hub on your network. "
                        "No rooting is needed. A stock Hub does not give out IR codes: they are picked from the "
                        "IR database by model and command names and may differ from the codes in the Hub. "
                        "Account data is not included."));
    close_ = new QPushButton(tr("Close"), this);
    buttons->addWidget(read_); buttons->addWidget(write_); buttons->addWidget(hub_);
    buttons->addStretch(); buttons->addWidget(close_);
    layout->addLayout(buttons);
    connect(models_, &QComboBox::currentIndexChanged, this, &ConfigTransferDialog::refresh);
    connect(read_, &QPushButton::clicked, this, [this] { transfer(false); });
    connect(write_, &QPushButton::clicked, this, [this] { transfer(true); });
    connect(hub_, &QPushButton::clicked, this, &ConfigTransferDialog::readHub);
    connect(close_, &QPushButton::clicked, this, &ConfigTransferDialog::reject);
    refresh();
}
ConfigTransferDialog::~ConfigTransferDialog() {
    if (worker_) worker_->wait();
}
void ConfigTransferDialog::reject() {
    if (!worker_) QDialog::reject();
}
void ConfigTransferDialog::refresh() {
    const auto* model = catalogModel(models_->currentData().toString().toStdString());
    read_->setEnabled(model && model->readable && !worker_);
    write_->setEnabled(model && model->writable && !worker_);
    if (!model) return;
    status_->setText(model->id == "auto" ? tr("The connected remote and the file's intended model will be checked before writing.") :
        model->skins.empty() ? tr("The identity of this model is not known yet.") :
        (model->writeTested ? tr("Writing from Harmony has been tested on this model.") :
                             tr("Transfer code is available. Writing from Harmony has not been tested on this model.")) +
        (model->readable ? QString() : "\n" + tr("This model does not support configuration readback through this transport.")));
}
void ConfigTransferDialog::transfer(bool writing) {
    if (worker_) return;
    const std::string id = models_->currentData().toString().toStdString();
    const auto* model = catalogModel(id);
    if (!model || (writing ? !model->writable : !model->readable)) return;
    const QString filter = tr("Remote configuration (*.EZHex *.ezhex *.zip *.hex);;All files (*)");
    const QString filename = writing ? QFileDialog::getOpenFileName(this, tr("Write configuration file"), {}, filter) :
        QFileDialog::getSaveFileName(this, tr("Read configuration to file"), "Harmony-" + QString::fromStdString(id) + ".EZHex", filter);
    if (filename.isEmpty()) return;
    QByteArray contents;
    QString backup;
    if (writing) {
        QFile file(filename);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) {
            QMessageBox::warning(this, windowTitle(), tr("Cannot read the configuration file.")); return;
        }
        contents = file.readAll();
        const auto inspected = transport::inspectTransferFile({reinterpret_cast<const std::uint8_t*>(contents.constData()), static_cast<std::size_t>(contents.size())});
        if (!inspected) { QMessageBox::warning(this, windowTitle(), labels::errorText(inspected.error())); return; }
        backup = QDir(library::backupFolder()).filePath("Harmony-" + QString::fromStdString(id) + "-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + ".EZHex");
        const QString detail = model->readable ? tr("A backup will be saved before writing:\n%1").arg(QDir::toNativeSeparators(backup)) :
            tr("This remote cannot be backed up through this connection. Continue without a backup?");
        if (QMessageBox::question(this, tr("Write configuration file"),
            tr("Write %1 to %2?\n\n%3\n\nDo not disconnect the remote while writing.")
                .arg(QDir::toNativeSeparators(filename), models_->currentText(), detail),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    }
    const transport::Endpoint endpoint{settings::remoteHost().toStdString(), static_cast<std::uint16_t>(settings::remotePort())};
    const bool allowWithoutBackup = writing && !model->readable;
    setBusy(true);
    status_->setText(tr("Connecting…"));
    progress_->setRange(0, 0);
    worker_ = QThread::create([this, id, filename, contents, backup, writing, endpoint, allowWithoutBackup] {
        const auto progress = [this](transport::Stage stage, std::size_t done, std::size_t total) {
            QMetaObject::invokeMethod(this, [this, stage, done, total] {
                status_->setText(stage == transport::Stage::Read ? tr("Reading configuration…") :
                    stage == transport::Stage::Restart ? tr("Restarting remote…") : tr("Transferring configuration…"));
                progress_->setRange(0, total ? 100 : 0);
                if (total) progress_->setValue(static_cast<int>(done * 100 / total));
            }, Qt::QueuedConnection);
            return true;
        };
        transport::ConcordTransfer session;
        const auto connected = session.connect(id, endpoint);
        QString error;
        if (!connected) error = labels::errorText(connected.error());
        else if (writing) {
            const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(contents.constData()), static_cast<std::size_t>(contents.size()));
            const auto result = session.write(bytes, std::filesystem::path(backup.toStdU16String()), allowWithoutBackup, progress);
            if (!result) error = labels::errorText(result.error());
        } else {
            auto result = session.read(progress);
            if (!result) error = labels::errorText(result.error());
            else {
                QSaveFile output(filename);
                if (!output.open(QIODevice::WriteOnly) || output.write(reinterpret_cast<const char*>(result->data()), static_cast<qint64>(result->size())) != static_cast<qint64>(result->size()) || !output.commit())
                    error = tr("Could not save the configuration file.");
            }
        }
        QMetaObject::invokeMethod(this, [this, error, writing, filename, backup, allowWithoutBackup] {
            progress_->setRange(0, 100); progress_->setValue(error.isEmpty() ? 100 : 0);
            status_->setText(error.isEmpty() ? (writing ? tr("Configuration written.") +
                (allowWithoutBackup ? QString() : "\n" + tr("Backup: %1").arg(QDir::toNativeSeparators(backup))) :
                tr("Configuration saved: %1").arg(QDir::toNativeSeparators(filename))) : error);
        }, Qt::QueuedConnection);
    });
    connect(worker_, &QThread::finished, this, [this] {
        worker_->deleteLater(); worker_ = nullptr;
        setBusy(false);
    });
    worker_->start();
}

void ConfigTransferDialog::setBusy(bool busy) {
    models_->setEnabled(!busy); close_->setEnabled(!busy); hub_->setEnabled(!busy);
    if (busy) {
        read_->setEnabled(false); write_->setEnabled(false);
        return;
    }
    const auto* selected = catalogModel(models_->currentData().toString().toStdString());
    read_->setEnabled(selected && selected->readable); write_->setEnabled(selected && selected->writable);
}

void ConfigTransferDialog::readHub() {
    if (worker_) return;
    bool accepted = false;
    const QString host = QInputDialog::getText(this, tr("Read Harmony Hub over network"),
        tr("IP address of the Harmony Hub on your network (see the router or the Harmony app):"),
        QLineEdit::Normal, settings::hubHost(), &accepted).trimmed();
    if (!accepted || host.isEmpty()) return;
    settings::setHubHost(host);
    QString safeHost = host;
    safeHost.replace(':', '-');
    const QString filename = QFileDialog::getSaveFileName(this, tr("Save Harmony Hub configuration"),
        QDir(library::backupFolder()).filePath("HarmonyHub-" + safeHost + "-" +
            QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".hub.json"),
        tr("Harmony Hub configuration (*.hub.json);;All files (*)"));
    if (filename.isEmpty()) return;
    hubFile_.clear();
    setBusy(true);
    status_->setText(tr("Connecting to the Hub at %1…").arg(host));
    progress_->setRange(0, 0);
    worker_ = QThread::create([this, host, filename] {
        const auto progress = [this](transport::Stage stage, std::size_t done, std::size_t total) {
            QMetaObject::invokeMethod(this, [this, stage, done, total] {
                status_->setText(stage == transport::Stage::Read ? tr("Receiving configuration (%1 KB)…").arg((done + 1023) / 1024)
                                                                 : tr("Connecting to the Hub…"));
                progress_->setRange(0, total ? 100 : 0);
                if (total) progress_->setValue(static_cast<int>(done * 100 / total));
            }, Qt::QueuedConnection);
            return true;
        };
        const auto hub = transport::readHubConfig(host.toStdString(), transport::kHubPort, progress);
        QString text;
        bool ok = false;
        if (!hub) {
            text = labels::errorText(hub.error());
        } else {
            QSaveFile output(filename);
            const QByteArray bytes = QByteArray::fromStdString(hub->backupJson);
            if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
                text = tr("Could not save the configuration file.");
            } else {
                ok = true;
                text = tr("Hub “%1”, firmware %2: %3 devices, %4 activities.\nSaved: %5\n"
                          "The file contains device names and models, but no IR codes, account or e-mail. "
                          "You can send it to the developers.")
                           .arg(QString::fromStdString(hub->hubName), QString::fromStdString(hub->firmware))
                           .arg(static_cast<qulonglong>(hub->devices.size()))
                           .arg(static_cast<qulonglong>(hub->activities.size()))
                           .arg(QDir::toNativeSeparators(filename));
            }
        }
        QMetaObject::invokeMethod(this, [this, text, ok, filename] {
            progress_->setRange(0, 100); progress_->setValue(ok ? 100 : 0);
            status_->setText(text);
            if (ok) hubFile_ = filename; // окно закроется, редактор откроет файл
        }, Qt::QueuedConnection);
    });
    connect(worker_, &QThread::finished, this, [this] {
        worker_->deleteLater(); worker_ = nullptr;
        setBusy(false);
        if (!hubFile_.isEmpty()) accept();
    });
    worker_->start();
}
} // namespace harmony::app
