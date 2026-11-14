/**
 * @file gspmScreens.cpp
 * @brief Разбор экранов (секция 6) и тач-зон (секция 19) образа One.
 */

#include "harmony/core/formats/gspmScreens.h"

namespace harmony::formats {

namespace {

std::unexpected<Error> bad(const char* text) {
    return makeError(ErrorCode::Format, text);
}

std::uint32_t u16(std::span<const std::uint8_t> d, std::size_t at) {
    return static_cast<std::uint32_t>(d[at]) | (static_cast<std::uint32_t>(d[at + 1]) << 8);
}

std::uint32_t u24(std::span<const std::uint8_t> d, std::size_t at) {
    return u16(d, at) | (static_cast<std::uint32_t>(d[at + 2]) << 16);
}

/// Абсолютный указатель → смещение в образе; nullopt — вне образа.
std::optional<std::size_t> resolve(std::span<const std::uint8_t> d, const GspmImage& image, std::size_t at) {
    const std::uint32_t pointer = u24(d, at);
    if (pointer < image.base || pointer - image.base >= d.size()) {
        return std::nullopt;
    }
    return pointer - image.base;
}

bool fits(std::span<const std::uint8_t> d, std::size_t at, std::size_t count) {
    return at <= d.size() && count <= d.size() - at;
}

} // namespace

Result<std::vector<GspmScreen>> readGspmScreens(std::span<const std::uint8_t> d, const GspmImage& image) {
    const auto range = gspmSectionRange(image, 6);
    if (!range || range->end > d.size() || range->begin + 3 > range->end) {
        return bad("GSPM screens: section 6 is missing");
    }
    const std::size_t begin = range->begin;
    const std::size_t count = u16(d, begin);
    if (d[begin + 2] != 0 || begin + 3 + 3 * count > range->end) {
        return bad("GSPM screens: invalid screen index");
    }
    std::vector<GspmScreen> screens;
    for (std::size_t n = 0; n < count; ++n) {
        const auto trailer = resolve(d, image, begin + 3 + 3 * n);
        if (!trailer || !fits(d, *trailer, 6)) {
            return bad("GSPM screens: a trailer is outside the image");
        }
        GspmScreen screen;
        screen.trailer = *trailer;
        screen.flag = d[*trailer];
        screen.header = resolve(d, image, *trailer + 1).value_or(0);
        const std::size_t slotCount = u16(d, *trailer + 4);
        if (slotCount == 0 || !fits(d, *trailer + 6, 3 * slotCount)) {
            return bad("GSPM screens: invalid slot index");
        }
        for (std::size_t s = 0; s < slotCount; ++s) {
            const auto slot = resolve(d, image, *trailer + 6 + 3 * s);
            if (!slot || !fits(d, *slot, 7)) {
                return bad("GSPM screens: a slot is outside the image");
            }
            const auto registerAt = resolve(d, image, *slot + 1);
            const auto program = resolve(d, image, *slot + 4);
            if (!registerAt || !program) {
                return bad("GSPM screens: a slot points outside the image");
            }
            const std::size_t entries = d[*registerAt];
            if (!fits(d, *registerAt + 1, 4 * entries)) {
                return bad("GSPM screens: a key register is cut off");
            }
            GspmScreenSlot item;
            item.templateIndex = d[*slot];
            item.keyRegister = *registerAt;
            item.program = *program;
            for (std::size_t e = 0; e < entries; ++e) {
                const std::size_t at = *registerAt + 1 + 4 * e;
                item.entries.push_back({d[at], static_cast<std::uint16_t>(u16(d, at + 1)), d[at + 3]});
            }
            screen.slots.push_back(std::move(item));
        }
        screens.push_back(std::move(screen));
    }
    return screens;
}

Result<std::vector<GspmTouchTemplate>> readGspmTouchTemplates(std::span<const std::uint8_t> d,
                                                              const GspmImage& image) {
    const auto range = gspmSectionRange(image, 19);
    if (!range || range->end > d.size() || range->begin + 1 > range->end) {
        return bad("GSPM touch: section 19 is missing");
    }
    const std::size_t begin = range->begin;
    const std::size_t count = d[begin];
    if (begin + 1 + 3 * count > range->end) {
        return bad("GSPM touch: the template index exceeds its section");
    }
    std::vector<GspmTouchTemplate> templates;
    for (std::size_t t = 0; t < count; ++t) {
        const auto table = resolve(d, image, begin + 1 + 3 * t);
        if (!table) {
            return bad("GSPM touch: a template is outside the image");
        }
        const std::size_t zoneCount = d[*table];
        if (!fits(d, *table + 1, 3 * zoneCount)) {
            return bad("GSPM touch: a zone index is cut off");
        }
        GspmTouchTemplate zones;
        for (std::size_t z = 0; z < zoneCount; ++z) {
            const auto zone = resolve(d, image, *table + 1 + 3 * z);
            if (!zone || !fits(d, *zone, 12)) {
                return bad("GSPM touch: a zone is outside the image");
            }
            const auto self = resolve(d, image, *zone + 9);
            if (!self || *self != *zone) {
                return bad("GSPM touch: a zone's self-pointer does not close");
            }
            GspmTouchZone item;
            item.x = static_cast<int>(u16(d, *zone));
            item.width = static_cast<int>(u16(d, *zone + 2));
            item.y = static_cast<int>(u16(d, *zone + 4));
            item.height = static_cast<int>(u16(d, *zone + 6));
            item.tag = d[*zone + 8];
            zones.push_back(item);
        }
        templates.push_back(std::move(zones));
    }
    return templates;
}

} // namespace harmony::formats
