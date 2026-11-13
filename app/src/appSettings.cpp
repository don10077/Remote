/**
 * @file appSettings.cpp
 * @brief Настройки программы в QSettings.
 */

#include "appSettings.h"

#include "harmony/transport/remoteLink.h"

#include <QDir>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>

namespace harmony::app::settings {

namespace {

constexpr auto kLanguage = "settings/language";
constexpr auto kStartup = "settings/startup";
constexpr auto kBackupFolder = "settings/backupFolder";
constexpr auto kRemoteHost = "settings/remoteHost";
constexpr auto kRemotePort = "settings/remotePort";
constexpr auto kAutoProbe = "settings/autoProbe";
constexpr auto kLearnTimeout = "settings/learnTimeout";
constexpr auto kTheme = "settings/theme";
constexpr auto kIrdbDevicesUrl = "irdb/devicesUrl";
constexpr auto kIrdbProtocolsUrl = "irdb/protocolsUrl";
constexpr auto kIrdbSourceFolder = "irdb/sourceFolder";
constexpr auto kIrdbFile = "irdb/file";
constexpr auto kDisclaimer = "settings/acceptedDisclaimer"; // сброс настроек его не трогает

} // namespace

QString language() {
    const QString stored = QSettings().value(kLanguage).toString();
    if (!stored.isEmpty()) {
        return stored; // в том числе язык из своего файла перевода
    }
    // По умолчанию — язык системы: русский для русско- и близкоязычных, иначе английский.
    switch (QLocale::system().language()) {
    case QLocale::Russian:
    case QLocale::Ukrainian:
    case QLocale::Belarusian:
    case QLocale::Kazakh:
        return QStringLiteral("ru");
    default:
        return QStringLiteral("en");
    }
}

void setLanguage(const QString& code) {
    QSettings().setValue(kLanguage, code);
}

Startup startup() {
    return QSettings().value(kStartup).toString() == "last" ? Startup::Last : Startup::Choose;
}

void setStartup(Startup value) {
    QSettings().setValue(kStartup, value == Startup::Last ? "last" : "choose");
}

QString defaultBackupFolder() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(QStringLiteral("Harmony/BackUp")); // путь от языка не зависит
}

QString backupFolder() {
    const QString folder = QSettings().value(kBackupFolder).toString();
    return folder.isEmpty() ? defaultBackupFolder() : folder;
}

void setBackupFolder(const QString& folder) {
    if (folder.isEmpty() || QDir::cleanPath(folder) == QDir::cleanPath(defaultBackupFolder())) {
        QSettings().remove(kBackupFolder);
    } else {
        QSettings().setValue(kBackupFolder, QDir::cleanPath(folder));
    }
}

QString remoteHost() {
    const QString host = QSettings().value(kRemoteHost).toString().trimmed();
    return host.isEmpty() ? QString::fromUtf8(transport::kDefaultRemoteHost) : host;
}

void setRemoteHost(const QString& host) {
    QSettings().setValue(kRemoteHost, host.trimmed());
}

int remotePort() {
    const int port = QSettings().value(kRemotePort, 0).toInt();
    return port > 0 && port < 65536 ? port : transport::Endpoint{}.port;
}

void setRemotePort(int port) {
    QSettings().setValue(kRemotePort, port);
}

bool autoProbe() {
    return QSettings().value(kAutoProbe, true).toBool();
}

void setAutoProbe(bool on) {
    QSettings().setValue(kAutoProbe, on);
}

int learnTimeout() {
    const int seconds = QSettings().value(kLearnTimeout, 30).toInt();
    return seconds >= 5 && seconds <= 120 ? seconds : 30;
}

void setLearnTimeout(int seconds) {
    QSettings().setValue(kLearnTimeout, seconds);
}

ThemeMode themeMode() {
    const QString stored = QSettings().value(kTheme).toString();
    return stored == "light" ? ThemeMode::Light : stored == "dark" ? ThemeMode::Dark : ThemeMode::System;
}

void setThemeMode(ThemeMode mode) {
    QSettings().setValue(kTheme, mode == ThemeMode::Light ? "light" : mode == ThemeMode::Dark ? "dark" : "system");
}

namespace {

/// Релиз архива базы Logitech, собранного сообществом (тот же, что у harmony900tools).
constexpr auto kIrdbRelease =
    "https://github.com/pickysysadmin/logitech-harmony-ir-archive/releases/download/"
    "raw-2026-09-02/";

const char* archiveName(IrdbArchive archive) {
    return archive == IrdbArchive::Devices ? "device_features_raw.jsonl.gz"
                                           : "AllProtocols.json.gz";
}

const char* archiveKey(IrdbArchive archive) {
    return archive == IrdbArchive::Devices ? kIrdbDevicesUrl : kIrdbProtocolsUrl;
}

} // namespace

QString defaultIrdbUrl(IrdbArchive archive) {
    return QString::fromLatin1(kIrdbRelease) + QString::fromLatin1(archiveName(archive));
}

QString irdbUrl(IrdbArchive archive) {
    QSettings settings;
    const QString url = settings.value(archiveKey(archive)).toString().trimmed();
    if (!url.isEmpty()) {
        return url;
    }
    return defaultIrdbUrl(archive);
}

void setIrdbUrl(IrdbArchive archive, const QString& url) {
    QSettings settings;
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty() || trimmed == defaultIrdbUrl(archive)) {
        settings.remove(archiveKey(archive));
    } else {
        settings.setValue(archiveKey(archive), trimmed);
    }
}

QString defaultIrdbFolder() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(QStringLiteral("Harmony/IRDatabase"));
}

QString irdbSourceFolder() {
    const QString folder = QSettings().value(kIrdbSourceFolder).toString();
    return folder.isEmpty() ? defaultIrdbFolder() : folder;
}

void setIrdbSourceFolder(const QString& folder) {
    if (folder.isEmpty() || QDir::cleanPath(folder) == QDir::cleanPath(defaultIrdbFolder())) {
        QSettings().remove(kIrdbSourceFolder);
    } else {
        QSettings().setValue(kIrdbSourceFolder, QDir::cleanPath(folder));
    }
}

QString irdbFile() {
    const QString file = QSettings().value(kIrdbFile).toString();
    return file.isEmpty() ? QDir(defaultIrdbFolder()).filePath(QStringLiteral("harmony.irdb"))
                          : file;
}

void setIrdbFile(const QString& file) {
    const QString fallback = QDir(defaultIrdbFolder()).filePath(QStringLiteral("harmony.irdb"));
    if (file.isEmpty() || QDir::cleanPath(file) == QDir::cleanPath(fallback)) {
        QSettings().remove(kIrdbFile);
    } else {
        QSettings().setValue(kIrdbFile, QDir::cleanPath(file));
    }
}

int acceptedDisclaimer() {
    return QSettings().value(kDisclaimer, 0).toInt();
}

void setAcceptedDisclaimer(int version) {
    QSettings().setValue(kDisclaimer, version);
}

void reset() {
    QSettings settings;
    for (const char* key : {kLanguage, kStartup, kTheme, kBackupFolder, kRemoteHost, kRemotePort,
                            kAutoProbe, kLearnTimeout, "settings/remoteResources"}) {
        settings.remove(key);
    }
}

} // namespace harmony::app::settings
