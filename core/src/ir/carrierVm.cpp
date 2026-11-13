/**
 * @file carrierVm.cpp
 * @brief Порт исполнителя ИК-программ пульта (afterglow ir_vm.py, GPL-3.0).
 *
 * Логика повторяет оригинал шаг в шаг, включая переполнение 8- и 16-битных
 * полей состояния: так проверка ловит те же ошибки, что сделал бы пульт.
 */

#include "harmony/core/ir/carrierVm.h"

#include "harmony/core/formats/irTables.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <string>

namespace harmony::ir {

namespace {

constexpr std::size_t kBase = 5;          ///< От этого байта отсчитываются указатели программы
constexpr std::uint32_t kMaxGap = 0x7FFF; ///< Наибольшая длительность одного слова, мкс
constexpr std::uint32_t kNoPeriod = 0xFFFFFFFF;
constexpr std::size_t kMaxWords = 10000;
constexpr std::size_t kWrapper = 8;       ///< Обёртка IrProto.bin: CRC32, размер, 00 00

/// Ошибка исполнения (внутри модуля; наружу — Result).
struct VmError {
    std::string message;
};

class Vm {
public:
    Vm(std::vector<std::uint8_t> payload, std::vector<std::uint8_t> code, unsigned toggleState)
        : payload_(std::move(payload)), code_(std::move(code)), toggle_(toggleState & 3U) {
        if (payload_.size() < 10 || payload_[kBase] != 1) {
            throw VmError{"the protocol table has no carrier program"};
        }
        if (code_.size() < 7) {
            throw VmError{"the command code is too short"};
        }
        const unsigned index = code_[0] | (code_[1] << 8);
        if (index >= u16(kBase + 1)) {
            throw VmError{"the code selects a missing protocol"};
        }
        block_ = kBase + u16(kBase + 3 + index * 2);
        if (block_ + 7 > payload_.size()) {
            throw VmError{"the protocol starts outside the table"};
        }
        command_.assign(code_.begin() + 5, code_.end());
    }

    CarrierRun run(unsigned heldReplays) {
        if (!sequenceInit() || !elementInit()) {
            throw VmError{"the command code has nothing to transmit"};
        }
        CarrierRun result;
        std::vector<std::uint16_t> words;
        for (;;) {
            result.stages.push_back(stage_);
            const std::size_t start = words.size();
            for (;;) {
                if (words.size() >= kMaxWords) {
                    throw VmError{"the program does not finish"};
                }
                words.push_back(nextWord());
                if (!prepareNext()) {
                    break;
                }
            }
            result.stageWords.push_back(words.size() - start);
            result.stageDurations.push_back(durations(std::span<const std::uint16_t>(words).subspan(start)));
            if (!advance(heldReplays)) {
                break;
            }
        }
        const std::uint32_t period = u32(block_ + 1);
        result.signal.frequency = period > 0 ? static_cast<std::uint32_t>((1'000'000'000ULL + period / 2) / period) : 0;
        result.signal.durations = durations(words);
        return result;
    }

private:
    static std::vector<int> durations(std::span<const std::uint16_t> words) {
        std::vector<int> out;
        for (const std::uint16_t word : words) {
            const int length = word & 0x7FFF;
            if (length == 0) {
                continue; // слово-заполнитель
            }
            const int value = (word & 0x8000) != 0 ? length : -length;
            if (!out.empty() && (out.back() > 0) == (value > 0)) {
                out.back() += value;
            } else {
                out.push_back(value);
            }
        }
        return out;
    }

    std::uint8_t byte(std::size_t at) const {
        if (at >= payload_.size()) {
            throw VmError{"the program reads outside the protocol table"};
        }
        return payload_[at];
    }
    std::uint32_t u16(std::size_t at) const { return byte(at) | (static_cast<std::uint32_t>(byte(at + 1)) << 8); }
    std::uint32_t u32(std::size_t at) const { return u16(at) | (u16(at + 2) << 16); }

    std::uint8_t commandByte() {
        if (cursor_ >= command_.size()) {
            throw VmError{"the command code ended while its data was being read"};
        }
        return command_[cursor_++];
    }

    bool sequenceInit() {
        if (stage_ >= 6 || cursor_ >= command_.size()) {
            return false;
        }
        if (command_[cursor_] == 0) {
            ++stage_;
            while (stage_ < 6) {
                ++cursor_;
                if (cursor_ >= command_.size() || command_[cursor_] != 0) {
                    break;
                }
                ++stage_;
            }
        }
        if (stage_ >= 6 || cursor_ >= command_.size()) {
            return false;
        }
        savedCursor_ = cursor_;
        sequenceCount_ = commandByte();
        sequenceIndex_ = 0;
        return true;
    }

    std::uint8_t toggle(std::uint32_t value, unsigned bitOffset) const {
        const unsigned first = byte(element_ + 2);
        if (first == 0xFF) {
            return static_cast<std::uint8_t>(value & 0xFF);
        }
        if (first >= bitOffset && first < bitOffset + 8) {
            const std::uint32_t mask = 1U << (7 - (first - bitOffset));
            value = (toggle_ & 1U) != 0 ? (value | mask) : (value & ~mask);
        }
        const unsigned second = byte(element_ + 3);
        if (second != 0xFF && second >= bitOffset && second < bitOffset + 8) {
            const std::uint32_t mask = 1U << (7 - (second - bitOffset));
            value = (toggle_ & 2U) != 0 ? (value | mask) : (value & ~mask);
        }
        return static_cast<std::uint8_t>(value & 0xFF);
    }

    void selectDuration() {
        const std::uint32_t symbol = (accumulator_ >> 8) & mask_;
        pointer_ = kBase + u16(element_ + 10) + byte(element_ + 9) * 2U * symbol;
    }

    void dataInit() {
        const unsigned symbols = byte(element_ + 8);
        if (symbols == 0) {
            throw VmError{"a data section has an empty symbol alphabet"};
        }
        unsigned bits = 0;
        while ((1U << bits) < symbols) {
            ++bits;
        }
        bits_ = static_cast<std::uint8_t>(bits);
        mask_ = static_cast<std::uint8_t>(bits != 0 ? (1U << bits) - 1 : 0);
        const std::uint32_t value = toggle(commandByte(), 0);
        bitOffset_ = 0;
        accumulator_ = (value << bits) & 0xFFFF;
        remaining_ = static_cast<std::uint8_t>(8 - bits);
        selectDuration();
    }

    void dataProcess() {
        const unsigned remaining = remaining_;
        const unsigned bits = bits_;
        std::uint32_t accumulator = accumulator_;
        if (remaining < bits) {
            bitOffset_ = static_cast<std::uint8_t>(bitOffset_ + 8);
            const std::uint32_t value = toggle(commandByte(), bitOffset_);
            accumulator = (value | (accumulator << remaining)) & 0xFFFF;
            accumulator = (accumulator << (bits - remaining)) & 0xFFFF;
            remaining_ = static_cast<std::uint8_t>(8 - (bits - remaining));
        } else {
            accumulator = (accumulator << bits) & 0xFFFF;
            remaining_ = static_cast<std::uint8_t>(remaining - bits);
        }
        accumulator_ = accumulator;
        selectDuration();
    }

    /// Начинает отрезок слов: начало или конец кадра (run) по указателю @p at.
    void startRun(unsigned phase, std::uint32_t at) {
        phase_ = phase;
        length_ = 1;
        unitWords_ = byte(kBase + at);
        pointer_ = kBase + at + 1;
    }

    bool elementInit() {
        if (cursor_ >= command_.size()) {
            return false;
        }
        const unsigned index = commandByte();
        if (index >= byte(block_ + 6)) {
            throw VmError{"the code selects a missing element of the protocol"};
        }
        element_ = kBase + u16(block_ + 7 + index * 2);
        if (element_ + 16 > payload_.size()) {
            throw VmError{"an element starts outside the protocol table"};
        }
        const std::uint32_t before = u16(element_ + 12);
        const std::uint32_t after = u16(element_ + 14);
        if (before != 0 && byte(kBase + before) != 0) {
            startRun(0, before);
        } else if (u16(element_ + 10) != 0) {
            phase_ = 1;
            length_ = u16(element_);
            unitWords_ = byte(element_ + 9);
            dataInit();
        } else if (after != 0 && byte(kBase + after) != 0) {
            startRun(2, after);
        } else {
            throw VmError{"an element has nothing to transmit"};
        }
        emitted_ = 0;
        unitPosition_ = 0;
        total_ = u32(element_ + 4);
        gap_ = false;
        gapWord_ = 0;
        elapsed_ = 0;
        return true;
    }

    bool prepareNext() {
        if (phase_ < 3) {
            unitPosition_ = static_cast<std::uint8_t>(unitPosition_ + 1);
            if (unitWords_ <= unitPosition_) {
                emitted_ = (emitted_ + 1) & 0xFFFF;
                if (emitted_ < length_) {
                    unitPosition_ = 0;
                    dataProcess();
                } else {
                    const unsigned previous = phase_;
                    phase_ = previous + 1;
                    if (previous == 0) {
                        if (u16(element_ + 10) == 0) {
                            phase_ = 2;
                        } else {
                            emitted_ = 0;
                            length_ = u16(element_);
                            unitWords_ = byte(element_ + 9);
                            unitPosition_ = 0;
                            dataInit();
                        }
                    }
                    if (phase_ == 2) {
                        const std::uint32_t after = u16(element_ + 14);
                        if (after == 0 || byte(kBase + after) == 0) {
                            ++phase_;
                        } else {
                            length_ = 1;
                            emitted_ = 0;
                            unitWords_ = byte(kBase + after);
                            unitPosition_ = 0;
                            pointer_ = kBase + after + 1;
                        }
                    }
                }
            }
        }
        if (phase_ == 3) {
            if (total_ == kNoPeriod || total_ <= elapsed_) {
                gapWord_ = 0;
                ++phase_;
                gap_ = false;
            } else {
                gap_ = true;
                gapWord_ = std::min(total_ - elapsed_, kMaxGap);
            }
        }
        if (phase_ != 4) {
            return true;
        }
        sequenceIndex_ = static_cast<std::uint8_t>(sequenceIndex_ + 1);
        if (sequenceIndex_ < sequenceCount_) {
            return elementInit();
        }
        return false;
    }

    bool advance(unsigned heldReplays) {
        bool repeat = false;
        if (stage_ == 3) {
            const unsigned minimum = code_[4];
            repeat = stageRepeat_ + 1U < minimum || stageRepeat_ < heldReplays;
        }
        if (repeat) {
            stageRepeat_ = (stageRepeat_ + 1) & 0xFF;
            cursor_ = savedCursor_;
        } else {
            ++stage_;
        }
        if (stage_ < 5 && sequenceInit() && elementInit()) {
            return true;
        }
        stage_ = 6;
        return false;
    }

    std::uint16_t nextWord() {
        std::uint32_t word = 0;
        if (gap_) {
            word = gapWord_;
        } else {
            word = u16(pointer_);
            pointer_ += 2;
        }
        elapsed_ += word & 0x7FFF;
        return static_cast<std::uint16_t>(word);
    }

    std::vector<std::uint8_t> payload_;
    std::vector<std::uint8_t> code_;
    std::vector<std::uint8_t> command_; ///< Код с 5-го байта
    unsigned toggle_ = 0;
    std::size_t block_ = 0;
    std::size_t element_ = 0;
    std::size_t cursor_ = 0;
    std::size_t savedCursor_ = 0;
    int stage_ = 2; ///< Нажатие — этап 2
    unsigned stageRepeat_ = 0;
    std::uint8_t sequenceCount_ = 0;
    std::uint8_t sequenceIndex_ = 0;

    // Состояние элемента (в прошивке — 30 байт по фиксированным смещениям).
    unsigned phase_ = 0;          ///< 0 — начало, 1 — данные, 2 — конец, 3 — пауза до периода, 4 — готово
    std::uint32_t length_ = 0;    ///< Сколько единиц (символов) в отрезке
    std::uint32_t emitted_ = 0;   ///< Сколько уже передано
    std::uint8_t unitWords_ = 0;  ///< Слов в единице
    std::uint8_t unitPosition_ = 0;
    std::size_t pointer_ = 0;     ///< Следующее слово
    std::uint32_t total_ = 0;     ///< Период кадра, мкс
    std::uint32_t elapsed_ = 0;
    bool gap_ = false;
    std::uint32_t gapWord_ = 0;
    std::uint32_t accumulator_ = 0;
    std::uint8_t remaining_ = 0;
    std::uint8_t bits_ = 0;
    std::uint8_t mask_ = 0;
    std::uint8_t bitOffset_ = 0;
};

} // namespace

Result<std::vector<std::uint8_t>> codeBytes(std::string_view code) {
    if (code.starts_with("0x") || code.starts_with("0X")) {
        code.remove_prefix(2);
    }
    if (code.empty() || code.size() % 2 != 0) {
        return makeError(ErrorCode::Format, "command code has an odd number of hex digits");
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(code.size() / 2);
    for (std::size_t i = 0; i < code.size(); i += 2) {
        unsigned value = 0;
        const auto [end, error] = std::from_chars(code.data() + i, code.data() + i + 2, value, 16);
        if (error != std::errc() || end != code.data() + i + 2) {
            return makeError(ErrorCode::Format, "command code is not a hexadecimal number");
        }
        bytes.push_back(static_cast<std::uint8_t>(value));
    }
    return bytes;
}

Result<CarrierRun> runCarrier(std::span<const std::uint8_t> protocol, std::span<const std::uint8_t> code,
                              unsigned toggleState, unsigned heldReplays) {
    // Одна запись — таблица из одного протокола, указатели в ней становятся абсолютными.
    formats::IrTable table;
    table.entries.emplace_back(protocol.begin(), protocol.end());
    if (!formats::fitsOffsets(table)) {
        return makeError(ErrorCode::Format, "the protocol is too large");
    }
    auto built = formats::buildIrProto(table);
    if (!built) {
        return std::unexpected(std::move(built.error()));
    }
    const formats::Bytes& file = *built;
    std::vector<std::uint8_t> payload(file.begin() + static_cast<std::ptrdiff_t>(kWrapper), file.end());
    std::vector<std::uint8_t> command(code.begin(), code.end());
    if (command.size() >= 2) {
        command[0] = 0;
        command[1] = 0;
    }
    try {
        Vm vm(std::move(payload), std::move(command), toggleState);
        return vm.run(heldReplays);
    } catch (const VmError& error) {
        return makeError(ErrorCode::Format, "the remote cannot execute the protocol command: %1", {error.message});
    }
}

} // namespace harmony::ir
