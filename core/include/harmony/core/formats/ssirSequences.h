#pragma once

/**
 * @file ssirSequences.h
 * @brief SsIr.bin пульта Harmony 1100: каждая команда — записанная последовательность устройства.
 *
 * У 1100 нет IrProto.bin: прошивка проигрывает сохранённые сигналы (`ir_send_device_ir`).
 * Файл называется как у 900, но устроен иначе (docs/harmony_pk/ssir-sequences.md в afterglow):
 * @code
 *   u16 версия (1), u24 → корень, затем пул данных
 *   корень      u8 число устройств, u24 → таблица устройства × N
 *   устройство  u8 0, u16 число последовательностей, u24 → запись × N
 *   запись      u8 n, n × u24 → описание несущей; u8 m, m × 9 байт вариантов:
 *               u24 → сигнал, u24 → точка повтора (0 — нет), 3 байта нулей
 *   несущая     u8 0, u24 период нс, u24 время включения нс
 *   сигнал      слова u16: бит 15 — импульс, биты 0…14 — длительность, мкс
 * @endcode
 * Длины у сигнала нет: он идёт до следующей структуры пула. Сегменты внутри сигнала
 * закрывает пара слов `0x0001 0x0000`. Всё little endian.
 *
 * Только чтение. Формат подтверждён afterglow на двух настоящих конфигурациях 1100,
 * у нас таких файлов пока нет, поэтому проверка идёт на собранных по описанию данных.
 */

#include "harmony/core/error.h"
#include "harmony/core/ir/irSignal.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace harmony::formats {

/// Один вариант последовательности (у RC5/RC6 по одному на состояние бита переключения).
struct SsirVariant {
    std::vector<std::uint16_t> words;     ///< Слова сигнала до границы следующей структуры
    std::optional<std::size_t> repeatAt;  ///< Индекс слова точки повтора внутри этого сигнала
    std::vector<std::uint16_t> sharedRepeat; ///< Повтор из сигнала другой команды устройства (слова до его конца)
};

/// Последовательность команды: нажатие или удержание.
struct SsirSequence {
    std::uint32_t carrierPeriodNs = 0; ///< Период несущей, нс
    std::uint32_t carrierOnNs = 0;     ///< Время включения, нс
    std::vector<SsirVariant> variants;

    /// @return Несущая, Гц; 0 — период не указан.
    [[nodiscard]] std::uint32_t frequency() const;
};

/// Содержимое SsIr.bin 1100: devices[d][s] — последовательность s устройства d.
struct SsirSequences {
    std::vector<std::vector<SsirSequence>> devices;

    /// @return Всего последовательностей.
    [[nodiscard]] std::size_t sequenceCount() const;
};

/**
 * @brief Разбирает SsIr.bin пульта 1100.
 * @return Таблица; ErrorCode::Format — версия не 1 или указатель выходит за файл.
 */
[[nodiscard]] Result<SsirSequences> parseSsirSequences(std::span<const std::uint8_t> file);

/**
 * @brief Первый вариант последовательности как ИК-сигнал для показа и сравнения.
 *
 * Берётся первый сегмент (до `0x0001 0x0000`); паузы-слова подряд склеиваются, пауза
 * в начале (задержка перед нажатием) отбрасывается.
 * @return nullopt — вариантов нет или в сегменте нет ни одного импульса.
 */
[[nodiscard]] std::optional<ir::IrSignal> firstFrameSignal(const SsirSequence& sequence);

} // namespace harmony::formats
