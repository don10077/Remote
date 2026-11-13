/**
 * @file irProtocol.cpp
 * @brief Сборка ИК-сигнала из протокола IrProto.bin и данных команды.
 *
 * Устройство записей разобрано по проекту harmony900tools (Martin Wagner).
 */

#include "harmony/core/ir/irProtocol.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <string>

namespace harmony::ir {

namespace {

constexpr std::uint32_t kNoInterval = 0xFFFFFFFF; ///< Период кадра не задан
constexpr int kMaxWord = 0x7FFF;                  ///< Наибольшая длительность одного слова, мкс
/// Предел длительностей сигнала — как kMaxWords у исполнителя программ (carrierVm.cpp):
/// без него 255 кадров по 65535 бит из XML дали бы сотни мегабайт.
constexpr std::size_t kMaxDurations = 10000;

std::uint32_t read16(std::span<const std::uint8_t> data, std::size_t at) {
    return data[at] | (static_cast<std::uint32_t>(data[at + 1]) << 8);
}

std::uint32_t read32(std::span<const std::uint8_t> data, std::size_t at) {
    return read16(data, at) | (read16(data, at + 2) << 16);
}

/// Биты байтов, старший бит — первым.
std::vector<bool> bitsOf(std::span<const std::uint8_t> bytes) {
    std::vector<bool> bits;
    bits.reserve(bytes.size() * 8);
    for (const std::uint8_t byte : bytes) {
        for (int i = 7; i >= 0; --i) {
            bits.push_back(((byte >> i) & 1) != 0);
        }
    }
    return bits;
}

/// Слово тайминга: +импульс / −пауза.
int timing(std::uint32_t word) {
    const int length = static_cast<int>(word & kMaxWord);
    return (word & 0x8000) != 0 ? length : -length;
}

/// Секция таймингов протокола.
struct Section {
    std::size_t bitCount = 0;
    std::uint32_t interval = kNoInterval;
    std::vector<int> start;    ///< Начало кадра
    std::vector<int> zero;     ///< Бит 0
    std::vector<int> one;      ///< Бит 1
    std::vector<int> end;      ///< Конец кадра
};

/// Блок «число слов, слова» по смещению; 0 — блока нет.
Result<std::vector<int>> readBlock(std::span<const std::uint8_t> entry, std::size_t at) {
    std::vector<int> out;
    if (at == 0) {
        return out;
    }
    if (at >= entry.size() || at + 1 + entry[at] * 2U > entry.size()) {
        return makeError(ErrorCode::Format, "protocol timings exceed the entry");
    }
    for (std::size_t i = 0; i < entry[at]; ++i) {
        out.push_back(timing(read16(entry, at + 1 + i * 2)));
    }
    return out;
}

Result<std::vector<Section>> readSections(std::span<const std::uint8_t> entry) {
    if (entry.size() < 7 || entry[0] != 0x01) {
        return makeError(ErrorCode::Format, "protocol entry is corrupt");
    }
    const std::size_t count = entry[6];
    if (entry.size() < 7 + count * 2) {
        return makeError(ErrorCode::Format, "protocol entry is truncated");
    }
    std::vector<Section> sections;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t at = read16(entry, 7 + i * 2);
        if (at + 16 > entry.size()) {
            return makeError(ErrorCode::Format, "protocol section exceeds the entry");
        }
        Section section;
        section.bitCount = read16(entry, at);
        section.interval = read32(entry, at + 4);
        const std::size_t pairs = entry[at + 9];
        const std::size_t data = read16(entry, at + 10);
        auto start = readBlock(entry, read16(entry, at + 12));
        auto end = readBlock(entry, read16(entry, at + 14));
        if (!start || !end) {
            return std::unexpected(std::move(start ? end.error() : start.error()));
        }
        section.start = std::move(*start);
        section.end = std::move(*end);
        if (data != 0 && pairs > 0) {
            if (data + pairs * 4 > entry.size()) {
                return makeError(ErrorCode::Format, "data timings exceed the entry");
            }
            section.zero = {timing(read16(entry, data)), timing(read16(entry, data + 2))};
            if (pairs > 1) {
                section.one = {timing(read16(entry, data + 4)), timing(read16(entry, data + 6))};
            }
        }
        sections.push_back(std::move(section));
    }
    return sections;
}

/// Добавляет длительность, складывая соседние одного вида.
void append(std::vector<int>& out, int duration) {
    if (duration == 0) {
        return;
    }
    if (!out.empty() && (out.back() > 0) == (duration > 0)) {
        out.back() += duration;
    } else {
        out.push_back(duration);
    }
}

/// Кадр секции: начало, биты, конец и пауза до периода кадра.
void renderFrame(const Section& section, const std::vector<bool>& bits, std::vector<int>& out) {
    if (bits.size() < section.bitCount) {
        return; // так же поступает пульт: без данных кадр не передаётся
    }
    std::vector<int> frame;
    for (const int d : section.start) {
        append(frame, d);
    }
    for (std::size_t i = 0; i < section.bitCount; ++i) {
        for (const int d : bits[i] ? section.one : section.zero) {
            append(frame, d);
        }
    }
    for (const int d : section.end) {
        append(frame, d);
    }
    long total = 0;
    for (const int d : frame) {
        total += std::abs(d);
    }
    for (const int d : frame) {
        append(out, d);
    }
    if (section.interval != kNoInterval && section.interval > 0) {
        const long pause = static_cast<long>(section.interval) - total;
        append(out, -static_cast<int>(pause > 0 ? pause : 50000));
    }
}

} // namespace

std::vector<bool> ProtocolCode::bits() const {
    std::vector<bool> all;
    for (const auto& section : sections) {
        all.insert(all.end(), section.begin(), section.end());
    }
    return all;
}

Result<ProtocolCode> parseProtocolCode(std::string_view code) {
    if (code.starts_with("0x") || code.starts_with("0X")) {
        code.remove_prefix(2);
    }
    if (code.size() % 2 != 0) {
        return makeError(ErrorCode::Format, "command code has an odd number of hex digits");
    }
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i < code.size(); i += 2) {
        unsigned value = 0;
        const auto [end, error] = std::from_chars(code.data() + i, code.data() + i + 2, value, 16);
        if (error != std::errc() || end != code.data() + i + 2) {
            return makeError(ErrorCode::Format, "command code is not a hexadecimal number");
        }
        bytes.push_back(static_cast<std::uint8_t>(value));
    }
    if (bytes.size() < 9 || bytes.back() != 0) {
        return makeError(ErrorCode::Format, "command code is too short");
    }
    bytes.pop_back(); // завершающий 00
    const std::span<const std::uint8_t> data(bytes);

    ProtocolCode result;
    result.protocol = read16(data, 0);
    result.delay = read16(data, 2);
    result.dataFrames = data[4];
    result.repeatFrame = (data[5] & 1) != 0;
    const std::size_t kind = data[6];
    if (kind == 0) {
        // Биты подряд, в конце 01 01.
        if (data.size() < 10 || data[data.size() - 1] != 1 || data[data.size() - 2] != 1) {
            return makeError(ErrorCode::Format, "command code: no end-of-data mark");
        }
        result.sections.push_back(bitsOf(data.subspan(7, data.size() - 9)));
    } else if (kind == 1) {
        result.sections.push_back(bitsOf(data.subspan(8)));
    } else {
        if (data.size() < 8 + kind * 2) {
            return makeError(ErrorCode::Format, "command code: sections are truncated");
        }
        for (std::size_t i = 0; i < kind; ++i) {
            result.sections.push_back(bitsOf(data.subspan(8 + i * 2, 2)));
        }
    }
    return result;
}

Result<IrSignal> renderProtocol(std::span<const std::uint8_t> protocol, const ProtocolCode& code) {
    auto sections = readSections(protocol);
    if (!sections) {
        return std::unexpected(std::move(sections.error()));
    }
    // Оценка сверху до сборки: сколько длительностей дадут все кадры.
    std::size_t frameSize = 0;
    for (std::size_t i = 0; i < code.sections.size() && i < sections->size(); ++i) {
        const Section& section = (*sections)[i];
        frameSize += section.start.size() + section.end.size() + 1 +
                     section.bitCount * std::max(section.zero.size(), section.one.size());
    }
    std::size_t total = frameSize * code.dataFrames;
    if (code.repeatFrame && code.sections.size() < sections->size()) {
        const Section& repeat = (*sections)[code.sections.size()];
        total += repeat.start.size() + repeat.end.size() + 1;
    }
    if (total > kMaxDurations) {
        return makeError(ErrorCode::Format, "the protocol signal is too long (%1 durations)",
                         {std::to_string(total)});
    }

    IrSignal signal;
    const std::uint32_t period = read16(protocol, 1);
    signal.frequency = period > 0 ? static_cast<std::uint32_t>(1'000'000'000U / period) : 38000;
    // Кадр данных — сколько задано раз (все секции подряд), затем кадр повтора.
    for (unsigned frame = 0; frame < code.dataFrames; ++frame) {
        for (std::size_t i = 0; i < code.sections.size() && i < sections->size(); ++i) {
            renderFrame((*sections)[i], code.sections[i], signal.durations);
        }
    }
    if (code.repeatFrame && code.sections.size() < sections->size()) {
        renderFrame((*sections)[code.sections.size()], {}, signal.durations);
    }
    // Сигнал начинается с импульса: ведущую паузу (если есть) отбрасываем.
    if (!signal.durations.empty() && signal.durations.front() < 0) {
        signal.durations.erase(signal.durations.begin());
    }
    return signal;
}

} // namespace harmony::ir
