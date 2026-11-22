#pragma once

/**
 * @file keyVocabulary.h
 * @brief Словарь имён физических клавиш: имя, категория, допустимые виды зоны, источник имени.
 *
 * Словарь один для всех пультов и не зависит ни от профиля модели, ни от скан-кода: имя —
 * роль клавиши (`Play`, `VolumeUp`, `SoftLeft1`), а не подпись на корпусе. Скан-код — знание
 * семейства, и он живёт отдельно (`keyScans`, шаг K2): код `0x2C` — `Play` на 650, но
 * `SoftRight4` на 885, поэтому общей таблицы кодов нет.
 *
 * Порядок записей — порядок групп в интерфейсе: служебные, активности, устройства,
 * избранное, навигация, D-pad, громкость, каналы, транспорт, цифры, цвета, экран, листание.
 * Порядок задаёт словарь, а не список зон какой-либо модели.
 *
 * Имя — данные, и у каждого имени есть источник (правило 8 [../key-names.md]). Источник —
 * тот, откуда взято имя; для имени с несколькими подтверждениями берётся сильнейший:
 * измерение, фактическая конфигурация, словарь Logitech по модели, надпись на корпусе.
 * Надпись — самый слабый источник: она помечается `Printed`, и по одному только ей список
 * клавиш модели не является исчерпывающим (K4). Имя без источника — придуманное:
 * `NameSource::Unknown`, и проверка (K5/C1) пишет про него предупреждение.
 */

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace harmony {

/// Категория имени — для группировки в интерфейсе.
enum class KeyCategory {
    Service,    ///< служебные
    Activity,   ///< активности
    Device,     ///< устройства
    Favorite,   ///< избранное
    Navigation, ///< навигация
    Dpad,       ///< D-pad
    Volume,     ///< громкость
    Channel,    ///< каналы
    Transport,  ///< транспорт
    Digit,      ///< цифры
    Color,      ///< цвета
    Screen,     ///< экран
    Page,       ///< листание
};

/// Вид физической зоны (`hardware.zones[].kind`). Не путать с `app::ZoneKind`.
enum class PhysicalKind : unsigned {
    Key = 1,
    System = 2,
    ScreenActs = 4,
    ScreenSoft = 8,
    PagePrev = 16,
    PageNext = 32,
};

/// Источник имени (правило 8 [../key-names.md]): все четыре — данные, но надпись слабее.
enum class NameSource {
    Measured,    ///< имя получено измерением: клавишу назвали по тому, что она отправляет
    Config,      ///< имя есть в фактической конфигурации
    LogitechMap, ///< имя берётся из словаря Logitech по модели
    Printed,     ///< надпись на корпусе: данные, но самый слабый источник
    Unknown,     ///< источника нет — имя придумано
};

struct KeyNameInfo {
    std::string_view name;      ///< каноническое имя
    KeyCategory category;       ///< категория — для группировки в интерфейсе
    unsigned kinds;             ///< битовая маска PhysicalKind: виды зон, где имя допустимо
    NameSource source;          ///< источник имени
};

struct KeyAlias {
    std::string_view alias;     ///< имя, которое принимает импорт или старый файл
    std::string_view canonical; ///< каноническое имя словаря
};

/// @return Словарь: порядок групп задаёт порядок интерфейса.
[[nodiscard]] std::span<const KeyNameInfo> keyVocabulary();
/// @return Алиасы словаря.
[[nodiscard]] std::span<const KeyAlias> keyAliases();

/// @brief Запись словаря по имени; алиас — не имя словаря.
/// @param name Каноническое имя.
/// @return Запись или nullptr — имени в словаре нет.
[[nodiscard]] const KeyNameInfo* keyInfo(std::string_view name);

/**
 * @brief Каноническое имя: имя словаря само по себе или имя, на которое указывает алиас.
 * @param nameOrAlias Имя или алиас; регистр важен.
 * @return nullopt — имени нет в словаре и среди алиасов.
 */
[[nodiscard]] std::optional<std::string_view> canonicalKeyName(std::string_view nameOrAlias);

/// @return Имена словаря, допустимые для вида зоны, в порядке словаря.
[[nodiscard]] std::vector<std::string_view> keyNamesForKind(PhysicalKind kind);

/// @brief Вид зоны по имени (`"Key"` → `PhysicalKind::Key`).
/// @return nullopt — имени вида нет.
[[nodiscard]] std::optional<PhysicalKind> physicalKind(std::string_view kind);
/// @return Имя вида — то же, что пишется в `hardware.zones[].kind`.
[[nodiscard]] std::string_view physicalKindName(PhysicalKind kind);

/// @brief Имя — временное: `Auto`, `Auto12`, `Zone`, `Zone3`, `ScanNN`, `Play_copy`.
[[nodiscard]] bool isTemporaryZoneName(std::string_view name);

} // namespace harmony
