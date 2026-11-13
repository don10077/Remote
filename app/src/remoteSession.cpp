/**
 * @file remoteSession.cpp
 * @brief Операции с пультом в фоновом потоке.
 */

#include "remoteSession.h"

#include "labels.h"
#include "appSettings.h"
#include "journal.h"

#include "harmony/transport/harmony900Link.h"
#include "harmony/transport/harmonyHidReader.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QMetaObject>
#include <QSaveFile>
#include <QThread>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

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

Result<transport::RemoteInfo> RemoteSession::connectAndReport(transport::RemoteLink& link) {
    // Адрес и порт — из настроек (обычно 169.254.1.2:3074).
    const transport::Endpoint endpoint{.host = settings::remoteHost().toStdString(),
                                       .port = static_cast<std::uint16_t>(settings::remotePort())};
    auto info = link.connect(endpoint);
    const bool connected = info.has_value();
    const QString text = connected ? describe(*info) : tr("Remote not connected");
    QMetaObject::invokeMethod(this, [this, connected, text] { emit stateChanged(connected, text); },
                              Qt::QueuedConnection);
    return info;
}

void RemoteSession::probe() {
    if (busy()) {
        return;
    }
    probing_ = true; // сбрасывается по окончании потока
    run([this] {
        transport::Harmony900Link link({}, {.connect = std::chrono::milliseconds(1500)});
        (void)connectAndReport(link);
    });
}

void RemoteSession::readConfig() {
    if (busy()) {
        return;
    }
    journal::info(tr("Reading the configuration from the remote…"));
    run([this] {
        transport::Harmony900Link link;
        auto fail = [this](const Error& error) {
            const QString message = labels::errorText(error);
            const bool cancelled = error.code == ErrorCode::Cancelled;
            QMetaObject::invokeMethod(this, [this, message, cancelled] { emit failed(message, cancelled); },
                                      Qt::QueuedConnection);
        };
        if (auto info = connectAndReport(link); !info) {
            fail(info.error());
            return;
        }
        auto ezhex = link.readConfig(progressHandler());
        if (!ezhex) {
            fail(ezhex.error());
            return;
        }
        const QByteArray bytes(reinterpret_cast<const char*>(ezhex->data()), static_cast<qsizetype>(ezhex->size()));
        QMetaObject::invokeMethod(this, [this, bytes] { emit readFinished(bytes); }, Qt::QueuedConnection);
    });
}

void RemoteSession::readHidConfig() {
    if (busy()) return;
    journal::info(tr("Reading the configuration over USB HID…"));
    run([this] {
        auto fail = [this](const Error& error) {
            const QString message = labels::errorText(error);
            const bool cancelled = error.code == ErrorCode::Cancelled;
            QMetaObject::invokeMethod(this, [this, message, cancelled] { emit failed(message, cancelled); }, Qt::QueuedConnection);
        };
        auto channel = transport::openHarmonyHid();
        if (!channel) { fail(channel.error()); return; }
        transport::HarmonyHidReader reader(**channel);
        auto result = reader.read(progressHandler());
        if (!result) { fail(result.error()); return; }
        const QByteArray raw(reinterpret_cast<const char*>(result->config.data()), static_cast<qsizetype>(result->config.size()));
        const QByteArray identity(reinterpret_cast<const char*>(result->identityReport.data()), static_cast<qsizetype>(result->identityReport.size()));
        const auto& info = result->info;
        QJsonObject object{
            {"format", "harmony-hid-backup"}, {"version", 1},
            {"createdUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
            {"model", QString::fromStdString(info.model)},
            {"architecture", info.identity.architecture}, {"skin", info.identity.skin},
            {"firmware", QString::fromStdString(info.firmwareVersion)},
            {"flash", QString::fromStdString(info.identity.flash)},
            {"board", QString::fromStdString(info.identity.board)},
            {"identityHex", QString::fromLatin1(identity.toHex())},
            {"configBase", static_cast<qint64>(result->configBase)}, {"configSize", static_cast<qint64>(raw.size())},
            {"configSha256", QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex())},
            {"configBase64", QString::fromLatin1(raw.toBase64())}
        };
        const QByteArray backup = QJsonDocument(object).toJson();
        QMetaObject::invokeMethod(this, [this, backup] { emit hidReadFinished(backup); }, Qt::QueuedConnection);
    });
}

void RemoteSession::learn(int seconds) {
    if (busy()) {
        return;
    }
    journal::info(tr("Learning: waiting for a signal (%1 s)…").arg(seconds));
    run([this, seconds] {
        transport::Harmony900Link link;
        auto fail = [this](const Error& error) {
            const QString message = labels::errorText(error);
            const bool cancelled = error.code == ErrorCode::Cancelled;
            QMetaObject::invokeMethod(this, [this, message, cancelled] { emit failed(message, cancelled); },
                                      Qt::QueuedConnection);
        };
        if (auto info = connectAndReport(link); !info) {
            fail(info.error());
            return;
        }
        auto signal = link.learnIr(progressHandler(), std::chrono::seconds(seconds));
        if (!signal) {
            fail(signal.error());
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
        transport::Harmony900Link link;
        auto fail = [this](const QString& message, bool cancelled) {
            QMetaObject::invokeMethod(this, [this, message, cancelled] { emit failed(message, cancelled); },
                                      Qt::QueuedConnection);
        };
        if (auto info = connectAndReport(link); !info) {
            fail(labels::errorText(info.error()), false);
            return;
        }
        auto current = link.readConfig(progressHandler());
        if (!current) {
            fail(tr("Could not save the backup, writing cancelled: %1").arg(labels::errorText(current.error())),
                 current.error().code == ErrorCode::Cancelled);
            return;
        }
        if (!saveBackup(backupFile, *current)) {
            fail(tr("Could not save the backup to %1, writing cancelled").arg(backupFile), false);
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
        transport::Harmony900Link link;
        auto fail = [this](const QString& message, bool cancelled) {
            QMetaObject::invokeMethod(this, [this, message, cancelled] { emit failed(message, cancelled); },
                                      Qt::QueuedConnection);
        };
        if (auto info = connectAndReport(link); !info) {
            fail(labels::errorText(info.error()), false);
            return;
        }

        // Резервная копия обязательна: без облака Logitech другого способа вернуть прежнее нет.
        // Пустой путь — её уже сохранил prepareWrite().
        if (!backupFile.isEmpty()) {
            auto current = link.readConfig(progressHandler());
            if (!current) {
                fail(tr("Could not save the backup, writing cancelled: %1")
                         .arg(labels::errorText(current.error())),
                     current.error().code == ErrorCode::Cancelled);
                return;
            }
            if (!saveBackup(backupFile, *current)) {
                fail(tr("Could not save the backup to %1, writing cancelled").arg(backupFile), false);
                return;
            }
        }

        auto written = link.writeConfig(ezhex, progressHandler());
        if (!written) {
            fail(labels::errorText(written.error()), written.error().code == ErrorCode::Cancelled);
            return;
        }
        QStringList checksum;
        for (const auto& param : link.updateChecksum()) {
            checksum << QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(param.data()),
                                                       static_cast<qsizetype>(param.size())).toHex());
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
