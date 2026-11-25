#include "configTransferDialog.h"
#include "../appSettings.h"
#include "../labels.h"
#include "../configLibrary.h"
#include "harmony/core/formats/remoteCapture.h"
#include "harmony/core/remoteCatalog.h"
#include "harmony/transport/concordTransfer.h"
#include "harmony/transport/hubConfigReader.h"

#include <QComboBox>
#include <QInputDialog>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QThread>
#include <QVBoxLayout>

namespace harmony::app {
ConfigTransferDialog::ConfigTransferDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Read configuration — all remote models"));
    resize(680, 350);
    auto* layout = new QVBoxLayout(this);
    auto* description = new QLabel(tr("Connect the remote by USB and read its configuration into one file (*.remote.json). The model is detected automatically and the file is checked after saving; the remote is not changed. Remotes paired with a Hub (Elite, Ultimate, Touch, 950, Companion) are read with the Hub button instead."), this);
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
    hub_ = new QPushButton(tr("Read Harmony Hub over network…"), this);
    hub_->setToolTip(tr("Elite, Ultimate, Ultimate Home, Touch, 950, Companion and other remotes paired with a Hub: "
                        "devices, commands and activities are read from the Hub on your network. Rooting is not "
                        "needed to read. The Hub's whole answer is saved as it is: a stock Hub gives out no IR codes "
                        "(they are picked from the IR database by model and command names and may differ), while a "
                        "rooted Hub gives out everything it has. Account data is not included."));
    close_ = new QPushButton(tr("Close"), this);
    buttons->addWidget(read_); buttons->addWidget(hub_);
    buttons->addStretch(); buttons->addWidget(close_);
    layout->addLayout(buttons);
    connect(models_, &QComboBox::currentIndexChanged, this, &ConfigTransferDialog::refresh);
    connect(read_, &QPushButton::clicked, this, &ConfigTransferDialog::readToFile);
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
    if (!model) return;
    status_->setText(model->id == "auto" ? tr("The connected remote is detected by itself; the file records which remote it came from.") :
        model->skins.empty() ? tr("The identity of this model is not known yet.") :
        model->readable ? tr("This model is read over USB.") :
        tr("This model is not read over USB. If it is paired with a Hub, use “Read Harmony Hub over network”."));
}
void ConfigTransferDialog::readToFile() {
    if (worker_) return;
    const std::string id = models_->currentData().toString().toStdString();
    const auto* model = catalogModel(id);
    if (!model || !model->readable) return;
    // Читается в один файл *.remote.json: контейнер с пульта плюс сведения о снятии и SHA-256.
    const auto now = QDateTime::currentDateTime();
    const QString suggested = QDir(library::lastDialogDir()).filePath(QString::fromStdString(formats::captureFileName(
        id, now.date().year(), now.date().month(), now.date().day(), now.time().hour(), now.time().minute())));
    QString filename = QFileDialog::getSaveFileName(this, tr("Read configuration to file"), suggested,
                                                    tr("Remote capture (*.remote.json)"));
    const QString suffix = QString::fromLatin1(formats::kRemoteCaptureExtension);
    if (!filename.isEmpty() && !filename.endsWith(suffix, Qt::CaseInsensitive)) filename += suffix;
    if (filename.isEmpty()) return;
    library::setLastDialogDir(QFileInfo(filename).absolutePath());
    const transport::Endpoint endpoint{settings::remoteHost().toStdString(), static_cast<std::uint16_t>(settings::remotePort())};
    setBusy(true);
    status_->setText(tr("Connecting…"));
    progress_->setRange(0, 0);
    worker_ = QThread::create([this, id, filename, endpoint] {
        const auto progress = [this](transport::Stage stage, std::size_t done, std::size_t total) {
            QMetaObject::invokeMethod(this, [this, stage, done, total] {
                status_->setText(stage == transport::Stage::Read
                    ? (total == 0 && done > 0
                        ? tr("Receiving configuration (%1 KB)…").arg((done + 1023) / 1024)
                        : tr("Reading configuration…"))
                    : tr("Connecting to the remote…"));
                progress_->setRange(0, total ? 100 : 0);
                if (total) progress_->setValue(static_cast<int>(done * 100 / total));
            }, Qt::QueuedConnection);
            return true;
        };
        transport::ConcordTransfer session;
        const auto connected = session.connect(id, endpoint);
        QString error;
        QString summary;
        if (!connected) error = labels::errorText(connected.error());
        else {
            auto result = session.read(progress);
            if (!result) error = labels::errorText(result.error());
            else {
                const auto capture = transport::makeCapture(*connected, std::move(*result));
                const auto json = formats::writeRemoteCapture(capture);
                if (!json) error = labels::errorText(json.error());
                else {
                    QSaveFile output(filename);
                    if (!output.open(QIODevice::WriteOnly) || output.write(json->data(), static_cast<qint64>(json->size())) != static_cast<qint64>(json->size()) || !output.commit())
                        error = tr("Could not save the configuration file.");
                    else {
                        // Проверка по файлу с диска, как его прочтёт получатель.
                        QFile saved(filename);
                        const QByteArray text = saved.open(QIODevice::ReadOnly) ? saved.readAll() : QByteArray();
                        const auto parsed = formats::parseRemoteCapture(std::string_view(text.constData(), static_cast<std::size_t>(text.size())));
                        if (!parsed) error = labels::errorText(parsed.error());
                        else if (const auto check = transport::inspectCapture(*parsed); !check)
                            error = labels::errorText(check.error());
                        else summary = tr("%1, %2 bytes; SHA-256 and container checksum are correct.")
                                           .arg(QString::fromStdString(connected->remote.model))
                                           .arg(static_cast<qulonglong>(capture.data.size()));
                    }
                }
            }
        }
        QMetaObject::invokeMethod(this, [this, error, filename, summary] {
            progress_->setRange(0, 100); progress_->setValue(error.isEmpty() ? 100 : 0);
            status_->setText(error.isEmpty()
                ? tr("Saved: %1\n%2\nThe remote was not changed. You can send this file to the developers.")
                      .arg(QDir::toNativeSeparators(filename), summary)
                : error);
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
        read_->setEnabled(false);
        return;
    }
    const auto* selected = catalogModel(models_->currentData().toString().toStdString());
    read_->setEnabled(selected && selected->readable);
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
        QDir(library::lastDialogDir()).filePath("HarmonyHub-" + safeHost + "-" +
            QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".hub.json"),
        tr("Harmony Hub configuration (*.hub.json);;All files (*)"));
    if (filename.isEmpty()) return;
    library::setLastDialogDir(QFileInfo(filename).absolutePath());
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
                          "The file holds the Hub's answer as it is — device names, models and commands, plus IR "
                          "codes if this Hub gives them out. Account and e-mail are not included. "
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
