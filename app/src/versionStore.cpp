/**
 * @file versionStore.cpp
 * @brief Версии конфигурации в папке программы.
 */

#include "versionStore.h"

#include "documentSource.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace harmony::app::versions {

namespace {

/// Имя файла версии: <время>-<вид>.EZHex.
constexpr const char* kTimeFormat = "yyyyMMdd-HHmmss-zzz";

QString tagOf(Kind kind) {
    switch (kind) {
    case Kind::Auto:
        return QStringLiteral("auto");
    case Kind::Saved:
        return QStringLiteral("saved");
    case Kind::Manual:
        return QStringLiteral("manual");
    case Kind::Restore:
        return QStringLiteral("restore");
    case Kind::Discarded:
        return QStringLiteral("discarded");
    }
    return QStringLiteral("auto");
}

Kind kindOf(const QString& tag) {
    if (tag == QLatin1String("saved")) {
        return Kind::Saved;
    }
    if (tag == QLatin1String("manual")) {
        return Kind::Manual;
    }
    if (tag == QLatin1String("restore")) {
        return Kind::Restore;
    }
    if (tag == QLatin1String("discarded")) {
        return Kind::Discarded;
    }
    return Kind::Auto;
}

/// Записывает .EZHex документа в файл версии.
Result<QString> write(const ConfigDocument& document, const QString& key, Kind kind) {
    auto data = document.toEzHex(false);
    if (!data) {
        return std::unexpected(std::move(data.error()));
    }
    const QString path =
        QDir(folder(key))
            .filePath(QStringLiteral("%1-%2.EZHex")
                          .arg(QDateTime::currentDateTime().toString(kTimeFormat), tagOf(kind)));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(reinterpret_cast<const char*>(data->data()),
                   static_cast<qint64>(data->size())) != static_cast<qint64>(data->size()) ||
        !file.commit()) {
        return makeError(ErrorCode::Io, "could not write the file: %1",
                         {QDir::toNativeSeparators(path).toStdString()});
    }
    prune(key);
    return path;
}

} // namespace

QString keyOf(const ConfigDocument& document) {
    return document.hasLocation()
               ? QFileInfo(fromPath(document.location())).absoluteFilePath()
               : QStringLiteral("unsaved:") + QString::fromStdString(document.name());
}

QString folder(const QString& key) {
    // Имя — для человека (без опасных символов), хеш пути различает одноимённые конфигурации.
    QString name = key.startsWith(QLatin1String("unsaved:"))
                       ? key.mid(8) + QStringLiteral("-unsaved")
                       : QFileInfo(key).completeBaseName();
    if (name.isEmpty()) {
        name = QStringLiteral("configuration");
    }
    static const QRegularExpression unsafe(QStringLiteral("[^\\w\\-. ]"));
    name.replace(unsafe, QStringLiteral("_"));
    // Хеш — свой (SHA-1), а не qHash: тот может поменяться с версией Qt, и папки «потерялись» бы.
    name += QLatin1Char('-') +
            QString::fromLatin1(
                QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                            .filePath(QStringLiteral("versions/") + name);
    QDir().mkpath(dir);
    return dir;
}

Result<QString> save(const ConfigDocument& document, const QString& key, Kind kind) {
    return write(document, key, kind);
}

Result<void> keepOnDisk(const ConfigDocument& document) {
    if (!document.hasLocation() || !std::filesystem::exists(document.location())) {
        return {};
    }
    auto onDisk = ConfigDocument::open(document.location());
    if (!onDisk) {
        return std::unexpected(std::move(onDisk.error()));
    }
    auto saved = write(*onDisk, keyOf(document), Kind::Saved);
    if (!saved) {
        return std::unexpected(std::move(saved.error()));
    }
    return {};
}

QList<Version> list(const QString& key) {
    QList<Version> out;
    static const QRegularExpression pattern(
        QStringLiteral("^(\\d{8}-\\d{6}-\\d{3})-(\\w+)\\.EZHex$"));
    const QDir dir(folder(key));
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.EZHex")}, QDir::Files)) {
        const auto match = pattern.match(info.fileName());
        if (!match.hasMatch()) {
            continue;
        }
        out.append({info.absoluteFilePath(), QDateTime::fromString(match.captured(1), kTimeFormat),
                    kindOf(match.captured(2)), info.size()});
    }
    std::ranges::sort(out, [](const Version& a, const Version& b) { return a.time > b.time; });
    return out;
}

Result<ConfigDocument> open(const Version& version, const std::string& name) {
    QFile file(version.path);
    if (!file.open(QIODevice::ReadOnly)) {
        return makeError(ErrorCode::NotFound, "file not found: %1",
                         {QDir::toNativeSeparators(version.path).toStdString()});
    }
    const QByteArray data = file.readAll();
    return ConfigDocument::fromEzHex(
        std::span(reinterpret_cast<const std::uint8_t*>(data.constData()),
                  static_cast<std::size_t>(data.size())),
        name);
}

void prune(const QString& key) {
    const auto versions = list(key);
    for (qsizetype i = kKeep; i < versions.size(); ++i) {
        QFile::remove(versions[i].path);
    }
}

QString kindName(Kind kind) {
    switch (kind) {
    case Kind::Auto:
        return QCoreApplication::translate("versions", "autosave");
    case Kind::Saved:
        return QCoreApplication::translate("versions", "before saving");
    case Kind::Manual:
        return QCoreApplication::translate("versions", "saved by hand");
    case Kind::Restore:
        return QCoreApplication::translate("versions", "before restoring");
    case Kind::Discarded:
        return QCoreApplication::translate("versions", "closed without saving");
    }
    return {};
}

} // namespace harmony::app::versions
