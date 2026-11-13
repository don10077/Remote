#pragma once

/**
 * @file journal.h
 * @brief Журнал программы: что делали с пультом и конфигурацией, ошибки.
 *
 * Записи хранятся в памяти (последние kKeep) и в файле harmony.log в папке
 * данных программы (при 1 МБ прежний файл становится harmony.log.1). Писать
 * можно из любого потока. Сколько подробностей записывать — уровень в
 * настройках; ошибки пишутся всегда.
 */

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

namespace harmony::app::journal {

/// Важность записи (по возрастанию подробности).
enum class Level {
    Error,   ///< Ошибки: не удалось открыть, записать, связаться
    Warning, ///< Предупреждения: то, что стоит проверить
    Info,    ///< Действия: открытие, сохранение, правки, обмен с пультом
    Debug,   ///< Подробности для разбора проблем (сообщения Qt и т. п.)
};

/// Запись журнала.
struct Entry {
    QDateTime time;
    Level level = Level::Info;
    QString text;
};

/// Сколько записей держать в памяти.
inline constexpr int kKeep = 2000;

/// Сообщает окну журнала о новых записях (в главном потоке).
class Notifier final : public QObject {
    Q_OBJECT

signals:
    /// Добавлена запись.
    void added(const harmony::app::journal::Entry& entry);
    /// Журнал очищен.
    void cleared();
};

/// @return Объект для подписки на новые записи.
Notifier* notifier();

/**
 * @brief Записывает в журнал, если @p level не подробнее уровня из настроек.
 * @param level Важность.
 * @param text Текст (одна строка или несколько).
 */
void write(Level level, const QString& text);

inline void error(const QString& text) { write(Level::Error, text); }
inline void warning(const QString& text) { write(Level::Warning, text); }
inline void info(const QString& text) { write(Level::Info, text); }
inline void debug(const QString& text) { write(Level::Debug, text); }

/// @return Записи в памяти, старые — первыми.
[[nodiscard]] QList<Entry> entries();

/// Очищает журнал в памяти (файл остаётся).
void clear();

/// @return Файл журнала.
[[nodiscard]] QString filePath();

/// @return Уровень: что записывать (из настроек, по умолчанию Info).
[[nodiscard]] Level level();
void setLevel(Level value);

/// @return Что записывается на этом уровне («Errors only», «Actions, warnings and errors»…).
[[nodiscard]] QString levelName(Level value);

/// Перехватывает сообщения Qt (qWarning и т. п.) в журнал; вызывать после QApplication.
void captureQtMessages();

} // namespace harmony::app::journal
