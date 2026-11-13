/**
 * @file irTables.cpp
 * @brief Чтение и запись IrProto.bin и SsIr.bin.
 */

#include "harmony/core/formats/irTables.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <optional>

namespace harmony::formats {

namespace {

/// Заголовок таблицы; шестой байт (01) отнесён к данным — от него считаются смещения.
constexpr std::array<std::uint8_t, 5> kHeader = {0x01, 0x01, 0x05, 0x00, 0x00};
constexpr std::size_t kWrapperSize = 8; ///< Обёртка IrProto.bin: CRC32, размер, 00 00

// Устройство записи протокола (см. irTables.h).
constexpr std::size_t kProtocolHeader = 7;   ///< 01, несущая (2), 00, 00, 32, число секций
constexpr std::size_t kSectionHeader = 16;   ///< Заголовок секции таймингов
constexpr std::size_t kSectionPointers = 10; ///< Смещение трёх указателей в секции (данные, начало, конец кадра)

std::uint16_t read16(std::span<const std::uint8_t> data, std::size_t at) {
    return static_cast<std::uint16_t>(data[at] | (data[at + 1] << 8));
}

void write16(Bytes& data, std::size_t at, std::uint32_t value) {
    data[at] = static_cast<std::uint8_t>(value & 0xFF);
    data[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void append16(Bytes& data, std::uint32_t value) {
    data.push_back(static_cast<std::uint8_t>(value & 0xFF));
    data.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

/// Разбирает таблицу (начиная с пятибайтового заголовка) на записи и их смещения.
Result<IrTable> parseTable(std::span<const std::uint8_t> raw, std::vector<std::size_t>* offsets,
                           std::string_view fileName) {
    const std::string name(fileName);
    if (raw.size() < kHeader.size() + 3 || !std::equal(kHeader.begin(), kHeader.end(), raw.begin())) {
        return makeError(ErrorCode::Format, "%1: unknown header", {name});
    }
    const auto data = raw.subspan(kHeader.size());
    const std::size_t count = read16(data, 1);
    const std::size_t tableEnd = 3 + 2 * count;
    if (data[0] != 0x01 || data.size() < tableEnd) {
        return makeError(ErrorCode::Format, "%1: offset table is truncated", {name});
    }

    IrTable table;
    std::size_t previous = tableEnd;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t start = read16(data, 3 + 2 * i);
        const std::size_t end = i + 1 < count ? read16(data, 3 + 2 * (i + 1)) : data.size();
        if (start < previous || end < start || end > data.size()) {
            return makeError(ErrorCode::Format, "%1: invalid entry offset %2", {name, std::to_string(i)});
        }
        table.entries.emplace_back(data.begin() + static_cast<std::ptrdiff_t>(start),
                                   data.begin() + static_cast<std::ptrdiff_t>(end));
        if (offsets != nullptr) {
            offsets->push_back(start);
        }
        previous = start;
    }
    return table;
}

/// Собирает таблицу; @p place вызывается для каждой записи с её итоговым смещением.
Bytes writeTable(const IrTable& table, const std::function<Bytes(const Bytes&, std::size_t)>& place) {
    Bytes out(kHeader.begin(), kHeader.end());
    out.push_back(0x01);
    append16(out, static_cast<std::uint32_t>(table.entries.size()));
    std::size_t offset = 3 + 2 * table.entries.size();
    for (const auto& entry : table.entries) {
        append16(out, static_cast<std::uint32_t>(offset));
        offset += entry.size();
    }
    offset = 3 + 2 * table.entries.size();
    for (const auto& entry : table.entries) {
        const Bytes placed = place ? place(entry, offset) : entry;
        out.insert(out.end(), placed.begin(), placed.end());
        offset += entry.size();
    }
    return out;
}

/**
 * @brief Сдвигает указатели внутри протокола на @p delta.
 *
 * Указатели: на секции таймингов (в заголовке протокола) и на части секций
 * (по три в каждой секции, 0 — части нет).
 * @param toRelative true — от начала таблицы к началу записи (base = смещение записи).
 * @return false, если запись устроена иначе или указатель выходит за её пределы.
 */
bool relocate(Bytes& entry, std::size_t base, bool toRelative) {
    if (entry.size() < kProtocolHeader || entry[0] != 0x01) {
        return false;
    }
    const std::size_t sections = entry[6];
    if (entry.size() < kProtocolHeader + 2 * sections) {
        return false;
    }
    // Переводит одно смещение; в относительном виде оно обязано попасть в запись.
    auto shift = [&](std::size_t at) -> std::optional<std::size_t> {
        const std::size_t value = read16(entry, at);
        if (value == 0) {
            return 0; // части нет
        }
        const std::size_t relative = toRelative ? value - base : value;
        if ((toRelative && value < base) || relative >= entry.size()) {
            return std::nullopt;
        }
        write16(entry, at, static_cast<std::uint32_t>(toRelative ? relative : value + base));
        return relative;
    };
    // На одну секцию могут ссылаться несколько указателей (у Samsung повтор — тот же
    // кадр данных): её собственные указатели сдвигаются один раз.
    std::vector<std::size_t> done;
    for (std::size_t i = 0; i < sections; ++i) {
        const auto section = shift(kProtocolHeader + 2 * i);
        if (!section || *section + kSectionHeader > entry.size()) {
            return false;
        }
        if (std::ranges::find(done, *section) != done.end()) {
            continue;
        }
        done.push_back(*section);
        for (std::size_t p = 0; p < 3; ++p) {
            if (!shift(*section + kSectionPointers + 2 * p)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

std::size_t IrTable::intern(const Bytes& entry) {
    const auto found = std::ranges::find(entries, entry);
    if (found != entries.end()) {
        return static_cast<std::size_t>(found - entries.begin());
    }
    entries.push_back(entry);
    return entries.size() - 1;
}

bool fitsOffsets(const IrTable& table) {
    // Данные: шестой байт заголовка (01), число записей, смещения, записи.
    std::size_t size = 3 + 2 * table.entries.size();
    for (const auto& entry : table.entries) {
        size += entry.size();
    }
    return size <= 0xFFFFU;
}

std::uint32_t crc32(std::span<const std::uint8_t> data) {
    // Таблица на 256 значений строится один раз при первом вызове.
    static const auto table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int bit = 0; bit < 8; ++bit) {
                c = (c & 1U) != 0 ? 0xEDB88320U ^ (c >> 1) : c >> 1;
            }
            values[i] = c;
        }
        return values;
    }();
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t byte : data) {
        crc = table[(crc ^ byte) & 0xFFU] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

Result<IrTable> parseIrProto(std::span<const std::uint8_t> file) {
    const std::string name(kIrProtoFileName);
    if (file.size() < kWrapperSize) {
        return makeError(ErrorCode::Format, "%1: file is too short", {name});
    }
    const auto data = file.subspan(kWrapperSize);
    const std::uint32_t storedCrc = static_cast<std::uint32_t>(read16(file, 0)) |
                                    (static_cast<std::uint32_t>(read16(file, 2)) << 16);
    if (read16(file, 4) != data.size() || read16(file, 6) != 0) {
        return makeError(ErrorCode::Format, "%1: invalid size in the header", {name});
    }
    if (crc32(data) != storedCrc) {
        return makeError(ErrorCode::Format, "%1: checksum mismatch", {name});
    }

    std::vector<std::size_t> offsets;
    auto table = parseTable(data, &offsets, kIrProtoFileName);
    if (!table) {
        return table;
    }
    for (std::size_t i = 0; i < table->entries.size(); ++i) {
        if (!relocate(table->entries[i], offsets[i], true)) {
            return makeError(ErrorCode::Format, "%1: unknown protocol device %2", {name, std::to_string(i)});
        }
    }
    return table;
}

Result<Bytes> buildIrProto(const IrTable& table) {
    const std::string name(kIrProtoFileName);
    if (!fitsOffsets(table)) {
        return makeError(ErrorCode::Format, "%1: the table does not fit into 64 KB", {name});
    }
    std::optional<std::size_t> broken;
    std::size_t index = 0;
    const Bytes data = writeTable(table, [&](const Bytes& entry, std::size_t offset) {
        Bytes placed = entry;
        if (!relocate(placed, offset, false) && !broken) {
            broken = index;
        }
        ++index;
        return placed;
    });
    if (broken) {
        return makeError(ErrorCode::Format, "%1: protocol %2 is damaged", {name, std::to_string(*broken)});
    }
    Bytes file(kWrapperSize);
    const std::uint32_t crc = crc32(data);
    write16(file, 0, crc & 0xFFFFU);
    write16(file, 2, crc >> 16);
    write16(file, 4, static_cast<std::uint32_t>(data.size()));
    file.insert(file.end(), data.begin(), data.end());
    return file;
}

Bytes writeIrProto(const IrTable& table) {
    auto file = buildIrProto(table);
    return file ? std::move(*file) : Bytes();
}

std::string protocolHash(std::span<const std::uint8_t> irProtoFile) {
    const std::uint32_t crc = irProtoFile.size() < 4
                                  ? 0
                                  : static_cast<std::uint32_t>(read16(irProtoFile, 0)) |
                                        (static_cast<std::uint32_t>(read16(irProtoFile, 2)) << 16);
    char text[11];
    std::snprintf(text, sizeof text, "0x%08X", crc);
    return text;
}

Result<IrTable> parseSsIr(std::span<const std::uint8_t> file) {
    return parseTable(file, nullptr, kSsIrFileName);
}

Bytes writeSsIr(const IrTable& table) {
    return writeTable(table, {});
}

} // namespace harmony::formats
