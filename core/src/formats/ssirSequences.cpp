/**
 * @file ssirSequences.cpp
 * @brief Разбор SsIr.bin пульта Harmony 1100 (последовательности устройств).
 */

#include "harmony/core/formats/ssirSequences.h"

#include <algorithm>
#include <set>

namespace harmony::formats {

namespace {

constexpr std::size_t kMaxWords = 1 << 16; ///< Сигнал длиннее не бывает: хватило бы и 4096

std::uint32_t u16(std::span<const std::uint8_t> d, std::size_t at) {
    return static_cast<std::uint32_t>(d[at]) | (static_cast<std::uint32_t>(d[at + 1]) << 8);
}

std::uint32_t u24(std::span<const std::uint8_t> d, std::size_t at) {
    return u16(d, at) | (static_cast<std::uint32_t>(d[at + 2]) << 16);
}

std::unexpected<Error> bad(const char* text) {
    return makeError(ErrorCode::Format, text);
}

/// Читатель с проверкой границ: любая попытка выйти за файл — ошибка, а не чтение мусора.
struct Reader {
    std::span<const std::uint8_t> data;

    bool has(std::size_t at, std::size_t count) const {
        return at <= data.size() && count <= data.size() - at;
    }
};

struct VariantRef {
    std::size_t waveform;
    std::size_t repeat; ///< 0 — нет
};

struct SequenceRef {
    std::size_t carrier;
    std::vector<VariantRef> variants;
};

} // namespace

std::uint32_t SsirSequence::frequency() const {
    // Округление к ближайшему: период 26316 нс — это 38 кГц, а не 37999.
    return carrierPeriodNs == 0
               ? 0
               : static_cast<std::uint32_t>((1'000'000'000ull + carrierPeriodNs / 2) / carrierPeriodNs);
}

std::size_t SsirSequences::sequenceCount() const {
    std::size_t total = 0;
    for (const auto& device : devices) {
        total += device.size();
    }
    return total;
}

Result<SsirSequences> parseSsirSequences(std::span<const std::uint8_t> file) {
    const Reader in{file};
    if (!in.has(0, 5) || u16(file, 0) != 1) {
        return bad("SsIr.bin: unsupported layout (not a Harmony 1100 sequence table)");
    }
    const std::size_t root = u24(file, 2);
    if (!in.has(root, 1)) {
        return bad("SsIr.bin: the root directory is outside the file");
    }

    // Первый проход: собрать все структуры и ссылки; каждая граница проверяется.
    std::set<std::size_t> starts{root};
    std::vector<std::vector<SequenceRef>> refs;

    const std::size_t deviceCount = file[root];
    if (!in.has(root + 1, 3 * deviceCount)) {
        return bad("SsIr.bin: the device directory is cut off");
    }
    for (std::size_t device = 0; device < deviceCount; ++device) {
        const std::size_t table = u24(file, root + 1 + 3 * device);
        if (!in.has(table, 3)) {
            return bad("SsIr.bin: a device table is outside the file");
        }
        starts.insert(table);
        const std::size_t count = u16(file, table + 1);
        if (!in.has(table + 3, 3 * count)) {
            return bad("SsIr.bin: a sequence list is cut off");
        }
        auto& list = refs.emplace_back();
        for (std::size_t s = 0; s < count; ++s) {
            const std::size_t record = u24(file, table + 3 + 3 * s);
            if (!in.has(record, 1)) {
                return bad("SsIr.bin: a sequence record is outside the file");
            }
            starts.insert(record);
            SequenceRef ref{};
            const std::size_t carriers = file[record];
            if (carriers == 0 || !in.has(record + 1, 3 * carriers + 1)) {
                return bad("SsIr.bin: a sequence record is cut off");
            }
            ref.carrier = u24(file, record + 1);
            if (!in.has(ref.carrier, 7)) {
                return bad("SsIr.bin: a carrier descriptor is outside the file");
            }
            starts.insert(ref.carrier);
            const std::size_t variantsAt = record + 1 + 3 * carriers;
            const std::size_t variants = file[variantsAt];
            if (!in.has(variantsAt + 1, 9 * variants)) {
                return bad("SsIr.bin: the variants of a sequence are cut off");
            }
            for (std::size_t v = 0; v < variants; ++v) {
                const std::size_t at = variantsAt + 1 + 9 * v;
                VariantRef variant{u24(file, at), u24(file, at + 3)};
                if (!in.has(variant.waveform, 2)) {
                    return bad("SsIr.bin: a waveform is outside the file");
                }
                if (variant.repeat != 0 && !in.has(variant.repeat, 2)) {
                    return bad("SsIr.bin: a repeat point is outside the file");
                }
                starts.insert(variant.waveform);
                ref.variants.push_back(variant);
            }
            list.push_back(std::move(ref));
        }
    }

    // Конец сигнала — ближайшая следующая структура пула либо конец файла; слова целые.
    const auto endOf = [&](std::size_t offset) {
        const auto next = starts.upper_bound(offset);
        std::size_t end = next == starts.end() ? file.size() : *next;
        end = offset + ((end - offset) & ~std::size_t{1});
        return end;
    };
    const auto wordsFrom = [&](std::size_t from, std::size_t to) {
        std::vector<std::uint16_t> words;
        for (std::size_t at = from; at + 2 <= to && words.size() < kMaxWords; at += 2) {
            words.push_back(static_cast<std::uint16_t>(u16(file, at)));
        }
        return words;
    };

    SsirSequences result;
    for (const auto& device : refs) {
        auto& out = result.devices.emplace_back();
        for (const auto& ref : device) {
            SsirSequence sequence;
            sequence.carrierPeriodNs = u24(file, ref.carrier + 1);
            sequence.carrierOnNs = u24(file, ref.carrier + 4);
            for (const auto& variant : ref.variants) {
                SsirVariant item;
                const std::size_t end = endOf(variant.waveform);
                item.words = wordsFrom(variant.waveform, end);
                if (variant.repeat != 0) {
                    if (variant.repeat >= variant.waveform && variant.repeat < end) {
                        item.repeatAt = (variant.repeat - variant.waveform) / 2;
                    } else {
                        item.sharedRepeat = wordsFrom(variant.repeat, endOf(variant.repeat));
                    }
                }
                sequence.variants.push_back(std::move(item));
            }
            out.push_back(std::move(sequence));
        }
    }
    return result;
}

std::optional<ir::IrSignal> firstFrameSignal(const SsirSequence& sequence) {
    if (sequence.variants.empty()) {
        return std::nullopt;
    }
    ir::IrSignal signal;
    signal.frequency = sequence.frequency() != 0 ? sequence.frequency() : 38000;

    const auto& words = sequence.variants.front().words;
    bool seenMark = false;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::uint16_t word = words[i];
        // Конец сегмента: пауза 1 мкс и нулевое слово.
        if (word == 0x0001 && i + 1 < words.size() && words[i + 1] == 0) {
            break;
        }
        if (word == 0) {
            break;
        }
        const bool mark = (word & 0x8000) != 0;
        const int duration = word & 0x7FFF;
        if (!mark && !seenMark) {
            continue; // задержка перед нажатием
        }
        seenMark = seenMark || mark;
        const int value = mark ? duration : -duration;
        if (!signal.durations.empty() && (signal.durations.back() > 0) == mark) {
            signal.durations.back() += value; // длинная пауза записана несколькими словами
        } else {
            signal.durations.push_back(value);
        }
    }
    if (!seenMark) {
        return std::nullopt;
    }
    return signal;
}

} // namespace harmony::formats
