/**
 * @file gspm.cpp
 * @brief Проверка образа GSPM Harmony One и индекс команд секции 5.
 */

#include "harmony/core/formats/gspm.h"

#include <algorithm>
#include <cstring>

namespace harmony::formats {

namespace {

constexpr std::size_t kMasterIndex = 0x0C;       ///< Начало мастер-индекса
constexpr std::size_t kButtonMapOffset = 0x63;   ///< `LWJL`
constexpr std::size_t kMinSize = kButtonMapOffset + 5;
constexpr std::size_t kMaxSize = 0x390000;       ///< Дальше лежит резервная копия приложения пульта
constexpr std::size_t kCommandSection = 5;
constexpr std::uint8_t kLaneLow = 0x21;
constexpr std::uint8_t kLaneHigh = 0x43;
constexpr std::size_t kRecordPrefix = 11;        ///< Указатель команды смотрит на байт +11 записи

std::uint32_t u16(std::span<const std::uint8_t> data, std::size_t at) {
    return static_cast<std::uint32_t>(data[at]) | (static_cast<std::uint32_t>(data[at + 1]) << 8);
}

std::uint32_t u24(std::span<const std::uint8_t> data, std::size_t at) {
    return u16(data, at) | (static_cast<std::uint32_t>(data[at + 2]) << 16);
}

std::uint32_t u32(std::span<const std::uint8_t> data, std::size_t at) {
    return u24(data, at) | (static_cast<std::uint32_t>(data[at + 3]) << 24);
}

/// Две XOR-дорожки по полным парам байт до @p end; нечётный хвост не учитывается.
std::pair<std::uint8_t, std::uint8_t> checksum(std::span<const std::uint8_t> data, std::size_t end) {
    std::uint8_t low = kLaneLow;
    std::uint8_t high = kLaneHigh;
    for (std::size_t i = 0; i + 2 <= end; i += 2) {
        low ^= data[i];
        high ^= data[i + 1];
    }
    return {low, high};
}

std::unexpected<Error> formatError(const char* text) {
    return makeError(ErrorCode::Format, text);
}

} // namespace

std::size_t GspmImage::sectionCount() const {
    return static_cast<std::size_t>(
        std::count_if(sections.begin(), sections.end(), [](const auto& s) { return s.has_value(); }));
}

std::size_t GspmCommandIndex::commandCount() const {
    std::size_t total = 0;
    for (const auto& device : devices) {
        total += device.size();
    }
    return total;
}

Result<GspmImage> parseGspm(std::span<const std::uint8_t> payload) {
    if (payload.size() < kMinSize) {
        return formatError("GSPM: the data is shorter than the fixed header");
    }
    if (payload.size() >= kMaxSize) {
        return formatError("GSPM: the data is too large for the remote's configuration area");
    }
    if (std::memcmp(payload.data(), "GSPM", 4) != 0) {
        return formatError("GSPM: wrong signature (not a Harmony One configuration)");
    }
    if (std::memcmp(payload.data() + kButtonMapOffset, "LWJL", 4) != 0) {
        return formatError("GSPM: the button map marker LWJL is missing");
    }

    // Указатель конца — абсолютный адрес PTYY; PTYY занимает последние 4 байта образа.
    const std::uint32_t endVector = u32(payload, 4);
    const std::size_t marker = payload.size() - 4;
    if (std::memcmp(payload.data() + marker, "PTYY", 4) != 0) {
        return formatError("GSPM: the end marker PTYY is missing");
    }
    if (endVector < marker) {
        return formatError("GSPM: the end pointer is below the image size");
    }
    GspmImage image;
    image.base = endVector - static_cast<std::uint32_t>(marker);
    image.checksumOffset = marker - 2;
    image.buttonCount = payload[kButtonMapOffset + 4];

    const auto [low, high] = checksum(payload, image.checksumOffset);
    if (low != payload[image.checksumOffset] || high != payload[image.checksumOffset + 1]) {
        return formatError("GSPM: checksum mismatch, the image is damaged");
    }

    std::vector<std::size_t> seen;
    for (std::size_t i = 0; i < kGspmSectionCount; ++i) {
        const std::uint32_t absolute = u32(payload, kMasterIndex + i * 4);
        if (absolute == 0) {
            continue;
        }
        if (absolute < image.base || absolute - image.base >= image.checksumOffset) {
            return formatError("GSPM: a section points outside the image");
        }
        const std::size_t relative = absolute - image.base;
        if (std::find(seen.begin(), seen.end(), relative) != seen.end()) {
            return formatError("GSPM: two sections share one address");
        }
        seen.push_back(relative);
        image.sections[i] = relative;
    }
    if (image.sections[kGspmAbsentSection] || image.sectionCount() != kGspmSectionCount - 1) {
        return formatError("GSPM: the master index is not the 19-section layout of architecture 12");
    }
    return image;
}

std::optional<GspmRange> gspmSectionRange(const GspmImage& image, std::size_t index) {
    if (index >= kGspmSectionCount || !image.sections[index]) {
        return std::nullopt;
    }
    const std::size_t begin = *image.sections[index];
    std::size_t end = image.checksumOffset;
    for (const auto& other : image.sections) {
        if (other && *other > begin && *other < end) {
            end = *other;
        }
    }
    return GspmRange{begin, end};
}

Result<GspmCommandIndex> readGspmCommands(std::span<const std::uint8_t> payload, const GspmImage& image) {
    const auto range = gspmSectionRange(image, kCommandSection);
    if (!range || range->end - range->begin < 1) {
        return formatError("GSPM: the command section is missing");
    }

    // Абсолютный указатель → смещение в образе; nullopt, если не помещается @p need байт.
    const auto resolve = [&](std::uint32_t absolute, std::size_t need) -> std::optional<std::size_t> {
        if (absolute < image.base) {
            return std::nullopt;
        }
        const std::size_t offset = absolute - image.base;
        if (offset > image.checksumOffset || need > image.checksumOffset - offset) {
            return std::nullopt;
        }
        return offset;
    };

    const std::size_t deviceCount = payload[range->begin];
    if (range->end - range->begin < 1 + 3 * deviceCount) {
        return formatError("GSPM: the device table is cut off");
    }

    GspmCommandIndex index;
    for (std::size_t device = 0; device < deviceCount; ++device) {
        const auto table = resolve(u24(payload, range->begin + 1 + 3 * device), 3);
        if (!table) {
            return formatError("GSPM: a device table points outside the image");
        }
        const std::size_t count = u16(payload, *table + 1);
        if (3 + 3 * count > image.checksumOffset - *table) {
            return formatError("GSPM: a command table is cut off");
        }

        auto& commands = index.devices.emplace_back();
        for (std::size_t command = 0; command < count; ++command) {
            const auto target = u24(payload, *table + 3 + 3 * command);
            // Указатель смотрит на байт +11 записи; режим лежит в +15, указатели сигналов в +16, +19, +22.
            if (target < image.base + kRecordPrefix) {
                return formatError("GSPM: a command record points outside the image");
            }
            const auto record = resolve(target - static_cast<std::uint32_t>(kRecordPrefix), 25);
            if (!record) {
                return formatError("GSPM: a command record points outside the image");
            }
            GspmCommand entry;
            entry.deviceOrdinal = static_cast<int>(device);
            entry.commandOrdinal = static_cast<int>(command);
            entry.record = *record;
            entry.toggles = payload[*record + 15] == 2;

            const auto press = resolve(u24(payload, *record + 16), 2);
            if (!press) {
                return formatError("GSPM: a press signal points outside the image");
            }
            entry.press = *press;
            if (const auto hold = u24(payload, *record + 19); hold != 0) {
                if (const auto at = resolve(hold, 2)) {
                    entry.hold = *at;
                }
            }
            if (const auto release = u24(payload, *record + 22); release != 0) {
                if (const auto at = resolve(release, 2)) {
                    entry.release = *at;
                }
            }
            commands.push_back(entry);
        }
    }
    return index;
}

} // namespace harmony::formats
