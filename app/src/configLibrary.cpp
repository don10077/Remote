/**
 * @file configLibrary.cpp
 * @brief Хранение списка конфигураций в QSettings.
 */

#include "configLibrary.h"

#include "appSettings.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace harmony::app::library {

namespace {

constexpr const char* kRecentKey = "library/recent";
constexpr const char* kStartupKey = "library/startup";
constexpr const char* kLastDirKey = "library/lastDialogDir";

} // namespace

QString normalized(const QString& folder) {
    return folder.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
}

QStringList recent() {
    return QSettings().value(kRecentKey).toStringList();
}

void touch(const QString& folder) {
    const QString path = normalized(folder);
    QStringList list = recent();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > kMaxRecent) {
        list.removeLast();
    }
    QSettings().setValue(kRecentKey, list);
}

void forget(const QString& folder) {
    const QString path = normalized(folder);
    QStringList list = recent();
    list.removeAll(path);
    QSettings().setValue(kRecentKey, list);
    if (startupFolder() == path) {
        setStartupFolder({});
    }
}

QString startupFolder() {
    return QSettings().value(kStartupKey).toString();
}

void setStartupFolder(const QString& folder) {
    QSettings().setValue(kStartupKey, normalized(folder));
}

QString defaultLocation() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(QStringLiteral("Harmony"));
}

QString backupFolder() {
    const QString folder = settings::backupFolder(); // из настроек; по умолчанию — в «Документы/Harmony»
    QDir().mkpath(folder);
    return folder;
}

QString newBackupFile(const QString& prefix) {
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    return QDir(backupFolder()).filePath(prefix + "-" + stamp + ".EZHex");
}

QString lastDialogDir() {
    const QString dir = QSettings().value(kLastDirKey).toString();
    return dir.isEmpty() ? defaultLocation() : dir;
}

void setLastDialogDir(const QString& dir) {
    QSettings().setValue(kLastDirKey, dir);
}

} // namespace harmony::app::library
