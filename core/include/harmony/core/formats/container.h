#pragma once

/**
 * @file container.h
 * @brief Общая рамка контейнера конфигураций архитектур 8, 9, 10, 12, 14 и 16.
 *
 * Описана в support/remotes-plan.md («Главная находка») по reference/config-format/
 * container.md проекта harmony-explorations (MIT):
 * @code
 * 0x00  char[4]  признак        TPTP / AHCM / GSPM
 * 0x04  u32      end_addr       абсолютный адрес концевого маркера
 * 0x08  u32      N в байте 0x09 число слотов, старшие разряды нулевые
 * 0x0B  {u8 spare; u24 address}[N]   таблица секций, адреса абсолютные
 *       char[4]  маркер         WLWL / CMAH / LWJL
 *       …        секции в порядке слотов, длина — до следующего указателя
 * end-6 u16      контрольная сумма (XOR слов с затравкой)
 * end-4 char[4]  концевой маркер DKDK / MCHA / PTYY
 * @endcode
 *
 * Различаются только признак, маркеры и число слотов — их задаёт таблица семейств
 * containerFamilies(), и там же ответы на два вопроса: подтверждена ли семантика секций
 * строкой данными и допускается ли заполнение после концевого маркера. Указатели —
 * абсолютные адреса флеш, поэтому адрес начала
 * (base) выводится из `end_addr` минус смещение концевого маркера; в самом файле
 * он не записан. Указатель элемента таблицы — байт-заполнитель и u24, читать его
 * как u32 нельзя: ненулевой заполнитель дал бы правдоподобную ошибку вместо отказа.
 *
 * Разбор — проверки, а не допущения: непустые указатели секций растут по слотам,
 * последний слот пуст (его нули держат маркер), маркер стоит ровно на `0x0B + 4N`,
 * `N` из байта 0x09 не расходится с положением маркера, сумма сходится. Не сошлось —
 * отказ с причиной.
 */

#include "harmony/core/error.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace harmony::formats {

/// Начало таблицы секций; адрес указателя слота i лежит на 0x0B + 4·i + 1.
inline constexpr std::size_t kContainerTableOffset = 0x0B;
/// Затравка суммы контейнера (SEED в Description.xml у Logitech).
inline constexpr std::uint16_t kContainerChecksumSeed = 0x4321;

/// Семейство контейнера — архитектура пульта.
enum class ContainerFamily {
    Arch8,   ///< 720, 785, 88x
    Arch9,   ///< 510–555
    Arch10,  ///< 890, 895 и экстендер C-RE4A (11 слотов)
    Arch12,  ///< One
    Arch14,  ///< 600, 650, 665, 700
    Arch16,  ///< 200, 300, 350
};

/// Константы семейства: признаки, маркеры, число слотов и два решения о строке — подтверждена
/// ли семантика секций данными и допускается ли заполнение после концевого маркера.
struct ContainerFamilyInfo {
    ContainerFamily family;   ///< Архитектура; у одной архитектуры может быть несколько строк с разным числом слотов
    int architecture = 0;     ///< Номер архитектуры (8…16)
    std::string_view cookie;  ///< Признак в 0x00
    std::string_view marker;  ///< Маркер сразу за таблицей слотов
    std::string_view endMarker; ///< Концевой маркер (в конце образа)
    std::uint32_t slots = 0;  ///< Число слотов N
    bool semanticsVerified = true;      ///< семантика секций подтверждена данными
    bool trailingPaddingAllowed = false; ///< файл может иметь заполнение после маркера

    /// Смещение маркера за таблицей: ровно `0x0B + 4N`.
    [[nodiscard]] constexpr std::size_t markerOffset() const {
        return kContainerTableOffset + 4 * static_cast<std::size_t>(slots);
    }
};

/// Таблица семейств (признак + число слотов однозначно дают архитектуру).
[[nodiscard]] const std::array<ContainerFamilyInfo, 7>& containerFamilies();

/**
 * @brief Семейство по признаку и числу слотов.
 * @return nullptr — такой комбинации в таблице нет.
 */
[[nodiscard]] const ContainerFamilyInfo* findFamily(std::string_view cookie, std::uint32_t slots);

/// Разобранный контейнер: рамка и таблица секций.
struct Container {
    const ContainerFamilyInfo* family = nullptr; ///< Семейство (указатель на таблицу)
    std::uint32_t base = 0;        ///< Адрес флеш, с которого идёт контейнер (обычно кратен 0x1000)
    std::uint32_t endAddress = 0;  ///< `end_addr` из 0x04 — абсолютный адрес концевого маркера
    std::size_t endOffset = 0;     ///< Смещение концевого маркера в данных
    std::size_t checksumOffset = 0;///< Смещение двух байт сохранённой суммы
    std::uint16_t checksumStored = 0; ///< Сумма, как она записана в файле
    /// Смещения секций от начала образа по номеру слота; nullopt — слота нет (нулевой указатель).
    std::vector<std::optional<std::size_t>> sections;

    /// @return Сколько секций присутствует.
    [[nodiscard]] std::size_t sectionCount() const;
};

/// Диапазон секции: от её начала до начала следующей секции (или до контрольной суммы).
struct ContainerRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

/**
 * @brief Разбирает рамку контейнера: признак, `end_addr`, `N`, таблицу `{spare, u24}`,
 *        маркер, секции по порядку слотов, сумму и концевой маркер.
 *
 * Конец образа берётся по концевому маркеру: `base = end_addr − end`, где end — смещение
 * маркера. Для строки таблицы, где `trailingPaddingAllowed`, он может стоять не в конце файла.
 * @param payload Бинарные данные контейнера (после снятия EZHex/ZIP); assumed
 *        целиком им и являются. Заполнение после концевого маркера допускается только
 *        для строки таблицы, где `trailingPaddingAllowed`: там маркер ищется, а всё
 *        после него должно быть нулями (иначе это не конец образа, а данные).
 * @return Контейнер; ErrorCode::Format — признак или `N` не из таблицы семейств,
 *         маркеры не на своих местах, `N` расходится с маркером, заполнитель слота
 *         ненулевой, указатель секции вне образа или не растёт, указатель есть там,
 *         где маркер, данные короче рамки или сумма не сходится.
 */
[[nodiscard]] Result<Container> parseContainer(std::span<const std::uint8_t> payload);

/**
 * @brief Диапазон секции контейнера (секции идут в порядке слотов).
 * @return nullopt — номер вне таблицы или секции нет.
 */
[[nodiscard]] std::optional<ContainerRange> containerSectionRange(const Container& container, std::size_t index);

/**
 * @brief Сумма контейнера: XOR слов (u16 little endian) от первого байта до
 *        сохранённого значения, затравка 0x4321; нечётный хвост не учитывается.
 */
[[nodiscard]] std::uint16_t containerChecksum(std::span<const std::uint8_t> payload, std::size_t checksumOffset);

} // namespace harmony::formats
