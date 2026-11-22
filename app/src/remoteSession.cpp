/**
 * @file remoteSession.cpp
 * @brief Операции с пультом в фоновом потоке.
 */

#include "remoteSession.h"

#include "labels.h"
#include "appSettings.h"
#include "journal.h"

#include "harmony/transport/linkFactory.h"
#include "harmony/transport/usbNetLink.h"
#include "harmony/transport/concordTransfer.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QMetaObject>
#include <QSaveFile>
#include <QThread>

namespace harmony::app {

namespace {

/// Подпись состояния пульта для шапки.
QString describe(const transport::RemoteInfo& info) {
    return QObject::tr("%1 · firmware %2")
        .arg(QString::fromStdString(info.model), QString::fromStdString(info.firmwareVersion));
}

} // namespace

RemoteSession::RemoteSession(QObject* parent) : QObject(parent) {
    // Журнал: итоги операций (проверка подключения — только когда состояние меняется).
    connect(this, &RemoteSession::stateChanged, this, [this](bool connected, const QString& text) {
        if (connected != lastConnected_) {
            lastConnected_ = connected;
            journal::info(connected ? tr("Remote connected: %1").arg(text) : tr("Remote disconnected"));
        }
    });
    connect(this, &RemoteSession::readFinished, this, [](const QByteArray& ezhex) {
        journal::info(tr("Configuration read from the remote (%1 KB)").arg((ezhex.size() + 1023) / 1024));
    });
    connect(this, &RemoteSession::backupSaved, this, [](const QByteArray&, const QString& file) {
        journal::info(tr("Remote backup saved: %1").arg(QDir::toNativeSeparators(file)));
    });
    connect(this, &RemoteSession::writeFinished, this, [](const QString&) {
        journal::info(tr("Configuration written to the remote"));
    });
    connect(this, &RemoteSession::learnFinished, this, [](const ir::IrSignal& signal) {
        journal::info(tr("Signal received: %1").arg(labels::signalSummary(signal)));
        journal::debug(tr("Timings: %1").arg(QString::fromStdString(ir::timingText(signal))));
    });
    connect(this, &RemoteSession::failed, this, [](const QString& message, bool cancelled) {
        journal::write(cancelled ? journal::Level::Info : journal::Level::Error,
                       cancelled ? tr("Cancelled: %1").arg(message) : tr("Remote: %1").arg(message));
    });
}

RemoteSession::~RemoteSession() {
    // Дождаться потока: иначе он обратится к уже удалённому объекту.
    if (thread_ != nullptr) {
        cancelled_ = true;
        thread_->wait();
    }
}

void RemoteSession::run(std::function<void()> work) {
    cancelled_ = false;
    thread_ = QThread::create(std::move(work));
    connect(thread_, &QThread::finished, this, [this] {
        thread_->deleteLater();
        thread_ = nullptr;
        probing_ = false;
        emit idle();
    });
    thread_->start();
}

transport::ProgressHandler RemoteSession::progressHandler() {
    return [this](transport::Stage stage, std::size_t done, std::size_t total) {
        QMetaObject::invokeMethod(this, [this, stage, done, total] {
            emit progress(stage, static_cast<qint64>(done), static_cast<qint64>(total));
        }, Qt::QueuedConnection);
        return !cancelled_;
    };
}

Result<transport::LinkChoice> RemoteSession::openLink(std::chrono::milliseconds connectTimeout) {
    transport::LinkTimings timings;
    if (connectTimeout.count() > 0) timings.connect = connectTimeout;
    return transport::makeLink(model_, timings);
}

QString RemoteSession::transferText() const {
    // Модель подключается не по USB-сети: для неё остаётся путь передачи — чтение и сохранение
    // файла (readCapture), а эта операция требует связи.
    return tr("%1 connects by %2: reading and saving go through the transfer path, this operation is not available for it")
        .arg(QString::fromStdString(model_.name),
             QString::fromStdString(std::string(harmony::linkName(model_.link))));
}

void RemoteSession::reportState(bool connected, const QString& text) {
    QMetaObject::invokeMethod(this, [this, connected, text] { emit stateChanged(connected, text); },
                              Qt::QueuedConnection);
}

void RemoteSession::reportFailure(const QString& message, bool cancelled) {
    QMetaObject::invokeMethod(this, [this, message, cancelled] { emit failed(message, cancelled); },
                              Qt::QueuedConnection);
}

void RemoteSession::reportFailure(const Error& error) {
    reportFailure(labels::errorText(error), error.code == ErrorCode::Cancelled);
}

Result<transport::RemoteInfo> RemoteSession::connectAndReport(transport::RemoteLink& link) {
    // Адрес и порт — из настроек (обычно 169.254.1.2:3074).
    const transport::Endpoint endpoint{.host = settings::remoteHost().toStdString(),
                                       .port = static_cast<std::uint16_t>(settings::remotePort())};
    auto info = link.connect(endpoint);
    const bool connected = info.has_value();
    reportState(connected, connected ? describe(*info) : tr("Remote not connected"));
    return info;
}

void RemoteSession::probe() {
    if (busy()) {
        return;
    }
    probing_ = true; // сбрасывается по окончании потока
    run([this] {
        const auto choice = openLink(std::chrono::milliseconds(1500));
        if (!choice) {
            reportState(false, labels::errorText(choice.error()));
            return;
        }
        if (!choice->link) {
            reportState(false, transferText());
            return;
        }
        (void)connectAndReport(*choice->link);
    });
}

void RemoteSession::readConfig() {
    if (busy()) {
        return;
    }
    journal::info(tr("Reading the configuration from the remote…"));
    run([this] {
        const auto choice = openLink();
        if (!choice) { reportFailure(choice.error()); return; }
        if (!choice->link) { reportFailure(transferText(), false); return; }
        auto& link = *choice->link;
        if (auto info = connectAndReport(link); !info) {
            reportFailure(info.error());
            return;
        }
        auto ezhex = link.readConfig(progressHandler());
        if (!ezhex) {
            reportFailure(ezhex.error());
            return;
        }
        const QByteArray bytes(reinterpret_cast<const char*>(ezhex->data()), static_cast<qsizetype>(ezhex->size()));
        QMetaObject::invokeMethod(this, [this, bytes] { emit readFinished(bytes); }, Qt::QueuedConnection);
    });
}

void RemoteSession::readCapture() {
    if (busy()) return;
    journal::info(tr("Reading the remote for analysis…"));
    run([this] {
        // Чтение — по подключённому железу: модель документа на него не влияет (mainWindowRemote.cpp),
        // поэтому здесь остаётся путь auto, а не решение фабрики.
        transport::ConcordTransfer session;
        auto info = session.connect("auto");
        if (!info) { reportFailure(info.error()); return; }
        journal::info(tr("Reading %1: model %2, architecture %3, protocol %4, skin %5, transport %6")
            .arg(QString::fromStdString(info->remote.model), QString::fromStdString(info->modelId))
            .arg(info->remote.identity.architecture).arg(info->protocol).arg(info->remote.identity.skin)
            .arg(info->network ? QStringLiteral("USB network") : QStringLiteral("USB HID")));
        auto data = session.read(progressHandler());
        if (!data) { reportFailure(data.error()); return; }
        const auto capture = transport::makeCapture(*info, std::move(*data));
        const auto json = formats::writeRemoteCapture(capture);
        if (!json) { reportFailure(json.error()); return; }
        // Проверка по тем байтам, что уйдут в файл: так же файл разберёт получатель.
        const auto parsed = formats::parseRemoteCapture(*json);
        if (!parsed) { reportFailure(parsed.error()); return; }
        const auto check = transport::inspectCapture(*parsed);
        if (!check) { reportFailure(check.error()); return; }
        QStringList passed{tr("SHA-256 of the data"), tr("container checksum")};
        if (check->frame) passed << tr("image frame");
        const QString text = passed.join(QStringLiteral(", "));
        const QByteArray bytes(json->data(), static_cast<qsizetype>(json->size()));
        const QString modelId = QString::fromStdString(capture.modelId);
        QMetaObject::invokeMethod(this, [this, bytes, modelId, text] { emit captureReadFinished(bytes, modelId, text); },
                                  Qt::QueuedConnection);
    });
}

void RemoteSession::learn(int seconds) {
    if (busy()) {
        return;
    }
    journal::info(tr("Learning: waiting for a signal (%1 s)…").arg(seconds));
    run([this, seconds] {
        const auto choice = openLink();
        if (!choice) { reportFailure(choice.error()); return; }
        if (!choice->link) { reportFailure(transferText(), false); return; }
        auto& link = *choice->link;
        if (auto info = connectAndReport(link); !info) {
            reportFailure(info.error());
            return;
        }
        auto signal = link.learnIr(progressHandler(), std::chrono::seconds(seconds));
        if (!signal) {
            reportFailure(signal.error());
            return;
        }
        QMetaObject::invokeMethod(this, [this, value = *signal] { emit learnFinished(value); }, Qt::QueuedConnection);
    });
}

namespace {

/// Записывает резервную копию; false — не удалось.
bool saveBackup(const QString& file, const std::vector<std::uint8_t>& ezhex) {
    QSaveFile out(file);
    return out.open(QIODevice::WriteOnly) &&
           out.write(reinterpret_cast<const char*>(ezhex.data()), static_cast<qint64>(ezhex.size())) ==
               static_cast<qint64>(ezhex.size()) &&
           out.commit();
}

} // namespace

void RemoteSession::prepareWrite(QString backupFile) {
    if (busy()) {
        return;
    }
    journal::info(tr("Before writing: reading the remote's configuration for the backup…"));
    run([this, backupFile] {
        const auto choice = openLink();
        if (!choice) { reportFailure(choice.error()); return; }
        if (!choice->link) { reportFailure(transferText(), false); return; }
        auto& link = *choice->link;
        if (auto info = connectAndReport(link); !info) {
            reportFailure(labels::errorText(info.error()), false);
            return;
        }
        auto current = link.readConfig(progressHandler());
        if (!current) {
            reportFailure(tr("Could not save the backup, writing cancelled: %1").arg(labels::errorText(current.error())),
                 current.error().code == ErrorCode::Cancelled);
            return;
        }
        if (!saveBackup(backupFile, *current)) {
            reportFailure(tr("Could not save the backup to %1, writing cancelled").arg(backupFile), false);
            return;
        }
        const QByteArray bytes(reinterpret_cast<const char*>(current->data()),
                               static_cast<qsizetype>(current->size()));
        QMetaObject::invokeMethod(this, [this, bytes, backupFile] { emit backupSaved(bytes, backupFile); },
                                  Qt::QueuedConnection);
    });
}

void RemoteSession::writeConfig(std::vector<std::uint8_t> ezhex, QString backupFile) {
    if (busy()) {
        return;
    }
    journal::info(tr("Writing the configuration to the remote (%1 KB)…").arg((ezhex.size() + 1023) / 1024));
    run([this, ezhex = std::move(ezhex), backupFile] {
        const auto choice = openLink();
        if (!choice) { reportFailure(choice.error()); return; }
        if (!choice->link) { reportFailure(transferText(), false); return; }
        auto& link = *choice->link;
        if (auto info = connectAndReport(link); !info) {
            reportFailure(labels::errorText(info.error()), false);
            return;
        }

        // Резервная копия обязательна: без облака Logitech другого способа вернуть прежнее нет.
        // Пустой путь — её уже сохранил prepareWrite().
        if (!backupFile.isEmpty()) {
            auto current = link.readConfig(progressHandler());
            if (!current) {
                reportFailure(tr("Could not save the backup, writing cancelled: %1")
                         .arg(labels::errorText(current.error())),
                     current.error().code == ErrorCode::Cancelled);
                return;
            }
            if (!saveBackup(backupFile, *current)) {
                reportFailure(tr("Could not save the backup to %1, writing cancelled").arg(backupFile), false);
                return;
            }
        }

        auto written = link.writeConfig(ezhex, progressHandler());
        if (!written) {
            reportFailure(labels::errorText(written.error()), written.error().code == ErrorCode::Cancelled);
            return;
        }
        QStringList checksum;
        // Сумма обновления отвечает только связь по USB-сети; фабрика для модели 900 даёт именно её.
        if (const auto* net = dynamic_cast<transport::UsbNetLink*>(&link)) {
            for (const auto& param : net->updateChecksum()) {
                checksum << QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(param.data()),
                                                           static_cast<qsizetype>(param.size())).toHex());
            }
        }
        const auto payloadSize = static_cast<qint64>(ezhex.size());
        (void)connectAndReport(link); // обновить подпись в шапке после перезапуска
        QMetaObject::invokeMethod(this, [this, backupFile, checksum, payloadSize] {
            journal::debug(tr("Remote update checksum: %1 (EZHex %2 bytes)").arg(checksum.join(u' '), QString::number(payloadSize)));
            emit writeFinished(backupFile);
        }, Qt::QueuedConnection);
    });
}

} // namespace harmony::app
