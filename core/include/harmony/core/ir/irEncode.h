#pragma once

/**
 * @file irEncode.h
 * @brief ИК-сигнал по протоколу, адресу и команде — без родного пульта.
 *
 * Обратное распознаванию (decode()): те же названия протоколов и та же
 * нумерация адреса и команды, что показывает программа и Arduino IRremote.
 * Сигнал — один кадр с паузой после него и повторы, которых требует протокол
 * (NEC — код повтора, Sony — три кадра, Denon и Sharp — кадр с инверсией).
 */

#include "harmony/core/error.h"
#include "harmony/core/ir/irSignal.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace harmony::ir {

/// Протокол, по которому можно собрать сигнал.
struct EncodableProtocol {
    std::string name;    ///< Как в decode(): «NEC», «Samsung32», «Sony SIRC 12»…
    int addressBits = 8; ///< Разрядность адреса
    int commandBits = 8; ///< Разрядность команды
    std::uint32_t frequency = 38000;
};

/// @return Протоколы в порядке показа (частые — первыми).
[[nodiscard]] const std::vector<EncodableProtocol>& encodableProtocols();

/**
 * @brief Собирает сигнал.
 * @param protocol Название из encodableProtocols().
 * @param address Адрес (в пределах разрядности протокола).
 * @param command Команда (в пределах разрядности протокола).
 * @return ErrorCode::Unsupported — протокол неизвестен; ErrorCode::Format — адрес или
 *         команда не помещаются в протокол.
 */
[[nodiscard]] Result<IrSignal> encode(std::string_view protocol, std::uint32_t address,
                                      std::uint32_t command);

/**
 * @brief Собирает команду из полного кадра данных в порядке передачи.
 * @param protocol Протокол из encodableProtocols().
 * @param bits Данные, включая проверочные биты, как в IrDecoded::bits.
 * @return Сигнал или ошибка длины/контрольных битов. RC5 toggle нормализуется.
 */
[[nodiscard]] Result<IrSignal> encodeBits(std::string_view protocol, const std::vector<bool>& bits);

} // namespace harmony::ir
