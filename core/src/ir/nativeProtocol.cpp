/**
 * @file nativeProtocol.cpp
 * @brief Программы протоколов для IrProto.bin и коды команд (по afterglow, GPL-3.0).
 *
 * Тайминги — не номинальные значения протоколов, а калибровка afterglow:
 * у NEC заголовок 8990 мкс вместо 9000, у бита 568 мкс. Это измеренные на
 * пульте числа; программы NEC и Samsung побайтно совпадают с работающими
 * на пульте, поэтому менять их нельзя.
 */

#include "harmony/core/ir/nativeProtocol.h"

#include "harmony/core/ir/carrierVm.h"
#include "harmony/core/ir/irEncode.h"
#include "harmony/core/ir/irSignal.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace harmony::ir {

namespace {

using formats::Bytes;

constexpr std::uint8_t kPwm = 50;
constexpr std::uint32_t kNoPeriod = 0xFFFFFFFF;
constexpr int kMaxWord = 0x7FFF;

/// Собирает запись протокола; указатели — от начала записи (перемещаемый вид).
class Body {
public:
    Body(std::uint32_t periodNs, unsigned elements) {
        u8(1);
        u32(periodNs);
        u8(kPwm);
        u8(static_cast<std::uint8_t>(elements));
        for (unsigned i = 0; i < elements; ++i) {
            u16(0);
        }
    }

    /// Заголовок элемента: символов, переключатели, период кадра, алфавит, слов на символ.
    std::size_t element(unsigned symbols, std::uint32_t periodUs, unsigned alphabet, unsigned wordsPerSymbol,
                        std::uint8_t togglePosition = 0xFF) {
        const std::size_t at = bytes_.size();
        u16(symbols);
        u8(togglePosition);
        u8(0xFF);
        u32(periodUs);
        u8(static_cast<std::uint8_t>(alphabet));
        u8(static_cast<std::uint8_t>(wordsPerSymbol));
        u16(0);
        u16(0);
        u16(0);
        return at;
    }

    /// Отрезок: число слов, затем слова.
    std::size_t run(std::initializer_list<int> pulses) {
        const std::size_t at = bytes_.size();
        u8(static_cast<std::uint8_t>(pulses.size()));
        for (const int pulse : pulses) {
            word(pulse);
        }
        return at;
    }

    /// Слова без счётчика (алфавит символов).
    std::size_t words(std::initializer_list<int> pulses) {
        return words(std::span<const int>(pulses.begin(), pulses.size()));
    }

    /// Отрезок из готовых слов (каждое — до 32767 мкс; 0 — заполнитель).
    std::size_t run(std::span<const int> pulses) {
        const std::size_t at = bytes_.size();
        u8(static_cast<std::uint8_t>(pulses.size()));
        words(pulses);
        return at;
    }

    std::size_t words(std::span<const int> pulses) {
        const std::size_t at = bytes_.size();
        for (const int pulse : pulses) {
            word(pulse);
        }
        return at;
    }

    [[nodiscard]] std::size_t size() const { return bytes_.size(); }

    void pointer(std::size_t field, std::size_t target) {
        bytes_[field] = static_cast<std::uint8_t>(target & 0xFF);
        bytes_[field + 1] = static_cast<std::uint8_t>((target >> 8) & 0xFF);
    }

    Bytes take() { return std::move(bytes_); }

private:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void u16(unsigned value) {
        u8(static_cast<std::uint8_t>(value & 0xFF));
        u8(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    }
    void u32(std::uint32_t value) {
        u16(value & 0xFFFF);
        u16(value >> 16);
    }
    /// Слово длительности: бит 15 — импульс; все длительности здесь — постоянные до 32767 мкс.
    void word(int pulse) { u16(static_cast<unsigned>(std::abs(pulse)) | (pulse > 0 ? 0x8000U : 0U)); }

    Bytes bytes_;
};

// --- Программы протоколов (afterglow ir_emit.py) -------------------------------------

/// NEC и Samsung: элемент данных (32 символа) и элемент повтора.
Bytes necBody(bool samsungLeader, bool fullFrameRepeat) {
    Body body(26315, 2);
    const std::size_t data = body.element(32, 107870, 2, 2);
    const std::size_t before = samsungLeader ? body.run({4500, -4500}) : body.run({8990, -4490});
    const std::size_t after = body.run({568});
    const std::size_t alphabet = body.words({568, -552, 568, -1662});
    // Элемент повтора остаётся и у Samsung (там повтор — полный кадр): так блок
    // совпадает с проверенным на пульте побайтно.
    const std::size_t repeat = body.element(0, kNoPeriod, 0, 0);
    const std::size_t repeatBefore = body.run({8990, -2230});
    // Пауза до периода повтора 107865 мкс: 96077 мкс тремя словами.
    const std::size_t repeatAfter = body.run({568, -kMaxWord, -kMaxWord, -(107865 - 8990 - 2230 - 568 - 2 * kMaxWord)});
    body.pointer(7, data);
    body.pointer(9, fullFrameRepeat ? data : repeat);
    body.pointer(data + 10, alphabet);
    body.pointer(data + 12, before);
    body.pointer(data + 14, after);
    body.pointer(repeat + 12, repeatBefore);
    body.pointer(repeat + 14, repeatAfter);
    return body.take();
}

/// Sony SIRC: один элемент, ширина импульса; пульт сам шлёт три кадра.
Bytes sonyBody(unsigned bits) {
    Body body(25000, 1);
    const std::size_t element = body.element(bits, 45000, 2, 2);
    const std::size_t before = body.run({2400});
    const std::size_t alphabet = body.words({-600, 600, -600, 1200});
    body.pointer(7, element);
    body.pointer(element + 10, alphabet);
    body.pointer(element + 12, before);
    return body.take();
}

/// JVC: заголовок и данные — разные элементы: повтор при удержании идёт без заголовка.
Bytes jvcBody() {
    Body body(26315, 2);
    const std::size_t leader = body.element(0, kNoPeriod, 0, 0);
    const std::size_t leaderRun = body.run({8400, -4200});
    const std::size_t data = body.element(16, 45000, 2, 2);
    const std::size_t dataRun = body.run({500});
    const std::size_t alphabet = body.words({-500, 500, -1600, 500});
    body.pointer(7, leader);
    body.pointer(9, data);
    body.pointer(leader + 12, leaderRun);
    body.pointer(data + 10, alphabet);
    body.pointer(data + 12, dataRun);
    return body.take();
}

/// RC5: 13 символов, бит переключения — второй передаваемый (его подставляет пульт).
Bytes rc5Body() {
    Body body(27778, 1);
    const std::size_t element = body.element(13, 113792, 2, 2, 1);
    const std::size_t before = body.run({889});
    const std::size_t alphabet = body.words({889, -889, -889, 889});
    body.pointer(7, element);
    body.pointer(element + 10, alphabet);
    body.pointer(element + 12, before);
    return body.take();
}

/// RC6 Mode 0: ведущий участок, старт и режим; двойной toggle; 16 бит данных.
Bytes rc6Body() {
    Body body(27778, 2);
    const std::size_t toggle = body.element(1, kNoPeriod, 2, 2, 0);
    const std::size_t before = body.run({2666, -889, 444, -888, 444, -444, 444, -444, 444});
    const std::size_t toggleAlphabet = body.words({-888, 888, 888, -888});
    const std::size_t data = body.element(16, 107000, 2, 2);
    const std::size_t dataAlphabet = body.words({-444, 444, 444, -444});
    body.pointer(7, toggle);
    body.pointer(9, data);
    body.pointer(toggle + 10, toggleAlphabet);
    body.pointer(toggle + 12, before);
    body.pointer(data + 10, dataAlphabet);
    return body.take();
}

// --- Коды команд (afterglow builder/codes.py) -----------------------------------------

/// Биты в порядке передачи.
void append(std::vector<bool>& bits, std::uint32_t value, int count, bool lsbFirst) {
    for (int i = 0; i < count; ++i) {
        const int shift = lsbFirst ? i : count - 1 - i;
        bits.push_back(((value >> shift) & 1U) != 0);
    }
}

/// Биты, старшим вперёд, с выравниванием влево до целых байтов.
std::vector<std::uint8_t> pack(const std::vector<bool>& bits) {
    std::vector<std::uint8_t> out((bits.size() + 7) / 8, 0);
    for (std::size_t i = 0; i < bits.size(); ++i) {
        if (bits[i]) {
            out[i / 8] = static_cast<std::uint8_t>(out[i / 8] | (0x80U >> (i % 8)));
        }
    }
    return out;
}

/// Код: заголовок, затем этапы.
std::vector<std::uint8_t> code(std::initializer_list<std::uint8_t> header,
                               std::initializer_list<std::span<const std::uint8_t>> parts) {
    std::vector<std::uint8_t> out{0, 0};
    out.insert(out.end(), header.begin(), header.end());
    for (const auto part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

/// Семейство программы протокола.
enum class Family { Nec, Samsung, Sony, Jvc, Rc5, Rc6 };

struct Mapping {
    std::string_view name;
    Family family;
};

constexpr std::array<Mapping, 10> kMappings = {{
    {"NEC", Family::Nec},
    {"NEC (extended)", Family::Nec},
    {"Onkyo", Family::Nec},
    {"Samsung32", Family::Samsung},
    {"Sony SIRC 12", Family::Sony},
    {"Sony SIRC 15", Family::Sony},
    {"Sony SIRC 20", Family::Sony},
    {"JVC", Family::Jvc},
    {"RC5", Family::Rc5},
    {"RC6", Family::Rc6},
}};

const Mapping* findMapping(std::string_view protocol) {
    const auto found = std::ranges::find(kMappings, protocol, &Mapping::name);
    return found == kMappings.end() ? nullptr : &*found;
}

/// Передаваемые биты команды — так же, как их собирает encode().
std::vector<bool> transmittedBits(std::string_view protocol, std::uint32_t address, std::uint32_t command) {
    std::vector<bool> bits;
    if (protocol == "NEC") {
        append(bits, address, 8, true);
        append(bits, ~address & 0xFF, 8, true);
        append(bits, command, 8, true);
        append(bits, ~command & 0xFF, 8, true);
    } else if (protocol == "NEC (extended)" || protocol == "Samsung32") {
        append(bits, address, 16, true);
        append(bits, command, 8, true);
        append(bits, ~command & 0xFF, 8, true);
    } else if (protocol == "Onkyo") {
        append(bits, address, 16, true);
        append(bits, command, 16, true);
    } else if (protocol.starts_with("Sony SIRC")) {
        const int addressBits = protocol == "Sony SIRC 12" ? 5 : protocol == "Sony SIRC 15" ? 8 : 13;
        append(bits, command, 7, true);
        append(bits, address, addressBits, true);
    } else if (protocol == "JVC") {
        append(bits, address, 8, true);
        append(bits, command, 8, true);
    } else if (protocol == "RC5") {
        // S2 (инверсный 7-й бит команды), T (0 — пульт подставит сам), адрес, команда.
        bits.push_back((command & 0x40) == 0);
        bits.push_back(false);
        append(bits, address, 5, false);
        append(bits, command & 0x3F, 6, false);
    } else if (protocol == "RC6") {
        bits.push_back(false); // бит переключения подставит пульт
        append(bits, address, 8, false);
        append(bits, command, 8, false);
    }
    return bits;
}

NativeCommand build(Family family, const std::vector<bool>& bits) {
    const std::vector<std::uint8_t> data = pack(bits);
    const std::span<const std::uint8_t> d(data);
    static constexpr std::array<std::uint8_t, 2> kOne0 = {0x01, 0x00};   // этап: 1 элемент, № 0
    static constexpr std::array<std::uint8_t, 2> kOne1 = {0x01, 0x01};   // этап: 1 элемент, № 1
    static constexpr std::array<std::uint8_t, 1> kEnd = {0x00};
    switch (family) {
    case Family::Nec:
        // Пауза 1000 мс, этап 2 — кадр данных, этап 3 — код повтора.
        return {necBody(false, false), code({0xE8, 0x03, 0x01}, {kOne0, d, kOne1, kEnd})};
    case Family::Samsung:
        // Повтор — полный кадр: данные ещё раз.
        return {necBody(true, true), code({0xE8, 0x03, 0x01}, {kOne0, d, kOne1, d})};
    case Family::Sony: {
        // Этап 2 пуст; этап 3 — кадр, минимум три раза.
        static constexpr std::array<std::uint8_t, 1> kSkip = {0x00};
        return {sonyBody(static_cast<unsigned>(bits.size())), code({0xF4, 0x01, 0x03}, {kSkip, kOne0, d, kEnd})};
    }
    case Family::Jvc: {
        // Этап 2 — заголовок и данные, этап 3 — данные без заголовка.
        static constexpr std::array<std::uint8_t, 3> kPress = {0x02, 0x00, 0x01};
        return {jvcBody(), code({0xF4, 0x01, 0x00}, {kPress, d, kOne1, d, kEnd})};
    }
    case Family::Rc5: {
        static constexpr std::array<std::uint8_t, 1> kSkip = {0x00};
        return {rc5Body(), code({0xF4, 0x01, 0x00}, {kSkip, kOne0, d, kEnd})};
    }
    case Family::Rc6: {
        static constexpr std::array<std::uint8_t, 1> kSkip = {0x00};
        static constexpr std::array<std::uint8_t, 2> kFrame = {0x02, 0x00};
        static constexpr std::array<std::uint8_t, 1> kToggle = {0x00};
        static constexpr std::array<std::uint8_t, 1> kDataElement = {0x01};
        const std::vector<bool> dataBits(bits.begin() + 1, bits.end());
        const std::vector<std::uint8_t> payload = pack(dataBits);
        return {rc6Body(), code({0xF4, 0x01, 0x00}, {kSkip, kFrame, kToggle, kDataElement, payload, kEnd})};
    }
    }
    return {};
}

/// Совпадает ли распознанное у двух сигналов.
bool sameCommand(const IrSignal& a, const IrSignal& b) {
    const auto first = decode(a);
    const auto second = decode(b);
    return first && second && first->protocol == second->protocol && first->address == second->address &&
           first->command == second->command;
}

constexpr int kFrameGapUs = 10'000; ///< Пауза длиннее — граница кадров

/// Кадры сигнала; пауза после кадра входит в него.
std::vector<std::vector<int>> framesOf(const std::vector<int>& durations) {
    std::vector<std::vector<int>> frames;
    std::vector<int> current;
    for (const int duration : durations) {
        current.push_back(duration);
        if (duration < -kFrameGapUs) {
            frames.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) {
        frames.push_back(std::move(current));
    }
    return frames;
}

/// Длительности кадров без паузы после них совпадают (допуск — как в sameSignal()).
bool sameTiming(std::span<const int> a, std::span<const int> b) {
    const auto body = [](std::span<const int> frame) {
        return !frame.empty() && frame.back() < -kFrameGapUs ? frame.first(frame.size() - 1) : frame;
    };
    a = body(a);
    b = body(b);
    if (a.empty() || a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const int p = std::abs(a[i]);
        const int q = std::abs(b[i]);
        if ((a[i] > 0) != (b[i] > 0) || std::abs(p - q) > std::max(200, std::max(p, q) / 4)) {
            return false;
        }
    }
    return true;
}

/// Распознанное, с RC5X под именем «RC5» (так его называет encodableProtocols()).
std::optional<IrDecoded> decodeNamed(const IrSignal& signal) {
    auto decoded = decode(signal);
    if (decoded && decoded->protocol == "RC5X") {
        decoded->protocol = "RC5";
    }
    return decoded;
}

/// Длительности → слова пульта: длинные разбиваются на слова того же вида по 32767 мкс.
std::vector<int> wordsOf(std::span<const int> durations) {
    std::vector<int> out;
    for (const int duration : durations) {
        int left = std::abs(duration);
        const int sign = duration > 0 ? 1 : -1;
        while (left > kMaxWord) {
            out.push_back(sign * kMaxWord);
            left -= kMaxWord;
        }
        if (left > 0) {
            out.push_back(sign * left);
        }
    }
    return out;
}

} // namespace

Result<Bytes> buildProgram(std::uint32_t carrierHz, std::span<const ProgramElement> elements) {
    if (carrierHz == 0) {
        return makeError(ErrorCode::Format, "the protocol has no carrier frequency");
    }
    if (elements.empty() || elements.size() > 0xFF) {
        return makeError(ErrorCode::Format, "a protocol needs 1 to 255 elements, not %1",
                         {std::to_string(elements.size())});
    }
    struct Prepared {
        std::vector<int> before;
        std::vector<int> after;
        std::vector<std::vector<int>> alphabet; ///< Выровнены нулями до wordsPerSymbol
        unsigned wordsPerSymbol = 0;
    };
    std::vector<Prepared> prepared;
    for (const ProgramElement& element : elements) {
        Prepared item{wordsOf(element.before), wordsOf(element.after), {}, 0};
        for (const auto& symbol : element.alphabet) {
            item.alphabet.push_back(wordsOf(symbol));
            item.wordsPerSymbol = std::max<unsigned>(item.wordsPerSymbol,
                                                     static_cast<unsigned>(item.alphabet.back().size()));
        }
        // Пустой символ пульт прочитал бы как чужие слова: заполнитель 0 он пропускает.
        item.wordsPerSymbol = std::max(item.wordsPerSymbol, 1U);
        for (auto& symbol : item.alphabet) {
            symbol.resize(item.wordsPerSymbol, 0);
        }
        const bool hasData = element.symbols > 0 && !item.alphabet.empty();
        if (item.before.size() > 0xFF || item.after.size() > 0xFF) {
            return makeError(ErrorCode::Format, "a protocol frame part is longer than 255 words");
        }
        if (item.alphabet.size() > 0xFF || item.wordsPerSymbol > 0xFF || element.symbols > 0xFFFF) {
            return makeError(ErrorCode::Format, "a protocol symbol alphabet is too large");
        }
        if (element.symbols > 0 && item.alphabet.empty()) {
            return makeError(ErrorCode::Format, "a protocol frame has data but no symbols");
        }
        if (!hasData && item.before.empty() && item.after.empty()) {
            return makeError(ErrorCode::Format, "a protocol frame has nothing to transmit");
        }
        prepared.push_back(std::move(item));
    }

    // Период несущей в нс — с отбрасыванием дробной части, как в записях Logitech (38 кГц → 26315).
    Body body(1'000'000'000U / carrierHz, static_cast<unsigned>(elements.size()));
    std::vector<std::size_t> heads;
    for (std::size_t i = 0; i < elements.size(); ++i) {
        const bool hasData = elements[i].symbols > 0;
        heads.push_back(body.element(hasData ? elements[i].symbols : 0, elements[i].periodUs,
                                     hasData ? static_cast<unsigned>(prepared[i].alphabet.size()) : 0,
                                     hasData ? prepared[i].wordsPerSymbol : 0,
                                     hasData ? elements[i].toggle : ProgramElement::kNoToggle));
    }
    for (std::size_t i = 0; i < elements.size(); ++i) {
        const Prepared& item = prepared[i];
        if (!item.before.empty()) {
            body.pointer(heads[i] + 12, body.run(item.before));
        }
        if (!item.after.empty()) {
            body.pointer(heads[i] + 14, body.run(item.after));
        }
        if (elements[i].symbols > 0) {
            const std::size_t at = body.size();
            for (const auto& symbol : item.alphabet) {
                body.words(symbol);
            }
            body.pointer(heads[i] + 10, at);
        }
        body.pointer(7 + i * 2, heads[i]);
    }
    if (body.size() > 0xFFFF) {
        return makeError(ErrorCode::Format, "the protocol is too large");
    }
    return body.take();
}

bool hasNativeProtocol(std::string_view protocol) {
    return findMapping(protocol) != nullptr;
}

Result<NativeCommand> nativeCommand(std::string_view protocol, std::uint32_t address, std::uint32_t command) {
    const Mapping* mapping = findMapping(protocol);
    if (mapping == nullptr) {
        return makeError(ErrorCode::Unsupported, "the remote does not store the protocol “%1” natively",
                         {std::string(protocol)});
    }
    // Тот же сигнал, собранный напрямую: им проверяется программа (и диапазоны адреса и команды).
    auto expected = encode(protocol, address, command);
    if (!expected) {
        return std::unexpected(std::move(expected.error()));
    }
    NativeCommand result = build(mapping->family, transmittedBits(protocol, address, command));

    // Запись должна пережить запись в файл и чтение обратно (указатели перемещаются).
    formats::IrTable table;
    table.entries.push_back(result.protocol);
    auto reread = formats::parseIrProto(formats::writeIrProto(table));
    if (!reread || reread->entries.size() != 1 || reread->entries.front() != result.protocol) {
        return makeError(ErrorCode::Format, "the generated protocol does not survive IrProto.bin");
    }

    // Исполняем, как пульт: нажатие должно дать тот же протокол, адрес и команду.
    auto pressed = runCarrier(result.protocol, result.code);
    if (!pressed) {
        return std::unexpected(std::move(pressed.error()));
    }
    const auto drift = std::abs(static_cast<long>(pressed->signal.frequency) - static_cast<long>(expected->frequency));
    if (!sameCommand(pressed->signal, *expected) || drift > 400) {
        return makeError(ErrorCode::Format, "the generated protocol command does not match “%1”",
                         {std::string(protocol)});
    }
    // Удержание добавляет повтор, а не ломает исполнение.
    const auto repeats = static_cast<unsigned>(std::ranges::count(pressed->stages, 3));
    auto held = runCarrier(result.protocol, result.code, 0, std::max(1U, repeats));
    if (!held || held->stages.size() <= pressed->stages.size()) {
        return makeError(ErrorCode::Format, "the generated protocol command does not repeat while held");
    }
    return result;
}

std::optional<IrDecoded> nativeProtocolOf(const IrSignal& signal) {
    auto decoded = decodeNamed(signal);
    if (!decoded || !hasNativeProtocol(decoded->protocol)) {
        return std::nullopt;
    }
    const auto expected = encode(decoded->protocol, decoded->address, decoded->command);
    if (!expected) {
        return std::nullopt;
    }
    // Образцы кадров: собранный сигнал и то, что шлёт программа протокола при удержании
    // (encode() не собирает, например, повтор JVC без заголовка).
    auto reference = framesOf(expected->durations);
    if (const auto native = nativeCommand(decoded->protocol, decoded->address, decoded->command)) {
        if (const auto held = runCarrier(native->protocol, native->code, 0, 1)) {
            for (auto& frame : framesOf(held->signal.durations)) {
                reference.push_back(std::move(frame));
            }
        }
    }
    for (std::vector<int> frame : framesOf(signal.durations)) {
        if (std::ranges::any_of(reference, [&](const auto& own) { return sameTiming(frame, own); })) {
            continue;
        }
        // Распознаётся кадр с паузой после него: у последнего кадра её может не быть.
        if (frame.back() > 0) {
            frame.push_back(-2 * kFrameGapUs);
        } else if (frame.back() >= -kFrameGapUs) {
            frame.back() -= 2 * kFrameGapUs;
        }
        const auto again = decodeNamed(IrSignal{signal.frequency, std::move(frame)});
        if (!again || again->protocol != decoded->protocol || again->address != decoded->address ||
            again->command != decoded->command) {
            return std::nullopt;
        }
    }
    return decoded;
}

std::string codeText(std::span<const std::uint8_t> code, unsigned protocolIndex) {
    std::string text = "0x";
    for (std::size_t i = 0; i < code.size(); ++i) {
        unsigned value = code[i];
        if (i == 0) {
            value = protocolIndex & 0xFF;
        } else if (i == 1) {
            value = (protocolIndex >> 8) & 0xFF;
        }
        char hex[3];
        std::snprintf(hex, sizeof hex, "%02X", value);
        text += hex;
    }
    return text;
}

} // namespace harmony::ir
