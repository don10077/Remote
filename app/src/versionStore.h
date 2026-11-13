#pragma once

/**
 * @file versionStore.h
 * @brief Версии конфигурации: копии в папке программы, чтобы вернуться к прежнему состоянию.
 *
 * У каждой конфигурации своя папка версий (по имени и пути; несохранённая —
 * по имени). Версия — файл .EZHex: в нём всё, включая ИК-таблицы. Виды:
 * - автосохранение — несохранённые правки раз в несколько минут;
 * - перед сохранением — что было в файле до перезаписи;
 * - вручную — «Сохранить версию сейчас»;
 * - перед восстановлением — состояние, которое заменила восстановленная версия;
 * - отменённые правки — закрыли без сохранения (на случай, если передумаете).
 * Хранятся последние kKeep версий.
 *
 * Папка: QStandardPaths::AppDataLocation/versions/<имя>-<8 знаков SHA-1 пути>.
 */

#include "harmony/core/configDocument.h"

#include <QDateTime>
#include <QList>
#include <QString>

namespace harmony::app::versions {

/// Сколько версий одной конфигурации хранится.
inline constexpr int kKeep = 40;

/// Вид версии.
enum class Kind {
    Auto,      ///< Автосохранение
    Saved,     ///< Файл перед перезаписью
    Manual,    ///< Вручную
    Restore,   ///< Перед восстановлением другой версии
    Discarded, ///< Правки, от которых отказались при закрытии
};

/// Одна версия.
struct Version {
    QString path;   ///< Файл версии (.EZHex)
    QDateTime time; ///< Когда сохранена
    Kind kind = Kind::Auto;
    qint64 size = 0; ///< Размер, байт
};

/**
 * @brief Ключ конфигурации для папки версий.
 * @return Путь на диске; у несохранённой — «unsaved:<имя>».
 */
[[nodiscard]] QString keyOf(const ConfigDocument& document);

/// Папка версий конфигурации (создаётся при необходимости).
[[nodiscard]] QString folder(const QString& key);

/**
 * @brief Сохраняет документ как версию.
 * @return Путь файла версии; ошибка — почему не сохранилась.
 */
[[nodiscard]] Result<QString> save(const ConfigDocument& document, const QString& key, Kind kind);

/**
 * @brief Сохраняет как версию то, что сейчас лежит на диске по пути документа (перед перезаписью).
 * @return Пусто — файла нет (нечего хранить) или сохранено; ошибка — не удалось.
 */
[[nodiscard]] Result<void> keepOnDisk(const ConfigDocument& document);

/// Версии конфигурации, новые первыми.
[[nodiscard]] QList<Version> list(const QString& key);

/// Открывает версию как документ.
[[nodiscard]] Result<ConfigDocument> open(const Version& version, const std::string& name);

/// Оставляет kKeep последних версий.
void prune(const QString& key);

/// @return Название вида на языке интерфейса.
[[nodiscard]] QString kindName(Kind kind);

} // namespace harmony::app::versions
