#pragma once

/**
 * @file carrierVm.h
 * @brief Исполнение команды по протоколу так, как это делает генератор ИК пульта.
 *
 * Пульт не хранит готовый сигнал команды по протоколу: запись IrProto.bin — это
 * маленькая программа из «элементов» (начало, символы данных, конец, период кадра),
 * а <Code> команды говорит, какие элементы и с какими битами исполнить на каждом
 * этапе — нажатие (этап 2), повтор при удержании (этап 3), отпускание (этап 5).
 * Байт 4 кода — сколько раз минимум исполнить этап 3. Бит-переключатель (RC5)
 * пульт подставляет сам на позиции, записанной в элементе.
 *
 * Здесь — порт этого исполнителя (прошивки irgen) из проекта afterglow (GPL-3.0,
 * backends/harmony_pk/ir_vm.py, восстановлен по дизассемблированной прошивке).
 * Он нужен, чтобы проверять сгенерированные протоколы до записи на пульт и
 * показывать сигнал команд, которые простой разбор кода не понимает.
 *
 * Устройство <Code> (байты):
 * @code
 *   u16 номер протокола | u16 пауза перед нажатием, мс | u8 минимум повторов этапа 3
 *   далее этапы, начиная со 2-го: u8 число элементов (00 — этап пуст, следующий)
 *   и для каждого: u8 номер элемента, затем байты его данных (биты — старшим вперёд)
 * @endcode
 */

#include "harmony/core/error.h"
#include "harmony/core/ir/irSignal.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace harmony::ir {

/// Итог исполнения команды.
struct CarrierRun {
    IrSignal signal;                    ///< Что пульт передаст (соседние длительности сложены)
    std::vector<int> stages;            ///< Этапы по порядку исполнения: 2, 3, 3, …
    std::vector<std::size_t> stageWords; ///< Сколько слов длительности дал каждый этап
    std::vector<std::vector<int>> stageDurations; ///< Длительности каждого этапа отдельно
};

/**
 * @brief Байты поля <Code> («0x0000E803…»).
 * @return ErrorCode::Format — не шестнадцатеричная строка.
 */
[[nodiscard]] Result<std::vector<std::uint8_t>> codeBytes(std::string_view code);

/**
 * @brief Исполняет команду.
 * @param protocol Запись IrProto.bin (перемещаемый вид, как после formats::parseIrProto()).
 * @param code Байты <Code>; номер протокола в нём не важен.
 * @param toggleState Состояние бит-переключателей (бит 0 — первый, бит 1 — второй).
 * @param heldReplays Сколько раз ещё повторить этап 3 — как если кнопку держат.
 * @return Сигнал по этапам; ErrorCode::Format — программа или код не исполняются.
 */
[[nodiscard]] Result<CarrierRun> runCarrier(std::span<const std::uint8_t> protocol,
                                            std::span<const std::uint8_t> code, unsigned toggleState = 0,
                                            unsigned heldReplays = 0);

} // namespace harmony::ir
