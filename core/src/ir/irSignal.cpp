/**
 * @file irSignal.cpp
 * @brief Хранение ИК-сигнала в SsIr.bin; описание сигнала для показа.
 */

#include "harmony/core/ir/irSignal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <span>

namespace harmony::ir {

namespace {

constexpr std::uint16_t kMarkBit = 0x8000;
constexpr int kMaxWord = 0x7FFF;

void put16(formats::Bytes& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

std::uint32_t read16(std::span<const std::uint8_t> data, std::size_t at) {
    return static_cast<std::uint32_t>(data[at] | (data[at + 1] << 8));
}

} // namespace

formats::Bytes encodeRawEntry(const IrSignal& signal) {
    std::vector<std::uint16_t> words;
    for (const int duration : signal.durations) {
        const int length = std::abs(duration);
        if (length == 0) {
            continue;
        }
        // Длинную длительность — равными словами (так делает и Logitech).
        const int parts = (length + kMaxWord - 1) / kMaxWord;
        for (int i = 0; i < parts; ++i) {
            const int part = length / parts + (i < length % parts ? 1 : 0);
            words.push_back(static_cast<std::uint16_t>(part | (duration > 0 ? kMarkBit : 0)));
        }
    }
    formats::Bytes out;
    const auto period = static_cast<std::uint32_t>(std::lround(1e9 / std::max<std::uint32_t>(signal.frequency, 1)));
    put16(out, period & 0xFFFF);
    put16(out, period >> 16);
    put16(out, static_cast<std::uint32_t>(words.size()));
    for (const std::uint16_t word : words) {
        put16(out, word);
    }
    return out;
}

Result<IrSignal> decodeRawEntry(std::span<const std::uint8_t> entry) {
    if (entry.size() < 6 || (entry.size() - 6) % 2 != 0) {
        return makeError(ErrorCode::Format, "raw IR code entry is corrupt (%1 bytes)", {std::to_string(entry.size())});
    }
    const std::size_t count = read16(entry, 4);
    if (count != (entry.size() - 6) / 2) {
        return makeError(ErrorCode::Format, "word count mismatch in a raw IR code entry");
    }
    IrSignal signal;
    const std::uint32_t period = read16(entry, 0) | (read16(entry, 2) << 16);
    signal.frequency = period > 0 ? static_cast<std::uint32_t>(std::lround(1e9 / period)) : 0;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t word = read16(entry, 6 + i * 2);
        const int length = static_cast<int>(word & kMaxWord);
        const int duration = (word & kMarkBit) != 0 ? length : -length;
        // Подряд идущие слова одного вида — одна длительность.
        if (!signal.durations.empty() && (signal.durations.back() > 0) == (duration > 0)) {
            signal.durations.back() += duration;
        } else {
            signal.durations.push_back(duration);
        }
    }
    return signal;
}

IrSignal fromMarkSegments(std::uint32_t frequency, const std::vector<std::uint16_t>& markSegments) {
    IrSignal signal;
    signal.frequency = frequency;
    for (std::size_t i = 0; i + 1 < markSegments.size(); i += 2) {
        const int mark = markSegments[i];
        const int pause = static_cast<int>(markSegments[i + 1]) - mark;
        if (mark > 0) {
            signal.durations.push_back(mark);
        }
        if (pause > 0) {
            signal.durations.push_back(-pause);
        }
    }
    return signal;
}

std::string timingText(const IrSignal& signal) {
    std::string text;
    for (const int duration : signal.durations) {
        if (!text.empty()) {
            text += ' ';
        }
        text += (duration > 0 ? "+" : "-") + std::to_string(std::abs(duration));
    }
    return text;
}

std::string prontoHex(const IrSignal& signal) {
    if (signal.durations.empty() || signal.frequency == 0) {
        return {};
    }
    // Пары «импульс, пауза» в периодах несущей; сигнал без конечной паузы получает короткую.
    std::vector<long> periods;
    const double carrier = signal.frequency;
    for (std::size_t i = 0; i < signal.durations.size(); ++i) {
        const int duration = signal.durations[i];
        if (periods.size() % 2 == 0 && duration < 0) {
            periods.push_back(1); // пара всегда начинается с импульса
        }
        periods.push_back(std::max(1L, std::lround(std::abs(duration) * carrier / 1e6)));
    }
    if (periods.size() % 2 != 0) {
        periods.push_back(1);
    }
    std::string text;
    auto word = [&text](long value) {
        char buffer[8];
        std::snprintf(buffer, sizeof buffer, "%04lX", std::min(value, 0xFFFFL));
        if (!text.empty()) {
            text += ' ';
        }
        text += buffer;
    };
    word(0); // выученный сигнал
    word(std::lround(1e6 / (carrier * 0.241246)));
    word(static_cast<long>(periods.size() / 2));
    word(0);
    for (const long value : periods) {
        word(value);
    }
    return text;
}

std::string describe(const IrSignal& signal) {
    if (const auto decoded = decode(signal)) {
        return decoded->text();
    }
    std::size_t marks = 0;
    for (const int duration : signal.durations) {
        marks += duration > 0 ? 1 : 0;
    }
    char khz[16];
    std::snprintf(khz, sizeof khz, "%.1f", signal.frequency / 1000.0);
    return "protocol not recognised · pulses: " + std::to_string(marks) + ", carrier " + khz + " kHz";
}

namespace {

/// Первый кадр сигнала: до паузы длиннее 10 мс (повторы при удержании не важны).
std::span<const int> firstFrame(const IrSignal& signal) {
    const auto& d = signal.durations;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (d[i] < -10'000) {
            return {d.data(), i};
        }
    }
    return d;
}

} // namespace

bool sameSignal(const IrSignal& a, const IrSignal& b) {
    const auto first = decode(a);
    const auto second = decode(b);
    if (first || second) {
        return first && second && first->protocol == second->protocol && first->address == second->address &&
               first->command == second->command && first->bits.size() == second->bits.size();
    }
    const auto x = firstFrame(a);
    const auto y = firstFrame(b);
    if (x.size() != y.size() || x.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < x.size(); ++i) {
        if ((x[i] > 0) != (y[i] > 0)) {
            return false;
        }
        const int p = std::abs(x[i]);
        const int q = std::abs(y[i]);
        if (std::abs(p - q) > std::max(200, std::max(p, q) / 4)) {
            return false;
        }
    }
    return true;
}

} // namespace harmony::ir
