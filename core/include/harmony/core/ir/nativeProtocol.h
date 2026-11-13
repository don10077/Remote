#pragma once

/**
 * @file nativeProtocol.h
 * @brief Команда по протоколу в собственном формате пульта: запись IrProto.bin и <Code>.
 *
 * Сохранённая сигналом (SsIr.bin) команда — один застывший кадр. Для протоколов
 * с отдельным кадром повтора (NEC — короткий «код повтора», JVC — кадр без
 * заголовка) или с бит-переключателем (RC5/RC6) это неверно: при удержании кнопки
 * уходит не то, а два нажатия подряд устройство принимает за одно долгое.
 * Запись протокола в IrProto.bin описывает все этапы, и пульт сам шлёт повторы
 * и меняет бит переключения.
 *
 * Программы протоколов и коды — по проекту afterglow (GPL-3.0,
 * backends/harmony_pk/ir_emit.py и builder/codes.py): тайминги NEC и Samsung
 * совпадают с проверенными на пульте, Sony, JVC и RC5 сняты с пульта прибором.
 * RC6 Mode 0 собран по таймингам кодировщика и требует проверки на стенде.
 * Каждая команда перед сохранением исполняется (carrierVm.h) и распознаётся
 * заново: если результат не совпал с протоколом, адресом и командой, команда
 * не выдаётся — её сохраняют сигналом.
 */

#include "harmony/core/error.h"
#include "harmony/core/formats/irTables.h"
#include "harmony/core/ir/irSignal.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace harmony::ir {

/// Команда для пульта: протокол и код.
struct NativeCommand {
    formats::Bytes protocol;        ///< Запись IrProto.bin (перемещаемый вид)
    std::vector<std::uint8_t> code; ///< Байты <Code>; номер протокола (байты 0–1) — 0
};

/**
 * @brief Элемент программы протокола: начало кадра, символы данных, конец, период.
 *
 * Длительности — мкс, импульс положительный, пауза отрицательная; длиннее
 * 32767 мкс разбиваются на несколько слов сами. Пульт передаёт элемент так:
 * начало, @ref symbols символов из <Code> (каждый — запись алфавита), конец и
 * паузу до @ref periodUs от начала элемента.
 */
struct ProgramElement {
    static constexpr std::uint32_t kNoPeriod = 0xFFFFFFFF;
    static constexpr std::uint8_t kNoToggle = 0xFF;

    std::vector<int> before;                ///< Начало кадра
    std::vector<std::vector<int>> alphabet; ///< Символы; номер символа — его значение в <Code>
    unsigned symbols = 0;                   ///< Сколько символов в кадре (0 — данных нет)
    std::vector<int> after;                 ///< Конец кадра
    std::uint32_t periodUs = kNoPeriod;     ///< Длина кадра от его начала
    std::uint8_t toggle = kNoToggle;        ///< Номер бита данных, который пульт переключает сам
};

/**
 * @brief Запись IrProto.bin (перемещаемый вид) из элементов.
 * @param carrierHz Несущая.
 * @return ErrorCode::Format — элементов нет или больше 255, у элемента нечего передать,
 *         отрезок длиннее 255 слов, алфавит больше 255 символов.
 */
[[nodiscard]] Result<formats::Bytes> buildProgram(std::uint32_t carrierHz,
                                                  std::span<const ProgramElement> elements);

/// @return true, если протокол (название как в encodableProtocols()) хранится в IrProto.bin.
[[nodiscard]] bool hasNativeProtocol(std::string_view protocol);

/**
 * @brief Протокол и код команды для пульта (с проверкой исполнением).
 * @param protocol «NEC», «NEC (extended)», «Onkyo», «Samsung32», «Sony SIRC 12/15/20», «JVC», «RC5», «RC6».
 * @return ErrorCode::Unsupported — протокол пульт так не хранит (сохранять сигналом);
 *         ErrorCode::Format — адрес или команда не помещаются, или проверка не прошла.
 */
[[nodiscard]] Result<NativeCommand> nativeCommand(std::string_view protocol, std::uint32_t address,
                                                  std::uint32_t command);

/**
 * @brief Протокол выученного сигнала, если его можно хранить протоколом без потерь.
 *
 * decode() смотрит только первый кадр. Здесь проверяется весь сигнал: каждый кадр (до паузы
 * длиннее 10 мс) должен распознаваться той же командой или совпадать по длительностям с
 * кадром, который собирают encode() или программа протокола при удержании (код повтора NEC,
 * кадр JVC без заголовка). Так кнопка
 * с двумя разными кадрами (Pioneer и подобные) не урезается до первого.
 * @return Протокол (RC5X — как «RC5», название из encodableProtocols()), адрес и команда;
 *         nullopt — протокол не распознан, пульт его так не хранит или в сигнале чужие кадры.
 */
[[nodiscard]] std::optional<IrDecoded> nativeProtocolOf(const IrSignal& signal);

/**
 * @brief Текст <Code> с номером протокола: «0x0100E80301…».
 * @param code Байты кода.
 * @param protocolIndex Номер записи в IrProto.bin (до 255).
 */
[[nodiscard]] std::string codeText(std::span<const std::uint8_t> code, unsigned protocolIndex);

} // namespace harmony::ir
