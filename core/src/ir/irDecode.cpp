/**
 * @file irDecode.cpp
 * @brief Распознавание ИК-протоколов по длительностям импульсов и пауз.
 *
 * Разбирается первый кадр сигнала. Допуск длительностей — четверть
 * (не меньше 120 мкс), как у обычных декодеров ИК. После последнего бита
 * кадр должен кончаться: иначе более длинный кадр чужого протокола
 * (Sanyo 42 бита, кондиционер 144 бита) узнавался бы по своему началу.
 */

#include "harmony/core/ir/irSignal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <span>

namespace harmony::ir {

namespace {

using Durations = std::span<const int>;

/// Пауза длиннее — граница кадров (как в nativeProtocol.cpp и irSignal.cpp).
constexpr int kFrameGapUs = 10'000;

int tolerance(int expected) { return std::max(expected / 4, 120); }

/// Длительность близка к образцу.
bool near(int value, int expected) { return std::abs(std::abs(value) - expected) <= tolerance(expected); }

bool isMark(Durations d, std::size_t at) { return at < d.size() && d[at] > 0; }
bool isSpace(Durations d, std::size_t at) { return at < d.size() && d[at] < 0; }

/// Кадр кончается в at: сигнал кончился или пауза длиннее самой длинной паузы внутри кадра.
bool frameEnds(Durations d, std::size_t at, int longestSpace) {
    return at >= d.size() || (d[at] < 0 && -d[at] > longestSpace + tolerance(longestSpace));
}

/// После count бит с кодированием длиной паузы (с from) — стоп-импульс и конец кадра.
bool stopsAfter(Durations d, std::size_t from, std::size_t count, int bitMark, int longestSpace) {
    const std::size_t stop = from + 2 * count;
    return isMark(d, stop) && near(d[stop], bitMark) && frameEnds(d, stop + 1, longestSpace);
}

/// Число из битов: lsbFirst — первый бит младший.
std::uint32_t value(const std::vector<bool>& bits, std::size_t from, std::size_t count, bool lsbFirst) {
    std::uint32_t result = 0;
    for (std::size_t i = 0; i < count && from + i < bits.size(); ++i) {
        if (lsbFirst) {
            result |= static_cast<std::uint32_t>(bits[from + i]) << i;
        } else {
            result = (result << 1) | static_cast<std::uint32_t>(bits[from + i]);
        }
    }
    return result;
}

std::string hex(std::uint32_t number, int digits) {
    char text[16];
    std::snprintf(text, sizeof text, "0x%0*X", digits, number);
    return text;
}

/**
 * @brief Биты с кодированием длиной паузы: импульс bitMark, пауза zero → 0, one → 1.
 *
 * Читает, пока пары узнаются (не больше maxBits); последний импульс без
 * узнаваемой паузы — стоп-бит.
 */
std::vector<bool> pulseDistance(Durations d, std::size_t from, int bitMark, int zero, int one, std::size_t maxBits) {
    std::vector<bool> bits;
    for (std::size_t at = from; bits.size() < maxBits && isMark(d, at) && near(d[at], bitMark) && isSpace(d, at + 1);
         at += 2) {
        if (near(d[at + 1], zero)) {
            bits.push_back(false);
        } else if (near(d[at + 1], one)) {
            bits.push_back(true);
        } else {
            break;
        }
    }
    return bits;
}

/// Кадр семейства NEC: ведущий импульс ~9000 (JVC — 8400), пауза ~4500; дальше по числу бит.
std::optional<IrDecoded> necFrame(Durations d) {
    if (!isMark(d, 0) || !near(d[0], 8800) || !isSpace(d, 1) || !near(d[1], 4400)) {
        return std::nullopt;
    }
    // JVC: бит — импульс 525, паузы 525/1575; NEC и LG — 560, 560/1690.
    auto bits = pulseDistance(d, 2, 560, 560, 1690, 32);
    if (!stopsAfter(d, 2, bits.size(), 560, 1690)) {
        return std::nullopt;
    }
    if (bits.size() == 32) {
        const auto b0 = value(bits, 0, 8, true);
        const auto b1 = value(bits, 8, 8, true);
        const auto b2 = value(bits, 16, 8, true);
        const auto b3 = value(bits, 24, 8, true);
        if (b3 == (~b2 & 0xFF)) {
            if (b1 == (~b0 & 0xFF)) {
                return IrDecoded{"NEC", b0, b2, bits};
            }
            return IrDecoded{"NEC (extended)", b0 | (b1 << 8), b2, bits};
        }
        // Команда без проверочного байта — Onkyo (16 бит адреса, 16 бит команды).
        return IrDecoded{"Onkyo", b0 | (b1 << 8), b2 | (b3 << 8), bits};
    }
    if (bits.size() == 28) {
        // LG: 8 бит адреса, 16 бит команды, 4 бита суммы — старшим битом вперёд.
        return IrDecoded{"LG", value(bits, 0, 8, false), value(bits, 8, 16, false), bits, false};
    }
    if (bits.size() == 16) {
        return IrDecoded{"JVC", value(bits, 0, 8, true), value(bits, 8, 8, true), bits};
    }
    return std::nullopt;
}

/**
 * @brief Семейство NEC по первому кадру, если дальше нет полного кадра с другой командой.
 *
 * Pioneer шлёт на кнопку два кадра NEC с разными адресами; выдать первый за всю
 * команду — потерять второй. Кадры повтора (9000/2250) и тот же кадр ещё раз не мешают.
 */
std::optional<IrDecoded> decodeNecFamily(Durations d) {
    auto first = necFrame(d);
    if (!first) {
        return std::nullopt;
    }
    for (std::size_t at = 0; at + 1 < d.size(); ++at) {
        if (d[at] >= -kFrameGapUs) {
            continue;
        }
        const auto other = necFrame(d.subspan(at + 1));
        if (other && (other->protocol != first->protocol || other->address != first->address ||
                      other->command != first->command)) {
            return std::nullopt;
        }
    }
    return first;
}

/// Samsung32 (4500/4500, 32 бита) и RCA (4000/4000, 24 бита).
std::optional<IrDecoded> decodeSamsungRca(Durations d) {
    if (!isMark(d, 0) || !isSpace(d, 1) || !near(d[0], 4300) || !near(d[1], 4300)) {
        return std::nullopt;
    }
    auto bits = pulseDistance(d, 2, 560, 560, 1690, 32);
    if (bits.size() == 32 && stopsAfter(d, 2, 32, 560, 1690)) {
        return IrDecoded{"Samsung32", value(bits, 0, 16, true), value(bits, 16, 8, true), bits};
    }
    bits = pulseDistance(d, 2, 500, 1000, 2000, 24);
    if (bits.size() == 24 && stopsAfter(d, 2, 24, 500, 2000)) {
        // RCA: 4 бита адреса, 8 бит команды, затем их инверсия — старшим битом вперёд.
        return IrDecoded{"RCA", value(bits, 0, 4, false), value(bits, 4, 8, false), bits, false};
    }
    return std::nullopt;
}

/**
 * @brief Samsung36 (BD-плееры Samsung): 4500/4500, 16 бит адреса, разрыв 500/4500, ещё 20 бит.
 *
 * Бит — импульс 500, паузы 500/1500, младшим битом вперёд. Вторая часть: 4 бита
 * расширения, 8 бит команды и её инверсия. Расширение (обычно 0) идёт в старшие
 * биты команды, чтобы его не потерять.
 */
std::optional<IrDecoded> decodeSamsung36(Durations d) {
    if (!isMark(d, 0) || !isSpace(d, 1) || !near(d[0], 4500) || !near(d[1], 4500)) {
        return std::nullopt;
    }
    auto bits = pulseDistance(d, 2, 500, 500, 1500, 16);
    constexpr std::size_t kGap = 2 + 2 * 16;
    if (bits.size() != 16 || !isMark(d, kGap) || !near(d[kGap], 500) || !isSpace(d, kGap + 1) ||
        !near(d[kGap + 1], 4500)) {
        return std::nullopt;
    }
    const auto tail = pulseDistance(d, kGap + 2, 500, 500, 1500, 20);
    if (tail.size() != 20 || !stopsAfter(d, kGap + 2, 20, 500, 4500)) {
        return std::nullopt;
    }
    bits.insert(bits.end(), tail.begin(), tail.end());
    const std::uint32_t command = value(bits, 20, 8, true);
    if (value(bits, 28, 8, true) != (~command & 0xFF)) {
        return std::nullopt;
    }
    return IrDecoded{"Samsung36", value(bits, 0, 16, true), command | (value(bits, 16, 4, true) << 8), bits};
}

/**
 * @brief Konka (ТВ Konka): 3000/3000, 8 бит адреса и 8 бит команды старшим битом вперёд.
 *
 * Бит — импульс 500, паузы 1500/2500. После данных — импульс 500, пауза 4000
 * и стоп-импульс 500.
 */
std::optional<IrDecoded> decodeKonka(Durations d) {
    if (!isMark(d, 0) || !isSpace(d, 1) || !near(d[0], 3000) || !near(d[1], 3000)) {
        return std::nullopt;
    }
    const auto bits = pulseDistance(d, 2, 500, 1500, 2500, 16);
    constexpr std::size_t kTrailer = 2 + 2 * 16;
    if (bits.size() != 16 || !isMark(d, kTrailer) || !near(d[kTrailer], 500) ||
        !isSpace(d, kTrailer + 1) || !near(d[kTrailer + 1], 4000) ||
        !stopsAfter(d, kTrailer + 2, 0, 500, 4000)) {
        return std::nullopt;
    }
    return IrDecoded{"Konka", value(bits, 0, 8, false), value(bits, 8, 8, false), bits, false};
}

/// Kaseikyo (Panasonic и другие): 3456/1728, 48 бит; первые 16 — код производителя.
std::optional<IrDecoded> decodeKaseikyo(Durations d) {
    if (!isMark(d, 0) || !isSpace(d, 1) || !near(d[0], 3456) || !near(d[1], 1728)) {
        return std::nullopt;
    }
    auto bits = pulseDistance(d, 2, 432, 432, 1296, 48);
    // Кадр кондиционера Mitsubishi Electric начинается как Kaseikyo 0xCB23, но в нём 144 бита.
    if (bits.size() != 48 || !stopsAfter(d, 2, 48, 432, 1296)) {
        return std::nullopt;
    }
    const std::uint32_t vendor = value(bits, 0, 16, true);
    std::string name = "Kaseikyo";
    switch (vendor) {
    case 0x2002: name = "Panasonic"; break;
    case 0x3254: name = "Denon (Kaseikyo)"; break;
    case 0x0103: name = "JVC (Kaseikyo)"; break;
    case 0x5AAA: name = "Sharp (Kaseikyo)"; break;
    case 0xCB23: name = "Mitsubishi (Kaseikyo)"; break;
    default: break;
    }
    // После производителя: 4 бита чётности, 12 бит адреса, 8 бит команды, 8 бит проверки.
    return IrDecoded{name, value(bits, 20, 12, true), value(bits, 32, 8, true), bits};
}

/// Sony SIRC: ведущий 2400/600; бит — импульс 1200 (1) или 600 (0), пауза 600.
std::optional<IrDecoded> decodeSony(Durations d) {
    if (!isMark(d, 0) || !near(d[0], 2400) || !isSpace(d, 1) || !near(d[1], 600)) {
        return std::nullopt;
    }
    std::vector<bool> bits;
    for (std::size_t at = 2; bits.size() < 20 && isMark(d, at); at += 2) {
        if (near(d[at], 1200)) {
            bits.push_back(true);
        } else if (near(d[at], 600)) {
            bits.push_back(false);
        } else {
            break;
        }
        // Пауза после последнего бита — длинная (до повтора).
        if (!isSpace(d, at + 1) || !near(d[at + 1], 600)) {
            break;
        }
    }
    // Стоп-импульса у SIRC нет: кадр кончается паузой после импульса последнего бита.
    if ((bits.size() != 12 && bits.size() != 15 && bits.size() != 20) || !frameEnds(d, 2 * bits.size() + 1, 600)) {
        return std::nullopt;
    }
    // 7 бит команды, затем адрес (5, 8 или 13 бит) — младшим битом вперёд.
    return IrDecoded{"Sony SIRC " + std::to_string(bits.size()), value(bits, 7, bits.size() - 7, true),
                     value(bits, 0, 7, true), bits};
}

/// Denon и Sharp: без ведущего импульса; импульс 300, паузы 750/1800; 15 бит.
std::optional<IrDecoded> decodeSharp(Durations d) {
    auto bits = pulseDistance(d, 0, 300, 750, 1800, 15);
    // Mitsubishi (16 бит, паузы 900/2100) укладывается в допуск Sharp, но на бит длиннее.
    if (bits.size() != 15 || !stopsAfter(d, 0, 15, 300, 1800)) {
        return std::nullopt;
    }
    // 5 бит адреса, 8 бит команды и два служебных: у Denon — 00 в первом кадре,
    // у Sharp — бит расширения 1 и проверочный 0.
    const char* name = bits[13] && !bits[14] ? "Sharp" : "Denon";
    return IrDecoded{name, value(bits, 0, 5, true), value(bits, 5, 8, true), bits};
}

/**
 * @brief Уровни сигнала по полутактам: длительность → 1…maxUnits одинаковых уровней.
 * @return Пусто — длительность не кратна такту.
 */
std::vector<bool> halfBits(Durations d, std::size_t from, int unit, int maxUnits) {
    std::vector<bool> levels;
    for (std::size_t at = from; at < d.size(); ++at) {
        const int length = std::abs(d[at]);
        int units = 0;
        for (int n = 1; n <= maxUnits; ++n) {
            if (near(length, unit * n)) {
                units = n;
                break;
            }
        }
        if (units == 0) {
            break; // длинная пауза — конец кадра
        }
        levels.insert(levels.end(), static_cast<std::size_t>(units), d[at] > 0);
    }
    return levels;
}

/// RC5 (Philips): манчестерский код, такт 889 мкс; 14 бит, «1» — пауза, затем импульс.
std::optional<IrDecoded> decodeRc5(Durations d) {
    if (!isMark(d, 0)) {
        return std::nullopt;
    }
    auto levels = halfBits(d, 0, 889, 2);
    levels.insert(levels.begin(), false); // первый стартовый бит начинается с паузы
    if (levels.size() % 2 != 0) {
        levels.push_back(false); // последняя пауза слилась с паузой после кадра
    }
    if (levels.size() != 28) {
        return std::nullopt;
    }
    std::vector<bool> bits;
    for (std::size_t i = 0; i < levels.size(); i += 2) {
        if (levels[i] == levels[i + 1]) {
            return std::nullopt;
        }
        bits.push_back(levels[i + 1]);
    }
    if (!bits[0]) {
        return std::nullopt;
    }
    // S1 S2 T A4…A0 C5…C0; инверсный S2 — седьмой бит команды (RC5X).
    const std::uint32_t command = value(bits, 8, 6, false) | (bits[1] ? 0U : 0x40U);
    return IrDecoded{bits[1] ? "RC5" : "RC5X", value(bits, 3, 5, false), command, bits, false};
}

/// RC6 (Philips): ведущий 2666/889, такт 444; старт, 3 бита режима, двойной бит переключения, 12–32 бита данных.
std::optional<IrDecoded> decodeRc6(Durations d) {
    if (!isMark(d, 0) || !near(d[0], 2666) || !isSpace(d, 1) || !near(d[1], 889)) {
        return std::nullopt;
    }
    auto levels = halfBits(d, 2, 444, 6);
    std::size_t at = 0;
    std::vector<bool> bits;
    // Бит RC6: «1» — импульс, затем пауза; ширина полубита — 1 такт (бит переключения — 2).
    auto readBit = [&](std::size_t width) -> std::optional<bool> {
        if (at + 2 * width > levels.size()) {
            // Последний полубит-пауза мог слиться с паузой после кадра.
            if (at + width <= levels.size() && levels[at]) {
                levels.resize(at + 2 * width, false);
            } else {
                return std::nullopt;
            }
        }
        for (std::size_t i = 1; i < width; ++i) {
            if (levels[at + i] != levels[at] || levels[at + width + i] != levels[at + width]) {
                return std::nullopt;
            }
        }
        if (levels[at] == levels[at + width]) {
            return std::nullopt;
        }
        const bool bit = levels[at];
        at += 2 * width;
        return bit;
    };
    const auto start = readBit(1);
    if (!start || !*start) {
        return std::nullopt;
    }
    std::uint32_t mode = 0;
    for (int i = 0; i < 3; ++i) {
        const auto bit = readBit(1);
        if (!bit) {
            return std::nullopt;
        }
        mode = (mode << 1) | static_cast<std::uint32_t>(*bit);
    }
    if (!readBit(2)) {
        return std::nullopt;
    }
    while (bits.size() < 32) {
        const auto bit = readBit(1);
        if (!bit) {
            break;
        }
        bits.push_back(*bit);
    }
    // После последнего бита — только пауза (короткая пауза после кадра попадает в levels).
    if (std::find(levels.begin() + static_cast<std::ptrdiff_t>(at), levels.end(), true) != levels.end()) {
        return std::nullopt;
    }
    if (mode == 0 && bits.size() == 16) {
        return IrDecoded{"RC6", value(bits, 0, 8, false), value(bits, 8, 8, false), bits, false};
    }
    if (mode == 6 && bits.size() == 32) {
        // RC6A: 16 бит кода производителя (старший бит — признак), 8 бит адреса, 8 бит команды.
        return IrDecoded{"RC6A", value(bits, 0, 24, false) & 0x7FFFFF, value(bits, 24, 8, false), bits, false};
    }
    if (bits.size() >= 12) {
        // Прочие режимы и длины («RC6-6-20» — Sky/Sky+): последние 8 бит — команда, остальное — адрес.
        const std::size_t addressBits = bits.size() - 8;
        return IrDecoded{"RC6-" + std::to_string(mode) + "-" + std::to_string(bits.size()),
                         value(bits, 0, addressBits, false), value(bits, addressBits, 8, false), bits, false};
    }
    return std::nullopt;
}

} // namespace

std::string bitsToHex(const std::vector<bool>& bits) {
    if (bits.empty()) {
        return {};
    }
    std::string out = "0x";
    const std::size_t pad = (4 - bits.size() % 4) % 4;
    int nibble = 0;
    int count = 0;
    for (std::size_t i = 0; i < pad + bits.size(); ++i) {
        nibble = (nibble << 1) | (i >= pad && bits[i - pad] ? 1 : 0);
        if (++count == 4) {
            out += "0123456789ABCDEF"[nibble];
            nibble = 0;
            count = 0;
        }
    }
    return out;
}

std::string IrDecoded::text() const {
    const int addressDigits = address > 0xFFFF ? 6 : address > 0xFF ? 4 : 2;
    return protocol + " · address " + hex(address, addressDigits) + ", command " + hex(command, command > 0xFF ? 4 : 2);
}

std::string IrDecoded::rawData() const {
    return bitsToHex(lsbFirst ? std::vector<bool>(bits.rbegin(), bits.rend()) : bits);
}

std::optional<IrDecoded> decode(const IrSignal& signal) {
    const Durations d(signal.durations);
    if (d.size() < 4) {
        return std::nullopt;
    }
    for (const auto decoder : {decodeNecFamily, decodeSamsungRca, decodeSamsung36, decodeKonka, decodeKaseikyo,
                               decodeSony, decodeRc6, decodeRc5, decodeSharp}) {
        if (auto decoded = decoder(d)) {
            return decoded;
        }
    }
    return std::nullopt;
}

} // namespace harmony::ir
