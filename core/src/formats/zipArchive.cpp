/**
 * @file zipArchive.cpp
 * @brief Чтение и запись ZIP с сохранением метаданных записей.
 */

#include "harmony/core/formats/zipArchive.h"

#include <zlib.h>

#include <algorithm>
#include <ctime>
#include <optional>

namespace harmony::formats {

namespace {

constexpr std::uint32_t kLocalSignature = 0x04034B50;
constexpr std::uint32_t kCentralSignature = 0x02014B50;
constexpr std::uint32_t kEndSignature = 0x06054B50;
constexpr std::size_t kLocalHeader = 30;
constexpr std::size_t kCentralHeader = 46;
constexpr std::size_t kEndRecord = 22;

// Архив приходит с пульта (то есть извне), поэтому размеры из заголовков не доверяем:
// иначе подменённый файл заставит выделить гигабайты памяти (zip-bomb).
constexpr std::size_t kMaxEntrySize = 64U * 1024U * 1024U;   ///< Распакованная запись, байт
constexpr std::size_t kMaxTotalSize = 256U * 1024U * 1024U;  ///< Все записи вместе, байт

/// Чтение чисел little endian с проверкой границ.
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool has(std::size_t at, std::size_t count) const {
        return at <= bytes_.size() && count <= bytes_.size() - at;
    }
    [[nodiscard]] std::uint16_t u16(std::size_t at) const {
        return static_cast<std::uint16_t>(bytes_[at] | (bytes_[at + 1] << 8));
    }
    [[nodiscard]] std::uint32_t u32(std::size_t at) const {
        return static_cast<std::uint32_t>(u16(at)) | (static_cast<std::uint32_t>(u16(at + 2)) << 16);
    }
    [[nodiscard]] Bytes slice(std::size_t at, std::size_t count) const {
        return {bytes_.begin() + static_cast<std::ptrdiff_t>(at),
                bytes_.begin() + static_cast<std::ptrdiff_t>(at + count)};
    }

private:
    std::span<const std::uint8_t> bytes_;
};

void put16(Bytes& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void put32(Bytes& out, std::uint32_t value) {
    put16(out, value & 0xFFFF);
    put16(out, value >> 16);
}

void append(Bytes& out, std::span<const std::uint8_t> bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}

std::uint32_t crcOf(std::span<const std::uint8_t> data) {
    return static_cast<std::uint32_t>(
        ::crc32(0L, data.empty() ? nullptr : data.data(), static_cast<uInt>(data.size())));
}

/// Сжатие deflate без заголовков zlib (как в ZIP); nullopt — zlib не справился (нет памяти).
std::optional<Bytes> deflateRaw(std::span<const std::uint8_t> data) {
    z_stream stream{};
    if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return std::nullopt;
    }
    Bytes out(deflateBound(&stream, static_cast<uLong>(data.size())));
    stream.next_in = const_cast<Bytef*>(data.data());
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    // Буфер размером deflateBound(): весь поток помещается за один вызов.
    const int result = deflate(&stream, Z_FINISH);
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return result == Z_STREAM_END ? std::optional<Bytes>(std::move(out)) : std::nullopt;
}

/// Распаковка deflate без заголовков zlib; nullopt — данные испорчены.
std::optional<Bytes> inflateRaw(std::span<const std::uint8_t> data, std::size_t size) {
    if (size > kMaxEntrySize) {
        return std::nullopt;
    }
    Bytes out(size);
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return std::nullopt;
    }
    stream.next_in = const_cast<Bytef*>(data.data());
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    const int result = inflate(&stream, Z_FINISH);
    const bool complete = result == Z_STREAM_END && stream.total_out == size;
    inflateEnd(&stream);
    return complete ? std::optional<Bytes>(std::move(out)) : std::nullopt;
}

/// Время Unix → поля времени и даты DOS (местное время, как у Info-ZIP).
void toDos(std::int64_t unixTime, std::uint16_t& time, std::uint16_t& date) {
    const auto seconds = static_cast<std::time_t>(unixTime);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    time = static_cast<std::uint16_t>((local.tm_hour << 11) | (local.tm_min << 5) | (local.tm_sec / 2));
    date = static_cast<std::uint16_t>(((std::max(local.tm_year, 80) - 80) << 9) |
                                      ((local.tm_mon + 1) << 5) | local.tm_mday);
}

} // namespace

ZipEntry* ZipArchive::find(const std::string& name) {
    const auto found = std::ranges::find(entries, name, &ZipEntry::name);
    return found != entries.end() ? &*found : nullptr;
}

const ZipEntry* ZipArchive::find(const std::string& name) const {
    const auto found = std::ranges::find(entries, name, &ZipEntry::name);
    return found != entries.end() ? &*found : nullptr;
}

Result<ZipArchive> readZip(std::span<const std::uint8_t> bytes) {
    const Reader in(bytes);
    // Конец оглавления — последние 22 байта плюс комментарий архива (до 64 КБ).
    if (bytes.size() < kEndRecord) {
        return makeError(ErrorCode::Format, "ZIP: file is too short");
    }
    std::size_t end = bytes.size() - kEndRecord;
    while (in.u32(end) != kEndSignature) {
        if (end == 0 || bytes.size() - end > kEndRecord + 0xFFFF) {
            return makeError(ErrorCode::Format, "ZIP: end of central directory not found");
        }
        --end;
    }
    const std::size_t count = in.u16(end + 10);
    const std::size_t directorySize = in.u32(end + 12);
    std::size_t at = in.u32(end + 16);
    if (in.u16(end + 4) != 0 || in.u16(end + 6) != 0 || !in.has(at, directorySize)) {
        return makeError(ErrorCode::Format, "ZIP: multi-volume archives are not supported");
    }

    ZipArchive archive;
    archive.comment = in.slice(end + kEndRecord, std::min<std::size_t>(in.u16(end + 20), bytes.size() - end - kEndRecord));
    std::size_t totalSize = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!in.has(at, kCentralHeader) || in.u32(at) != kCentralSignature) {
            return makeError(ErrorCode::Format, "ZIP: central directory is corrupt");
        }
        ZipEntry entry;
        entry.versionMadeBy = in.u16(at + 4);
        entry.versionNeeded = in.u16(at + 6);
        entry.flags = in.u16(at + 8);
        entry.method = in.u16(at + 10);
        entry.dosTime = in.u16(at + 12);
        entry.dosDate = in.u16(at + 14);
        entry.crc = in.u32(at + 16);
        const std::size_t compressed = in.u32(at + 20);
        entry.size = in.u32(at + 24);
        const std::size_t nameLength = in.u16(at + 28);
        const std::size_t extraLength = in.u16(at + 30);
        const std::size_t commentLength = in.u16(at + 32);
        entry.internalAttributes = in.u16(at + 36);
        entry.externalAttributes = in.u32(at + 38);
        const std::size_t local = in.u32(at + 42);
        if (!in.has(at + kCentralHeader, nameLength + extraLength + commentLength)) {
            return makeError(ErrorCode::Format, "ZIP: central directory is corrupt");
        }
        const Bytes name = in.slice(at + kCentralHeader, nameLength);
        entry.name.assign(name.begin(), name.end());
        totalSize += entry.size;
        if (entry.size > kMaxEntrySize || totalSize > kMaxTotalSize) {
            return makeError(ErrorCode::Format, "ZIP: “%1” is too large for a remote configuration", {entry.name});
        }
        entry.centralExtra = in.slice(at + kCentralHeader + nameLength, extraLength);
        entry.comment = in.slice(at + kCentralHeader + nameLength + extraLength, commentLength);
        if ((entry.flags & 0x0009U) != 0) { // 1 — шифрование, 8 — дескриптор данных
            return makeError(ErrorCode::Format, "ZIP: “%1” — unsupported entry variant", {entry.name});
        }

        // Локальный заголовок: служебные поля там бывают длиннее, чем в оглавлении.
        if (!in.has(local, kLocalHeader) || in.u32(local) != kLocalSignature) {
            return makeError(ErrorCode::Format, "ZIP: header of “%1” not found", {entry.name});
        }
        const std::size_t localName = in.u16(local + 26);
        const std::size_t localExtra = in.u16(local + 28);
        const std::size_t dataAt = local + kLocalHeader + localName + localExtra;
        if (!in.has(dataAt, compressed)) {
            return makeError(ErrorCode::Format, "ZIP: data of “%1” is truncated", {entry.name});
        }
        entry.localExtra = in.slice(local + kLocalHeader + localName, localExtra);
        entry.data = in.slice(dataAt, compressed);
        archive.entries.push_back(std::move(entry));
        at += kCentralHeader + nameLength + extraLength + commentLength;
    }
    return archive;
}

Bytes writeZip(const ZipArchive& archive) {
    Bytes out;
    std::vector<std::uint32_t> offsets;
    for (const ZipEntry& entry : archive.entries) {
        offsets.push_back(static_cast<std::uint32_t>(out.size()));
        put32(out, kLocalSignature);
        put16(out, entry.versionNeeded);
        put16(out, entry.flags);
        put16(out, entry.method);
        put16(out, entry.dosTime);
        put16(out, entry.dosDate);
        put32(out, entry.crc);
        put32(out, static_cast<std::uint32_t>(entry.data.size()));
        put32(out, entry.size);
        put16(out, static_cast<std::uint32_t>(entry.name.size()));
        put16(out, static_cast<std::uint32_t>(entry.localExtra.size()));
        out.insert(out.end(), entry.name.begin(), entry.name.end());
        append(out, entry.localExtra);
        append(out, entry.data);
    }
    const auto directoryAt = static_cast<std::uint32_t>(out.size());
    for (std::size_t i = 0; i < archive.entries.size(); ++i) {
        const ZipEntry& entry = archive.entries[i];
        put32(out, kCentralSignature);
        put16(out, entry.versionMadeBy);
        put16(out, entry.versionNeeded);
        put16(out, entry.flags);
        put16(out, entry.method);
        put16(out, entry.dosTime);
        put16(out, entry.dosDate);
        put32(out, entry.crc);
        put32(out, static_cast<std::uint32_t>(entry.data.size()));
        put32(out, entry.size);
        put16(out, static_cast<std::uint32_t>(entry.name.size()));
        put16(out, static_cast<std::uint32_t>(entry.centralExtra.size()));
        put16(out, static_cast<std::uint32_t>(entry.comment.size()));
        put16(out, 0); // номер тома
        put16(out, entry.internalAttributes);
        put32(out, entry.externalAttributes);
        put32(out, offsets[i]);
        out.insert(out.end(), entry.name.begin(), entry.name.end());
        append(out, entry.centralExtra);
        append(out, entry.comment);
    }
    const auto directorySize = static_cast<std::uint32_t>(out.size()) - directoryAt;
    put32(out, kEndSignature);
    put16(out, 0);
    put16(out, 0);
    put16(out, static_cast<std::uint32_t>(archive.entries.size()));
    put16(out, static_cast<std::uint32_t>(archive.entries.size()));
    put32(out, directorySize);
    put32(out, directoryAt);
    put16(out, static_cast<std::uint32_t>(archive.comment.size()));
    append(out, archive.comment);
    return out;
}

Result<Bytes> entryContent(const ZipEntry& entry) {
    if (entry.method == 0) {
        // У записей без сжатия CRC тоже обязателен: иначе порча файла пройдёт незамеченной.
        if (entry.data.size() != entry.size || crcOf(entry.data) != entry.crc) {
            return makeError(ErrorCode::Format, "ZIP: data of “%1” is corrupt", {entry.name});
        }
        return entry.data;
    }
    if (entry.method != 8) {
        return makeError(ErrorCode::Format, "ZIP: “%1” is compressed with unknown method %2", {entry.name, std::to_string(entry.method)});
    }
    auto content = inflateRaw(entry.data, entry.size);
    if (!content || crcOf(*content) != entry.crc) {
        return makeError(ErrorCode::Format, "ZIP: data of “%1” is corrupt", {entry.name});
    }
    return std::move(*content);
}

Result<void> setContent(ZipEntry& entry, std::span<const std::uint8_t> content) {
    auto current = entryContent(entry);
    if (!current) {
        return std::unexpected(std::move(current.error()));
    }
    if (std::ranges::equal(*current, content)) {
        return {}; // не изменилось — сжатые данные остаются прежними
    }
    if (entry.method == 8) {
        auto packed = deflateRaw(content);
        if (!packed) {
            return makeError(ErrorCode::Format, "ZIP: “%1” could not be compressed", {entry.name});
        }
        entry.data = std::move(*packed);
    } else {
        entry.data.assign(content.begin(), content.end());
    }
    entry.crc = crcOf(content);
    entry.size = static_cast<std::uint32_t>(content.size());
    return {};
}

ZipEntry makeEntry(const std::string& name, std::uint32_t mode, std::span<const std::uint8_t> content,
                   std::int64_t unixTime, bool compress) {
    ZipEntry entry;
    entry.name = name;
    toDos(unixTime, entry.dosTime, entry.dosDate);
    entry.setUnixMode(mode);
    entry.crc = crcOf(content);
    entry.size = static_cast<std::uint32_t>(content.size());
    // Не сжалось (или zlib не справился) — запись хранится без сжатия.
    std::optional<Bytes> packed = compress && !content.empty() ? deflateRaw(content) : std::nullopt;
    if (packed && !packed->empty() && packed->size() < content.size()) {
        entry.method = 8;
        entry.versionNeeded = 20;
        entry.data = std::move(*packed);
    } else {
        entry.method = 0;
        entry.versionNeeded = 10;
        entry.data.assign(content.begin(), content.end());
    }

    // Как у Info-ZIP на пульте: «UT» — время (в заголовке файла — изменения и доступа,
    // в оглавлении — только изменения), «Ux» — владелец root (в оглавлении — пустое).
    const auto time = static_cast<std::uint32_t>(unixTime);
    put16(entry.localExtra, 0x5455);
    put16(entry.localExtra, 9);
    entry.localExtra.push_back(3);
    put32(entry.localExtra, time);
    put32(entry.localExtra, time);
    put16(entry.localExtra, 0x7855);
    put16(entry.localExtra, 4);
    put32(entry.localExtra, 0);
    put16(entry.centralExtra, 0x5455);
    put16(entry.centralExtra, 5);
    entry.centralExtra.push_back(3);
    put32(entry.centralExtra, time);
    put16(entry.centralExtra, 0x7855);
    put16(entry.centralExtra, 0);
    return entry;
}

} // namespace harmony::formats
