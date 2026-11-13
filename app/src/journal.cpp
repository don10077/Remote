/**
 * @file journal.cpp
 * @brief Журнал программы: память, файл, уровень, сообщения Qt.
 */

#include "journal.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QRecursiveMutex>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <atomic>
#include <deque>

namespace harmony::app::journal {

namespace {

constexpr auto kLevelKey = "settings/journalLevel";
constexpr qint64 kMaxFileSize = 1024 * 1024; ///< При этом размере файл уходит в harmony.log.1

QRecursiveMutex mutex; ///< Рекурсивный: запись в файл сама может вызвать сообщение Qt
std::deque<Entry> memory;
/// Уровень — в памяти (настройки читаются один раз): write() зовут часто и из потоков.
std::atomic<int> currentLevel{-1};

/// Буква уровня в файле.
char letter(Level value) {
    switch (value) {
    case Level::Error: return 'E';
    case Level::Warning: return 'W';
    case Level::Info: return 'I';
    case Level::Debug: return 'D';
    }
    return '?';
}

/// Дописывает запись в файл (с переносом старого при переполнении); вызывается под mutex.
void appendToFile(const Entry& entry) {
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (QFileInfo(path).size() > kMaxFileSize) {
        QFile::remove(path + ".1");
        QFile::rename(path, path + ".1");
    }
    QFile file(path);
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        return; // журнал — не повод мешать работе
    }
    const QString line = entry.time.toString(Qt::ISODateWithMs) + ' ' + letter(entry.level) + ' ' +
                         QString(entry.text).replace('\n', QStringLiteral("\n    ")) + '\n';
    file.write(line.toUtf8());
}

/// Сообщения Qt → журнал (предупреждения Qt — подробности, а не ошибки программы).
void qtHandler(QtMsgType type, const QMessageLogContext&, const QString& message) {
    switch (type) {
    case QtFatalMsg:
    case QtCriticalMsg: write(Level::Error, QStringLiteral("Qt: ") + message); break;
    case QtWarningMsg: write(Level::Debug, QStringLiteral("Qt: ") + message); break;
    default: write(Level::Debug, QStringLiteral("Qt: ") + message); break;
    }
}

} // namespace

Notifier* notifier() {
    static Notifier* instance = new Notifier; // живёт до конца программы
    return instance;
}

Level level() {
    int value = currentLevel.load();
    if (value < 0) {
        value = std::clamp(QSettings().value(kLevelKey, static_cast<int>(Level::Info)).toInt(), 0,
                           static_cast<int>(Level::Debug));
        currentLevel = value;
    }
    return static_cast<Level>(value);
}

void setLevel(Level value) {
    currentLevel = static_cast<int>(value);
    QSettings().setValue(kLevelKey, static_cast<int>(value));
}

void write(Level value, const QString& text) {
    if (value != Level::Error && static_cast<int>(value) > static_cast<int>(level())) {
        return;
    }
    const Entry entry{QDateTime::currentDateTime(), value, text};
    {
        const QMutexLocker<QRecursiveMutex> locker(&mutex);
        memory.push_back(entry);
        while (memory.size() > static_cast<std::size_t>(kKeep)) {
            memory.pop_front();
        }
        appendToFile(entry);
    }
    // Окно журнала — в главном потоке.
    QMetaObject::invokeMethod(notifier(), [entry] { emit notifier()->added(entry); }, Qt::QueuedConnection);
}

QList<Entry> entries() {
    const QMutexLocker<QRecursiveMutex> locker(&mutex);
    return {memory.begin(), memory.end()};
}

void clear() {
    {
        const QMutexLocker<QRecursiveMutex> locker(&mutex);
        memory.clear();
    }
    emit notifier()->cleared();
}

QString filePath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("harmony.log");
}

QString levelName(Level value) {
    switch (value) {
    case Level::Error: return QCoreApplication::translate("journal", "Errors only");
    case Level::Warning: return QCoreApplication::translate("journal", "Errors and warnings");
    case Level::Info: return QCoreApplication::translate("journal", "Actions, warnings and errors");
    case Level::Debug: return QCoreApplication::translate("journal", "Everything, with details");
    }
    return {};
}

void captureQtMessages() {
    qInstallMessageHandler(qtHandler);
}

} // namespace harmony::app::journal
