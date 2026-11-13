#pragma once

/**
 * @file irProtocol.h
 * @brief Команда по протоколу IrProto.bin: разбор поля <Code> и сборка ИК-сигнала.
 *
 * Большинство команд пульт хранит не сигналом, а данными для протокола:
 * <Protocol>N</Protocol> — номер записи в IrProto.bin, <Code> — биты.
 * Устройство <Code> (числа 16 бит — little endian):
 * @code
 *   u16 номер протокола | u16 задержка | u8 сколько раз передать кадр данных
 *   | u8 бит 0 — есть кадр повтора | u8 вид данных | данные | 00
 *   вид 0 — биты подряд, затем 01 01
 *   вид 1 — байт 00, затем биты подряд
 *   вид N ≥ 2 — байт 00, затем N секций по 16 бит
 * @endcode
 * Биты идут в порядке передачи (старший бит байта — первый).
 *
 * Запись протокола (смещения — от начала записи, как после formats::parseIrProto()):
 * @code
 *   01 | u16 период несущей, нс | 00 00 32 | u8 число секций | u16 × N смещения секций
 *   секция: u16 число бит | u8 бит переключения | FF | u32 период кадра, мкс (FFFFFFFF — нет)
 *           | u8 вид кадра | u8 пар таймингов данных (0/1/2)
 *           | u16 → тайминги данных | u16 → начало кадра | u16 → конец кадра (0 — нет)
 *   начало/конец кадра: u8 число слов, u16 × N слова (бит 15 — импульс, остальное — мкс)
 *   данные: пары «импульс, пауза» для бита 0, затем для бита 1
 * @endcode
 */

#include "harmony/core/error.h"
#include "harmony/core/ir/irSignal.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace harmony::ir {

/// Разобранное поле <Code> команды по протоколу.
struct ProtocolCode {
    unsigned protocol = 0;    ///< Номер протокола в IrProto.bin
    unsigned delay = 0;       ///< Задержка (назначение неизвестно; обычно 500)
    unsigned dataFrames = 1;  ///< Сколько раз передаётся кадр данных
    bool repeatFrame = false; ///< После данных — кадр повтора
    std::vector<std::vector<bool>> sections; ///< Биты секций в порядке передачи

    /// @return Все биты данных подряд в порядке передачи.
    [[nodiscard]] std::vector<bool> bits() const;
};

/**
 * @brief Разбирает <Code> («0x0000F401…»).
 * @return ErrorCode::Format — не шестнадцатеричная строка или неизвестное устройство.
 */
[[nodiscard]] Result<ProtocolCode> parseProtocolCode(std::string_view code);

/**
 * @brief Собирает сигнал, который пульт передаст по команде.
 * @param protocol Запись протокола из IrProto.bin.
 * @param code Данные команды.
 * @return ErrorCode::Format — запись протокола повреждена.
 */
[[nodiscard]] Result<IrSignal> renderProtocol(std::span<const std::uint8_t> protocol, const ProtocolCode& code);

} // namespace harmony::ir
