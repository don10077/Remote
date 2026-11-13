#pragma once

/**
 * @file irSignal.h
 * @brief ИК-сигнал: несущая и чередование импульсов и пауз; хранение в SsIr.bin и распознавание.
 *
 * Сигнал записывается как в LIRC и Flipper Zero: длительности в микросекундах,
 * положительные — импульс (несущая включена), отрицательные — пауза.
 *
 * Запись SsIr.bin (выученный «сырой» код):
 * @code
 *   u32  период несущей, нс (26316 ≈ 38 кГц)
 *   u16  число слов
 *   u16 × N  слова: бит 15 — импульс, биты 0…14 — длительность, мкс
 * @endcode
 * Все числа — little endian.
 */

#include "harmony/core/error.h"
#include "harmony/core/formats/irTables.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace harmony::ir {

/// ИК-сигнал.
struct IrSignal {
    std::uint32_t frequency = 38000; ///< Несущая, Гц
    std::vector<int> durations;      ///< +импульс / −пауза, мкс

    bool operator==(const IrSignal&) const = default;
};

/// Распознанная команда известного протокола.
struct IrDecoded {
    std::string protocol;   ///< «NEC», «Samsung32», «Sony SIRC 12», «Panasonic», «RC5», «RC6», …
    std::uint32_t address = 0;
    std::uint32_t command = 0;
    std::vector<bool> bits; ///< Биты данных в порядке передачи
    bool lsbFirst = true;   ///< Числа протокола передаются младшим битом вперёд (NEC, Sony, Kaseikyo…)

    /// @return «NEC · адрес 0x04, команда 0x08».
    [[nodiscard]] std::string text() const;

    /**
     * @brief Код, как его печатает Arduino IRremote («Raw-Data»).
     *
     * Для протоколов «младшим битом вперёд» первый переданный бит — младший:
     * NEC 0xF708FB04 — адрес 0x04, ~адрес, команда 0x08, ~команда.
     * Для RC5/RC6 — в порядке передачи.
     * @return «0xF708FB04»; пусто — бит нет.
     */
    [[nodiscard]] std::string rawData() const;
};

/// Биты (первый — первым переданный) как шестнадцатеричное число, старшие цифры — слева.
[[nodiscard]] std::string bitsToHex(const std::vector<bool>& bits);

/**
 * @brief Запись SsIr.bin для сигнала.
 *
 * Длительности длиннее 32767 мкс делятся на равные слова: подряд идущие слова
 * одного вида пульт складывает.
 */
[[nodiscard]] formats::Bytes encodeRawEntry(const IrSignal& signal);

/**
 * @brief Сигнал из записи SsIr.bin (подряд идущие слова одного вида складываются).
 * @return ErrorCode::Format — запись короче заголовка или число слов не сходится.
 */
[[nodiscard]] Result<IrSignal> decodeRawEntry(std::span<const std::uint8_t> entry);

/**
 * @brief Сигнал из ответа пульта при обучении.
 *
 * Пульт присылает пары «импульс, импульс + пауза» (мкс).
 * @param frequency Несущая, Гц.
 * @param markSegments Пары слов; нулевые длительности пропускаются.
 */
[[nodiscard]] IrSignal fromMarkSegments(std::uint32_t frequency, const std::vector<std::uint16_t>& markSegments);

/**
 * @brief Распознаёт протокол по первому кадру сигнала.
 *
 * NEC (и расширенный — «NEC (extended)», Onkyo), LG, JVC, Samsung32, RCA, Sony SIRC 12/15/20,
 * Kaseikyo (Panasonic, Denon, JVC, Sharp, Mitsubishi), Denon/Sharp 15, RC5/RC5X, RC6 (режимы 0 и 6).
 * @return Команда или nullopt — протокол не распознан (сигнал всё равно можно хранить «сырым»).
 */
[[nodiscard]] std::optional<IrDecoded> decode(const IrSignal& signal);

/// @return Длительности текстом: «+9000 -4500 +560 -560 …», мкс.
[[nodiscard]] std::string timingText(const IrSignal& signal);

/**
 * @brief Сигнал в формате Pronto Hex («0000 006D 0022 0000 0157 00AB …»).
 * @return Пусто — сигнала нет.
 */
[[nodiscard]] std::string prontoHex(const IrSignal& signal);

/**
 * @brief Тот же ли это сигнал (та же кнопка пульта).
 *
 * Распознанные сравниваются по протоколу, адресу и команде (бит-переключатель
 * RC5/RC6 и число повторов не важны); нераспознанные — по первому кадру (до
 * паузы длиннее 10 мс), каждая длительность в пределах 25% или 200 мкс.
 */
[[nodiscard]] bool sameSignal(const IrSignal& a, const IrSignal& b);

/// @return Краткое описание для показа: протокол или «импульсов: 67, 38 кГц».
[[nodiscard]] std::string describe(const IrSignal& signal);

} // namespace harmony::ir
