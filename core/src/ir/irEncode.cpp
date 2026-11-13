/**
 * @file irEncode.cpp
 * @brief Сборка ИК-сигнала по протоколу: длительности импульсов и пауз.
 *
 * Тайминги — стандартные (как в Arduino IRremote); распознавание (irDecode.cpp)
 * узнаёт собранное с тем же адресом и командой — это проверяют тесты.
 */

#include "harmony/core/ir/irEncode.h"

#include <algorithm>
#include <cstdlib>

namespace harmony::ir {

namespace {

/// Сборщик длительностей: соседние импульсы (паузы) сливаются.
class Builder {
public:
    void mark(int us) { add(us); }
    void space(int us) { add(-us); }

    /// Добивает паузой до длины кадра @p frameUs от @p startUs (не меньше @p minGap).
    void gapTo(long startUs, long frameUs, int minGap) {
        const long used = elapsed() - startUs;
        space(static_cast<int>(std::max<long>(minGap, frameUs - used)));
    }

    [[nodiscard]] long elapsed() const {
        long sum = 0;
        for (const int d : durations_) {
            sum += std::abs(d);
        }
        return sum;
    }

    std::vector<int> take() { return std::move(durations_); }

private:
    void add(int us) {
        if (us == 0) {
            return;
        }
        if (!durations_.empty() && (durations_.back() > 0) == (us > 0)) {
            durations_.back() += us;
        } else {
            durations_.push_back(us);
        }
    }

    std::vector<int> durations_;
};

/// Биты числа: lsbFirst — младший первым.
std::vector<bool> bitsOf(std::uint32_t value, int count, bool lsbFirst) {
    std::vector<bool> bits;
    for (int i = 0; i < count; ++i) {
        const int shift = lsbFirst ? i : count - 1 - i;
        bits.push_back(((value >> shift) & 1U) != 0);
    }
    return bits;
}

void append(std::vector<bool>& bits, std::uint32_t value, int count, bool lsbFirst) {
    const auto more = bitsOf(value, count, lsbFirst);
    bits.insert(bits.end(), more.begin(), more.end());
}

/// Кодирование длиной паузы: импульс bitMark, пауза zero/one; в конце стоп-импульс.
void pulseDistance(Builder& out, const std::vector<bool>& bits, int bitMark, int zero, int one) {
    for (const bool bit : bits) {
        out.mark(bitMark);
        out.space(bit ? one : zero);
    }
    out.mark(bitMark);
}

// --- Протоколы -------------------------------------------------------------------

std::vector<int> nec(const std::vector<bool>& bits, int leadMark, int leadSpace, int bitMark,
                     int zero, int one, bool repeatCode) {
    Builder out;
    out.mark(leadMark);
    out.space(leadSpace);
    pulseDistance(out, bits, bitMark, zero, one);
    out.gapTo(0, 108000, 20000);
    if (repeatCode) {
        // Код повтора NEC: приёмник понимает, что кнопку держат.
        const long start = out.elapsed();
        out.mark(leadMark);
        out.space(leadSpace / 2);
        out.mark(bitMark);
        out.gapTo(start, 108000, 20000);
    }
    return out.take();
}

std::vector<int> samsung(const std::vector<bool>& bits) {
    Builder out;
    out.mark(4500);
    out.space(4500);
    pulseDistance(out, bits, 560, 560, 1690);
    out.gapTo(0, 108000, 20000);
    return out.take();
}

std::vector<int> rca(const std::vector<bool>& bits) {
    Builder out;
    out.mark(4000);
    out.space(4000);
    pulseDistance(out, bits, 500, 1000, 2000);
    out.gapTo(0, 64000, 8000);
    return out.take();
}

std::vector<int> kaseikyo(const std::vector<bool>& bits) {
    Builder out;
    out.mark(3456);
    out.space(1728);
    pulseDistance(out, bits, 432, 432, 1296);
    out.gapTo(0, 130000, 74000);
    return out.take();
}

/// Sony: три одинаковых кадра с периодом 45 мс (так их шлёт родной пульт).
std::vector<int> sony(const std::vector<bool>& bits) {
    Builder out;
    for (int frame = 0; frame < 3; ++frame) {
        const long start = out.elapsed();
        out.mark(2400);
        out.space(600);
        for (std::size_t i = 0; i < bits.size(); ++i) {
            out.mark(bits[i] ? 1200 : 600);
            if (i + 1 < bits.size()) {
                out.space(600);
            }
        }
        out.gapTo(start, 45000, 10000);
    }
    return out.take();
}

/// Denon и Sharp: кадр, затем кадр с инвертированной командой и служебными битами.
std::vector<int> sharpFamily(std::uint32_t address, std::uint32_t command, bool sharp) {
    Builder out;
    for (int frame = 0; frame < 2; ++frame) {
        std::vector<bool> bits;
        append(bits, address, 5, true);
        const std::uint32_t cmd = frame == 0 ? command : (~command & 0xFF);
        append(bits, cmd, 8, true);
        // Sharp: бит расширения 1 и проверочный 0; Denon: 00. Во втором кадре — инверсия.
        const bool expansion = sharp ? frame == 0 : frame != 0;
        const bool check = frame != 0;
        bits.push_back(expansion);
        bits.push_back(check);
        pulseDistance(out, bits, 300, 750, 1800);
        out.space(frame == 0 ? 40000 : 45000);
    }
    return out.take();
}

/// Манчестерский код по уровням полутактов → длительности (сигнал начинается с импульса).
void levelsToDurations(Builder& out, const std::vector<bool>& levels, int unit) {
    bool started = false;
    for (const bool level : levels) {
        if (!started && !level) {
            continue; // пауза до первого импульса не передаётся
        }
        started = true;
        if (level) {
            out.mark(unit);
        } else {
            out.space(unit);
        }
    }
}

std::vector<int> rc5(std::uint32_t address, std::uint32_t command) {
    std::vector<bool> bits{true, (command & 0x40) == 0, false}; // S1, S2 (инверсный 7-й бит), T
    append(bits, address, 5, false);
    append(bits, command & 0x3F, 6, false);
    std::vector<bool> levels;
    for (const bool bit : bits) {
        // RC5: «1» — пауза, затем импульс; «0» — наоборот.
        levels.push_back(!bit);
        levels.push_back(bit);
    }
    Builder out;
    levelsToDurations(out, levels, 889);
    out.gapTo(0, 113778, 20000);
    return out.take();
}

std::vector<int> rc6(std::uint32_t address, std::uint32_t command) {
    Builder out;
    out.mark(2666);
    out.space(889);
    std::vector<bool> levels;
    auto bit = [&levels](bool value, int width) {
        // RC6: «1» — импульс, затем пауза.
        levels.insert(levels.end(), static_cast<std::size_t>(width), value);
        levels.insert(levels.end(), static_cast<std::size_t>(width), !value);
    };
    bit(true, 1); // старт
    for (int i = 0; i < 3; ++i) {
        bit(false, 1); // режим 0
    }
    bit(false, 2); // бит переключения — двойной ширины
    for (const bool value : bitsOf((address << 8) | command, 16, false)) {
        bit(value, 1);
    }
    for (const bool level : levels) {
        if (level) {
            out.mark(444);
        } else {
            out.space(444);
        }
    }
    out.gapTo(0, 107000, 20000);
    return out.take();
}

bool fits(std::uint32_t value, int bits) {
    return bits >= 32 || value >> bits == 0;
}

} // namespace

const std::vector<EncodableProtocol>& encodableProtocols() {
    static const std::vector<EncodableProtocol> protocols = {
        {"NEC", 8, 8, 38000},          {"NEC (extended)", 16, 8, 38000},
        {"Samsung32", 16, 8, 38000},   {"Sony SIRC 12", 5, 7, 40000},
        {"Sony SIRC 15", 8, 7, 40000}, {"Sony SIRC 20", 13, 7, 40000},
        {"RC5", 5, 7, 36000},          {"RC6", 8, 8, 36000},
        {"Panasonic", 12, 8, 37000},   {"LG", 8, 16, 38000},
        {"JVC", 8, 8, 38000},          {"Onkyo", 16, 16, 38000},
        {"Denon", 5, 8, 38000},        {"Sharp", 5, 8, 38000},
        {"RCA", 4, 8, 56000},
    };
    return protocols;
}

Result<IrSignal> encode(std::string_view protocol, std::uint32_t address, std::uint32_t command) {
    const std::string name = protocol == "RC5X" ? std::string("RC5") : std::string(protocol);
    const auto& all = encodableProtocols();
    const auto it = std::ranges::find(all, name, &EncodableProtocol::name);
    if (it == all.end()) {
        return makeError(ErrorCode::Unsupported, "cannot build a signal for the protocol “%1”",
                         {name});
    }
    if (!fits(address, it->addressBits)) {
        return makeError(ErrorCode::Format, "the address does not fit into %1 bits",
                         {std::to_string(it->addressBits)});
    }
    if (!fits(command, it->commandBits)) {
        return makeError(ErrorCode::Format, "the command does not fit into %1 bits",
                         {std::to_string(it->commandBits)});
    }

    IrSignal signal;
    signal.frequency = it->frequency;
    std::vector<bool> bits;
    if (name == "NEC" || name == "NEC (extended)") {
        const std::uint32_t high = name == "NEC" ? (~address & 0xFF) : (address >> 8);
        append(bits, address & 0xFF, 8, true);
        append(bits, high, 8, true);
        append(bits, command, 8, true);
        append(bits, ~command & 0xFF, 8, true);
        signal.durations = nec(bits, 9000, 4500, 560, 560, 1690, true);
    } else if (name == "Onkyo") {
        append(bits, address, 16, true);
        append(bits, command, 16, true);
        signal.durations = nec(bits, 9000, 4500, 560, 560, 1690, true);
    } else if (name == "LG") {
        append(bits, address, 8, false);
        append(bits, command, 16, false);
        const std::uint32_t sum =
            ((command >> 12) + (command >> 8) + (command >> 4) + command) & 0xF;
        append(bits, sum, 4, false);
        signal.durations = nec(bits, 9000, 4500, 560, 560, 1690, true);
    } else if (name == "JVC") {
        append(bits, address, 8, true);
        append(bits, command, 8, true);
        signal.durations = nec(bits, 8400, 4200, 525, 525, 1575, false);
    } else if (name == "Samsung32") {
        append(bits, address, 16, true);
        append(bits, command, 8, true);
        append(bits, ~command & 0xFF, 8, true);
        signal.durations = samsung(bits);
    } else if (name == "RCA") {
        append(bits, address, 4, false);
        append(bits, command, 8, false);
        append(bits, ~address & 0xF, 4, false);
        append(bits, ~command & 0xFF, 8, false);
        signal.durations = rca(bits);
    } else if (name == "Panasonic") {
        // Kaseikyo: производитель 0x2002, его чётность, 12 бит адреса, команда, проверка.
        constexpr std::uint32_t kVendor = 0x2002;
        const std::uint32_t vendorParity =
            ((kVendor ^ (kVendor >> 8)) ^ ((kVendor ^ (kVendor >> 8)) >> 4)) & 0xF;
        append(bits, kVendor, 16, true);
        const std::uint32_t byte2 = vendorParity | ((address & 0xF) << 4);
        const std::uint32_t byte3 = (address >> 4) & 0xFF;
        append(bits, byte2, 8, true);
        append(bits, byte3, 8, true);
        append(bits, command, 8, true);
        append(bits, (byte2 ^ byte3 ^ command) & 0xFF, 8, true);
        signal.durations = kaseikyo(bits);
    } else if (name.starts_with("Sony SIRC")) {
        append(bits, command, 7, true);
        append(bits, address, it->addressBits, true);
        signal.durations = sony(bits);
    } else if (name == "RC5") {
        signal.durations = rc5(address, command);
    } else if (name == "RC6") {
        signal.durations = rc6(address, command);
    } else if (name == "Denon" || name == "Sharp") {
        signal.durations = sharpFamily(address, command, name == "Sharp");
    }
    return signal;
}

Result<IrSignal> encodeBits(std::string_view protocol, const std::vector<bool>& bits) {
    const auto sample = encode(protocol, 0, 0);
    if (!sample) return std::unexpected(sample.error());
    const auto shape = decode(*sample);
    if (!shape || bits.size() != shape->bits.size())
        return makeError(ErrorCode::Format, "the data must contain %1 bits",
                         {shape ? std::to_string(shape->bits.size()) : "0"});
    const auto value = [&](std::size_t offset, std::size_t count, bool lsb) {
        std::uint32_t result = 0;
        for (std::size_t i = 0; i < count; ++i)
            if (bits[offset + i]) result |= 1U << (lsb ? i : count - 1 - i);
        return result;
    };
    std::uint32_t address = 0, command = 0;
    if (protocol == "NEC" || protocol == "NEC (extended)" || protocol == "Onkyo" || protocol == "Samsung32") {
        address = value(0, protocol == "NEC" ? 8 : 16, true);
        command = value(16, protocol == "Onkyo" ? 16 : 8, true);
    } else if (protocol == "JVC") {
        address = value(0, 8, true); command = value(8, 8, true);
    } else if (protocol.starts_with("Sony SIRC")) {
        address = value(7, bits.size() - 7, true); command = value(0, 7, true);
    } else if (protocol == "Panasonic") {
        address = value(20, 12, true); command = value(32, 8, true);
    } else if (protocol == "LG") {
        address = value(0, 8, false); command = value(8, 16, false);
    } else if (protocol == "RCA") {
        address = value(0, 4, false); command = value(4, 8, false);
    } else if (protocol == "Denon" || protocol == "Sharp") {
        address = value(0, 5, true); command = value(5, 8, true);
    } else if (protocol == "RC5" || protocol == "RC5X") {
        address = value(3, 5, false); command = value(8, 6, false) | (bits[1] ? 0U : 64U);
    } else if (protocol == "RC6") {
        address = value(0, 8, false); command = value(8, 8, false);
    }
    auto result = encode(protocol, address, command);
    if (!result) return result;
    const auto decoded = decode(*result);
    auto canonical = bits;
    if (protocol == "RC5" || protocol == "RC5X") canonical[2] = false; // toggle is generated by the remote
    if (!decoded || decoded->bits != canonical)
        return makeError(ErrorCode::Format, "the data does not match the selected protocol (check parity and bit order)");
    return result;
}

} // namespace harmony::ir
