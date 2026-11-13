/**
 * @file irDatabaseService.cpp
 * @brief Открытие, скачивание и сборка базы ИК-кодов.
 */

#include "irDatabaseService.h"

#include "appSettings.h"
#include "documentSource.h"
#include "labels.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QUrl>
#include <algorithm>

namespace harmony::app {

IrDatabaseService::IrDatabaseService(QObject* parent) : QObject(parent) {}

IrDatabaseService::~IrDatabaseService() {
    cancelled_ = true;
    if (thread_ != nullptr) {
        thread_->wait(); // поток сборки обращается к этому объекту
    }
}

const irdb::IrDatabase* IrDatabaseService::database() {
    if (!opened_) {
        opened_ = true;
        error_.clear();
        auto database = irdb::IrDatabase::open(toPath(settings::irdbFile()));
        if (database) {
            database_ = std::make_unique<irdb::IrDatabase>(std::move(*database));
        } else {
            database_.reset();
            error_ = labels::errorText(database.error());
        }
    }
    return database_.get();
}

void IrDatabaseService::reload() {
    database_.reset();
    opened_ = false;
    emit changed();
}

IrDatabaseService::Archive IrDatabaseService::archive(settings::IrdbArchive kind,
                                                      const QString& url) {
    QString name = QUrl(url.trimmed()).fileName();
    if (name.isEmpty()) {
        name = QUrl(settings::defaultIrdbUrl(kind)).fileName();
    }
    return {kind, url.trimmed(), name};
}

QList<IrDatabaseService::Archive> IrDatabaseService::archives() {
    return {
        archive(settings::IrdbArchive::Devices, settings::irdbUrl(settings::IrdbArchive::Devices)),
        archive(settings::IrdbArchive::Protocols,
                settings::irdbUrl(settings::IrdbArchive::Protocols))};
}

QString IrDatabaseService::findArchive(const QString& folder, const Archive& archive) {
    // Сжатый файл предпочтительнее: читать с диска в 18 раз меньше.
    const QDir dir(folder);
    const QString& name = archive.name;
    if (QFileInfo::exists(dir.filePath(name))) {
        return dir.filePath(name);
    }
    if (name.endsWith(QLatin1String(".gz")) && QFileInfo::exists(dir.filePath(name.chopped(3)))) {
        return dir.filePath(name.chopped(3));
    }
    return {};
}

bool IrDatabaseService::findSources(const QString& folder, QString& devices, QString& protocols) {
    const auto list = archives();
    devices = findArchive(folder, list[0]);
    protocols = findArchive(folder, list[1]);
    return !devices.isEmpty() && !protocols.isEmpty();
}

void IrDatabaseService::build() {
    if (busy_) {
        return;
    }
    QString devices;
    QString protocols;
    if (!findSources(settings::irdbSourceFolder(), devices, protocols)) {
        const auto list = archives();
        emit finished(false,
                      tr("No archive files in the folder %1: %2 and %3 are needed (compressed or "
                         "unpacked).")
                          .arg(QDir::toNativeSeparators(settings::irdbSourceFolder()), list[0].name,
                               list[1].name));
        return;
    }
    busy_ = true;
    cancelled_ = false;
    const QString output = settings::irdbFile();
    QDir().mkpath(QFileInfo(output).absolutePath());
    // База открыта — закрыть: файл будет заменён.
    database_.reset();
    opened_ = false;
    const std::string source = settings::irdbUrl(settings::IrdbArchive::Devices).toStdString();
    thread_ = QThread::create([this, devices, protocols, output, source] {
        const QString stage = tr("Indexing…");
        auto result = irdb::buildDatabase(
            toPath(devices), toPath(protocols), toPath(output), source,
            [this, stage](std::uint64_t done, std::uint64_t total) {
                QMetaObject::invokeMethod(
                    this,
                    [this, stage, done, total] {
                        emit progress(stage, static_cast<qint64>(done), static_cast<qint64>(total));
                    },
                    Qt::QueuedConnection);
                return !cancelled_.load();
            });
        const bool ok = result.has_value();
        const QString message =
            ok ? tr("Indexed: %1, %2. Skipped records: %3.")
                     .arg(labels::plural(static_cast<qsizetype>(result->devices), "device",
                                         "devices"),
                          labels::plural(static_cast<qsizetype>(result->commands), "command",
                                         "commands")).arg(result->skippedLines)
               : labels::errorText(result.error());
        QMetaObject::invokeMethod(
            this,
            [this, ok, message] {
                thread_->wait();
                thread_->deleteLater();
                thread_ = nullptr;
                busy_ = false;
                emit changed();
                emit finished(ok, message);
            },
            Qt::QueuedConnection);
    });
    thread_->start();
}

void IrDatabaseService::download(bool all) {
    if (busy_) {
        return;
    }
    const QString folder = settings::irdbSourceFolder();
    if (!QDir().mkpath(folder)) {
        emit finished(false,
                      tr("Could not create the folder: %1").arg(QDir::toNativeSeparators(folder)));
        return;
    }
    // Протоколы — первыми: маленький файл, сразу видно, что адрес верный.
    queue_.clear();
    auto list = archives();
    std::reverse(list.begin(), list.end());
    for (const Archive& item : list) {
        if (all || findArchive(folder, item).isEmpty()) {
            queue_ << item;
        }
    }
    if (network_ == nullptr) {
        network_ = new QNetworkAccessManager(this);
    }
    busy_ = true;
    cancelled_ = false;
    downloaded_ = 0;
    downloadNext();
}

void IrDatabaseService::downloadNext() {
    if (cancelled_) {
        fail(tr("Cancelled."));
        return;
    }
    if (queue_.isEmpty()) {
        busy_ = false;
        emit finished(true, downloaded_ > 0 ? tr("Downloaded. Now press “Index”.")
                                            : tr("All files are already in the folder."));
        return;
    }
    const Archive item = queue_.takeFirst();
    const QString name = item.name;
    const QString target = QDir(settings::irdbSourceFolder()).filePath(name);
    download_ = new QSaveFile(target, this);
    if (!download_->open(QIODevice::WriteOnly)) {
        fail(tr("Could not create the file: %1")
                 .arg(QDir::toNativeSeparators(download_->fileName())));
        return;
    }
    QNetworkRequest request{QUrl(item.url)};
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    reply_ = network_->get(request);
    const QString stage = tr("Downloading %1…").arg(name);
    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        if (download_ != nullptr && reply_ != nullptr) {
            const auto data = reply_->readAll();
            if (download_->write(data) != data.size()) {
                reply_->abort();
            }
        }
    });
    connect(reply_, &QNetworkReply::downloadProgress, this,
            [this, stage](qint64 done, qint64 total) { emit progress(stage, done, total); });
    connect(reply_, &QNetworkReply::finished, this, [this, target] {
        QNetworkReply* reply = reply_;
        reply_ = nullptr;
        reply->deleteLater();
        const auto data = reply->readAll();
        const bool written = download_->write(data) == data.size();
        const bool committed = reply->error() == QNetworkReply::NoError && written && download_->commit();
        if (!committed) {
            download_->cancelWriting();
        }
        download_->deleteLater();
        download_ = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            fail(cancelled_ ? tr("Cancelled.")
                            : tr("Download failed: %1 (%2)")
                                  .arg(reply->errorString(), reply->url().toString()));
            return;
        }
        if (!committed) {
            fail(tr("Could not create the file: %1").arg(QDir::toNativeSeparators(target)));
            return;
        }
        ++downloaded_;
        downloadNext();
    });
}

void IrDatabaseService::cancel() {
    cancelled_ = true;
    if (reply_ != nullptr) {
        reply_->abort();
    }
}

void IrDatabaseService::fail(const QString& message) {
    busy_ = false;
    emit finished(false, message);
}

} // namespace harmony::app
