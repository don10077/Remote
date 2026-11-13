#pragma once

/**
 * @file i18n.h
 * @brief Языки интерфейса: английский (исходный), русский и свои файлы переводов.
 *
 * Строки интерфейса пишутся по-английски в tr(); русский перевод встроен
 * в программу (app/resources/i18n/harmony_ru.ts, формат Qt Linguist).
 *
 * Свой перевод — файл harmony_<код>.ts (например, harmony_de.ts или
 * harmony_pt_BR.ts) в одной из папок translationFolders(). Файл для уже
 * встроенного языка дополняет и правит встроенный перевод; непереведённое
 * остаётся по-английски. Программа читает .ts сама: ни lrelease, ни
 * пересборка не нужны. Перевод ищется по исходной строке, без учёта
 * контекста. Заготовку перевода со всеми строками даёт writeTemplate().
 */

#include <QList>
#include <QString>
#include <QStringList>

class QApplication;

namespace harmony::app::i18n {

/// Язык интерфейса.
struct Language {
    QString code; ///< «ru», «en», «de», «pt_BR»
    QString name; ///< Название на самом языке: «Русский», «English», «Deutsch»
    bool external = false; ///< Есть свой файл перевода (а не только встроенный)
};

/// @return Папки своих переводов: рядом с программой и «Документы/Harmony/translations».
[[nodiscard]] QStringList translationFolders();

/// @return Папка своих переводов пользователя («Документы/Harmony/translations»).
[[nodiscard]] QString userTranslationFolder();

/// @return Доступные языки: русский, встроенные переводы и найденные файлы.
[[nodiscard]] QList<Language> languages();

/**
 * @brief Включает язык интерфейса (до создания окон).
 *
 * Подключает перевод программы (встроенный и свой файл поверх него) и
 * стандартных кнопок Qt («OK», «Отмена»), если они установлены.
 * @param code Код языка; перевода нет — английский.
 */
void install(QApplication& app, const QString& code);

/// @return Текущий код языка.
[[nodiscard]] QString current();

/**
 * @brief Записывает заготовку перевода: все строки программы с пустыми переводами.
 *
 * @param file Путь к .ts (имя — harmony_<код>.ts).
 * @param code Код языка перевода (записывается в файл).
 * @return false — не удалось записать.
 */
bool writeTemplate(const QString& file, const QString& code);

} // namespace harmony::app::i18n
