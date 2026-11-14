/**
 * @file gspmFonts.cpp
 * @brief Разбор шрифтов Harmony One (секция 7) и декодирование глифов.
 */

#include "harmony/core/formats/gspmFonts.h"

namespace harmony::formats {

namespace {

constexpr std::size_t kMaxGlyphRows = 220;  ///< Выше экрана глиф не бывает
constexpr std::size_t kMaxGlyphWidth = 176; ///< Шире экрана тоже

std::unexpected<Error> bad(const char* text) {
    return makeError(ErrorCode::Format, text);
}

std::uint32_t u24(std::span<const std::uint8_t> d, std::size_t at) {
    return static_cast<std::uint32_t>(d[at]) | (static_cast<std::uint32_t>(d[at + 1]) << 8) |
           (static_cast<std::uint32_t>(d[at + 2]) << 16);
}

/// Указатель глифа разрешается: нулевой (глифа нет) или попадает в образ.
bool resolves(std::span<const std::uint8_t> d, std::uint32_t base, std::size_t at) {
    const std::uint32_t pointer = u24(d, at);
    return pointer == 0 || (pointer >= base && pointer - base < d.size());
}

/// Из двух заголовков выбирается тот, при котором все указатели разрешаются.
std::optional<std::pair<std::size_t, bool>> glyphCount(std::span<const std::uint8_t> d, std::uint32_t base,
                                                       std::size_t offset) {
    const std::size_t candidates[2] = {
        static_cast<std::size_t>(d[offset + 1]) | (static_cast<std::size_t>(d[offset + 2]) << 8),
        d[offset + 2]};
    for (int variant = 0; variant < 2; ++variant) {
        const std::size_t slots = candidates[variant];
        if (slots == 0 || offset + 3 + 3 * slots > d.size()) {
            continue;
        }
        bool all = true;
        for (std::size_t i = 0; i < slots && all; ++i) {
            all = resolves(d, base, offset + 3 + 3 * i);
        }
        if (all) {
            return std::pair{slots, variant == 1};
        }
    }
    return std::nullopt;
}

} // namespace

std::uint64_t GspmGlyph::fingerprint() const {
    std::uint64_t hash = 1469598103934665603ull;
    const auto feed = [&hash](std::uint8_t byte) {
        hash ^= byte;
        hash *= 1099511628211ull;
    };
    feed(static_cast<std::uint8_t>(width));
    feed(static_cast<std::uint8_t>(height));
    for (const auto& row : rows) {
        for (const auto value : row) {
            feed(value);
        }
    }
    return hash;
}

std::size_t GspmFont::presentCount() const {
    std::size_t total = 0;
    for (const auto& glyph : glyphs) {
        total += glyph ? 1 : 0;
    }
    return total;
}

Result<GspmGlyph> decodeGspmGlyph(std::span<const std::uint8_t> d, std::size_t offset) {
    if (offset >= d.size()) {
        return bad("GSPM glyph: starts outside the image");
    }
    const int declared = d[offset];
    std::size_t cursor = offset + 1;
    GspmGlyph glyph;
    std::vector<std::uint8_t> row;
    std::optional<std::size_t> width;
    bool closed = false;
    while (cursor < d.size()) {
        const std::uint8_t control = d[cursor++];
        if (control == 0) {
            if (!row.empty()) {
                glyph.rows.push_back(std::move(row));
            }
            closed = true;
            break;
        }
        if (control == 0x80) {
            if (!width) {
                width = row.size();
            } else if (row.size() != *width) {
                return bad("GSPM glyph: rows have different widths");
            }
            glyph.rows.push_back(std::move(row));
            row.clear();
            if (glyph.rows.size() > kMaxGlyphRows) {
                return bad("GSPM glyph: taller than the display");
            }
            continue;
        }
        const std::size_t length = control & 0x7Fu;
        if ((control & 0x80) != 0) {
            row.insert(row.end(), length, 0);
        } else {
            if (cursor + 2 * length > d.size()) {
                return bad("GSPM glyph: a literal run is cut off");
            }
            row.insert(row.end(), length, 1);
            cursor += 2 * length;
        }
        if (row.size() > kMaxGlyphWidth) {
            return bad("GSPM glyph: wider than the display");
        }
    }
    if (!closed) {
        return bad("GSPM glyph: no terminator");
    }
    if (glyph.rows.empty()) {
        return bad("GSPM glyph: no rows");
    }
    const std::size_t finalWidth = width.value_or(glyph.rows.front().size());
    for (const auto& item : glyph.rows) {
        if (item.size() != finalWidth) {
            return bad("GSPM glyph: rows have different widths");
        }
    }
    if (static_cast<std::size_t>(declared) != finalWidth) {
        return bad("GSPM glyph: declared width differs from the decoded one");
    }
    glyph.width = declared;
    glyph.height = static_cast<int>(glyph.rows.size());
    glyph.encodedSize = cursor - offset;
    return glyph;
}

Result<std::vector<GspmFont>> readGspmFontsAt(std::span<const std::uint8_t> d, std::uint32_t base,
                                              std::size_t begin, std::size_t end) {
    if (end > d.size() || begin + 2 > end) {
        return bad("GSPM fonts: section 7 is outside the image");
    }
    const std::size_t count = static_cast<std::size_t>(d[begin]) | (static_cast<std::size_t>(d[begin + 1]) << 8);
    if (begin + 2 + 3 * count > end) {
        return bad("GSPM fonts: the font index exceeds its section");
    }
    std::vector<GspmFont> fonts;
    for (std::size_t n = 0; n < count; ++n) {
        const std::uint32_t pointer = u24(d, begin + 2 + 3 * n);
        if (pointer < base || pointer - base + 3 > d.size()) {
            return bad("GSPM fonts: a font pointer is outside the image");
        }
        GspmFont font;
        font.offset = pointer - base;
        font.height = d[font.offset];
        const auto counted = glyphCount(d, base, font.offset);
        if (!counted) {
            return bad("GSPM fonts: no glyph count resolves for a font");
        }
        font.byteCount = counted->second;
        for (std::size_t i = 0; i < counted->first; ++i) {
            const std::uint32_t glyphPointer = u24(d, font.offset + 3 + 3 * i);
            if (glyphPointer == 0) {
                font.glyphs.emplace_back(std::nullopt);
                continue;
            }
            auto glyph = decodeGspmGlyph(d, glyphPointer - base);
            if (!glyph) {
                return std::unexpected(glyph.error());
            }
            font.glyphs.emplace_back(std::move(*glyph));
        }
        fonts.push_back(std::move(font));
    }
    return fonts;
}

Result<std::vector<GspmFont>> readGspmFonts(std::span<const std::uint8_t> payload, const GspmImage& image) {
    const auto range = gspmSectionRange(image, 7);
    if (!range) {
        return bad("GSPM fonts: section 7 is missing");
    }
    return readGspmFontsAt(payload, image.base, range->begin, range->end);
}

} // namespace harmony::formats
