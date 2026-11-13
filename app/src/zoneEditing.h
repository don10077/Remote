#pragma once

/**
 * @file zoneEditing.h
 * @brief Логика редактора зон без виджетов: проверка зон и точечная правка текста
 *        zones.json, profile.json и layouts.json.
 *
 * Слои разметки: «корпус» — zones.json модели в пикселях фото, «экран» — зоны раскладки
 * из layouts.json в единицах `ref`. Правка заменяет в файле только изменённый фрагмент
 * (массив зон, значение ключа), а неизменённые зоны переносит исходным текстом: дифф
 * остаётся минимальным, форматирование и порядок ключей остального файла не трогаются.
 */

#include <QByteArray>
#include <QJsonArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <optional>

namespace harmony::app::zoneEditing {
/**
 * @brief Сохранить набор файлов с проверкой конфликтов, подготовкой и откатом при сбое.
 * @param root Папка ресурсов.
 * @param expected Содержимое прочитанных файлов; любое внешнее изменение отменяет запись.
 * @param updates Новое содержимое изменённых файлов (пути относительно root).
 * @return Пусто при успехе, иначе сообщение; при ошибке отката указан путь резервных копий.
 */
QString saveFiles(const QString& root, const QHash<QString, QByteArray>& expected,
                  const QHash<QString, QByteArray>& updates);


/// Допустимые `kind` зон корпуса (zones.json), в порядке показа.
const QStringList& bodyKinds();
/// Известные `kind` зон экрана (layouts.json), в порядке показа.
const QStringList& screenKinds();
/// По зоне экрана этого вида можно нажать (учитывается при поиске пересечений).
bool isClickableScreenKind(const QString& kind);

/// Замечание проверки.
struct Issue {
    enum class Level { Error, Warning };
    Level level = Level::Warning;
    int zone = -1; ///< Индекс зоны в слое; -1 — рамка экрана или слой целиком.
    QString text;  ///< Переведённый текст.
};

/// `[x, y, w, h]` зоны; пустой QRectF — поле отсутствует или некорректно.
QRectF rectOf(const QJsonObject& zone);
/// Записать `rect`, округлив значения до сотых.
void setRect(QJsonObject& zone, const QRectF& rect);
/// Округлить до сотых (дробные координаты раскладок хранятся так).
qreal round2(qreal value);

/**
 * @brief Зоны, распознанные на фото: каждый элемент `boxes` — рамка `[x, y, w, h]`.
 *
 * Каждая рамка становится зоной: уникальный id от `base` (Auto, Auto2, …),
 * `radius` по форме (почти квадрат — круг, иначе 0.30 меньшей стороны), `kind` — "Key".
 * Зоны ставятся только на свободное место: рамка, хоть немного пересекающая зону из `existing`,
 * рамку `screen` или ранее принятую рамку из `boxes`, отбрасывается (касание краями допустимо).
 * Неразмерные рамки тоже отбрасываются.
 * @param existing Зоны, отображаемые в редакторе (не сохранённые в файле).
 * @param screen Рамка экрана из редактора; nullopt — экран не размечен.
 */
QJsonArray autoZones(const QJsonArray& boxes, const QJsonArray& existing, const QString& base,
                     const std::optional<QRectF>& screen = std::nullopt);

/**
 * @brief Проверить зоны корпуса. Ошибки — то, из-за чего программа отвергнет ресурсы
 *        при загрузке; предупреждения — подозрительная, но допустимая разметка.
 * @param zones Содержимое zones.json.
 * @param photo Размер фото (profile.json: size).
 * @param screen Рамка экрана; nullopt — экран не размечен.
 */
QList<Issue> checkBody(const QJsonArray& zones, const QSizeF& photo, const std::optional<QRectF>& screen);

/**
 * @brief Проверить зоны раскладки экрана.
 * @param zones Зоны раскладки.
 * @param ref Система координат раскладки.
 * @param bodyIds id зон корпуса модели (для поля `button`).
 * @param modes Режимы экрана модели (для поля `goto`).
 */
QList<Issue> checkScreen(const QJsonArray& zones, const QSizeF& ref, const QSet<QString>& bodyIds,
                         const QSet<QString>& modes);

/// Одна зона одной строкой в стиле файлов ресурсов: `{"id": "Off", "rect": [79, 40, 38, 38], ...}`.
QByteArray zoneLine(const QJsonObject& zone);

/**
 * @brief Исходный текст каждой зоны: элементы корневого массива zones.json или
 *        массива `zones` раскладки `layoutId` в layouts.json.
 * @return nullopt — текст не разобран или раскладки нет.
 */
std::optional<QList<QByteArray>> zoneTexts(const QByteArray& text, const QString& layoutId = {});

/// Разобрать вставленный текст: массив зон, одна зона или строки зон с запятыми; пусто — не зоны.
QJsonArray parseZones(const QByteArray& text);

/// Заменить корневой массив zones.json строками `lines`; nullopt — текст не массив.
std::optional<QByteArray> patchZonesFile(const QByteArray& text, const QList<QByteArray>& lines);
/// Заменить значение `"screen"` в profile.json (nullopt — `null`); nullopt — ключа нет.
std::optional<QByteArray> patchScreen(const QByteArray& text, const std::optional<QRectF>& screen);
/// Заменить раскладку режима в `"modes"` профиля; nullopt — режима нет.
std::optional<QByteArray> patchMode(const QByteArray& text, const QString& mode, const QString& layout);
/// Заменить массив `zones` раскладки `id` в layouts.json; nullopt — раскладки нет.
std::optional<QByteArray> patchLayoutZones(const QByteArray& text, const QString& id,
                                           const QList<QByteArray>& lines);
/**
 * @brief Добавить в конец layouts.json копию раскладки `from` с id `to` и зонами `lines`.
 * @return nullopt — исходной раскладки нет или файл не разобран.
 */
std::optional<QByteArray> appendLayoutCopy(const QByteArray& text, const QString& from, const QString& to,
                                           const QList<QByteArray>& lines);

} // namespace harmony::app::zoneEditing
