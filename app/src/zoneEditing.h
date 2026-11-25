#pragma once

/**
 * @file zoneEditing.h
 * @brief Логика редактора зон без виджетов: проверка зон и точечная правка текста profile.json.
 *
 * Слои разметки: «корпус» — `hardware.zones` в пикселях фото, «экран» — `cells` одного экрана
 * (`screens[]`) в единицах его `ref`. Правка заменяет в файле только изменённый фрагмент
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
 * @param zones Зоны корпуса (`hardware.zones` в profile.json).
 * @param photo Размер фото (`hardware.size`).
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
 * @brief Исходный текст каждой зоны profile.json: `hardware.zones` при пустом `screenId`,
 *        иначе `cells` экрана `screenId`.
 * @return nullopt — текст не разобран или такого экрана нет.
 */
std::optional<QList<QByteArray>> zoneTexts(const QByteArray& text, const QString& screenId = {});

/// Разобрать вставленный текст: массив зон, одна зона или строки зон с запятыми; пусто — не зоны.
QJsonArray parseZones(const QByteArray& text);

/// Заменить массив зон корпуса (`screenId` пуст) или ячеек экрана строками `lines`; nullopt — массива нет.
std::optional<QByteArray> patchZones(const QByteArray& text, const QString& screenId,
                                     const QList<QByteArray>& lines);
/// Зазор между плитками по умолчанию (`grid.gap`), в единицах `ref`.
inline constexpr qreal kDefaultGap = 4;

/**
 * @brief Записать в `grid` экрана `screenId` число колонок и рядов плиток и зазор (зона `area` сохраняется).
 * @param gap Зазор между плитками; nullopt — прежний `gap` сохраняется. Зазор по умолчанию
 *        (kDefaultGap) не дописывается, если его в файле не было.
 * @return Новый текст; исходный, если у экрана нет `grid`; nullopt — экрана нет.
 */
std::optional<QByteArray> patchGrid(const QByteArray& text, const QString& screenId, int cols, int rows,
                                    std::optional<qreal> gap = std::nullopt);
/**
 * @brief Записать одно значение profile.json, не трогая остальной текст.
 *
 * Существующее значение заменяется на месте; отсутствующий ключ дописывается последним членом
 * объекта в его оформлении (с новой строки, если объект многострочный), недостающие
 * промежуточные объекты создаются. Удаление убирает член вместе с запятой.
 * @param screenId Экран (`screens[].id`), внутри которого путь; пусто — от корня файла.
 * @param keys Путь ключей, например `{"tiles", "color"}`.
 * @param value Новое значение; `QJsonValue::Undefined` — удалить ключ (его отсутствие не ошибка).
 * @return Новый текст; nullopt — нет экрана либо на пути стоит не объект.
 */
std::optional<QByteArray> patchValue(const QByteArray& text, const QString& screenId, const QStringList& keys,
                                     const QJsonValue& value);
/**
 * @brief Дописать экран в конец `screens` с отступами файла; ячейки — по одной в строке.
 * @return Новый текст; nullopt — в файле нет массива `screens`.
 */
std::optional<QByteArray> appendScreen(const QByteArray& text, const QJsonObject& screen);
/**
 * @brief Убрать экран `screenId` из `screens`, не трогая остальной текст.
 * @return Новый текст; nullopt — такого экрана нет.
 */
std::optional<QByteArray> removeScreen(const QByteArray& text, const QString& screenId);
/// Заменить `display.rect` (nullopt — `null`); nullopt — ключа нет.
std::optional<QByteArray> patchDisplay(const QByteArray& text, const std::optional<QRectF>& rect);

} // namespace harmony::app::zoneEditing
