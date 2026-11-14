/**
 * @file gspmFontsTest.cpp
 * @brief Тесты шрифтов и глифов One на данных, собранных по описанию afterglow.
 */

#include "harmony/core/formats/gspmFonts.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace harmony::formats;

namespace {

using Image = std::vector<std::uint8_t>;

constexpr std::uint32_t kBase = 0x040000;
constexpr std::size_t kSection = 0x10;  // секция 7: до 0x40
constexpr std::size_t kFontA = 0x40;
constexpr std::size_t kFontB = 0x70;
constexpr std::size_t kGlyphT = 0xA0;   // «Т»: 3 колонки, 2 строки
constexpr std::size_t kGlyphDot = 0xC0; // точка 1x1
constexpr std::size_t kEnd = 0xE0;

void put(Image& image, std::size_t at, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        image[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

std::uint32_t address(std::size_t offset) {
    return kBase + static_cast<std::uint32_t>(offset);
}

Image makeImage() {
    Image f(kEnd, 0);
    put(f, kSection, 2, 2);
    put(f, kSection + 2, address(kFontA), 3);
    put(f, kSection + 5, address(kFontB), 3);

    // Шрифт A: обычный, высота 2, u16 число глифов 3: [T, пусто, точка].
    f[kFontA] = 2;
    put(f, kFontA + 1, 3, 2);
    put(f, kFontA + 3, address(kGlyphT), 3);
    put(f, kFontA + 6, 0, 3);
    put(f, kFontA + 9, address(kGlyphDot), 3);

    // Шрифт B: заводской — число глифов в байте +2 (u16 по +1 дал бы 0x0300 — не разрешится).
    f[kFontB] = 2;
    f[kFontB + 1] = 0;
    f[kFontB + 2] = 1;
    put(f, kFontB + 3, address(kGlyphDot), 3);

    // «Т»: строка 1 — 3 пикселя, строка 2 — пропуск 1, пиксель, пропуск 1.
    std::size_t at = kGlyphT;
    f[at++] = 3;
    f[at++] = 0x03; at += 6;               // 3 пикселя RGB565
    f[at++] = 0x80;
    f[at++] = 0x81;
    f[at++] = 0x01; at += 2;
    f[at++] = 0x81;
    f[at++] = 0x80;
    f[at++] = 0x00;

    at = kGlyphDot;
    f[at++] = 1;
    f[at++] = 0x01; at += 2;
    f[at++] = 0x80;
    f[at++] = 0x00;
    return f;
}

} // namespace

TEST(GspmGlyph, DecodesRunsIntoMask) {
    const auto image = makeImage();
    const auto glyph = decodeGspmGlyph(image, kGlyphT);
    ASSERT_TRUE(glyph) << glyph.error().message;
    EXPECT_EQ(glyph->width, 3);
    EXPECT_EQ(glyph->height, 2);
    EXPECT_EQ(glyph->rows[0], (std::vector<std::uint8_t>{1, 1, 1}));
    EXPECT_EQ(glyph->rows[1], (std::vector<std::uint8_t>{0, 1, 0}));
    EXPECT_EQ(glyph->encodedSize, 16u);

    const auto dot = decodeGspmGlyph(image, kGlyphDot);
    ASSERT_TRUE(dot);
    EXPECT_NE(glyph->fingerprint(), dot->fingerprint());
    EXPECT_EQ(dot->fingerprint(), decodeGspmGlyph(image, kGlyphDot)->fingerprint());
}

TEST(GspmGlyph, RejectsBrokenStreams) {
    auto image = makeImage();
    EXPECT_FALSE(decodeGspmGlyph(image, kEnd + 10)); // вне образа

    auto width = makeImage();
    width[kGlyphT] = 4; // заявленная ширина не совпадает
    EXPECT_FALSE(decodeGspmGlyph(width, kGlyphT));

    auto ragged = makeImage();
    ragged[kGlyphT + 9] = 0x82; // вторая строка шире первой
    EXPECT_FALSE(decodeGspmGlyph(ragged, kGlyphT));

    auto cut = makeImage();
    cut[kGlyphT + 1] = 0x7F; // литерал длиннее образа
    EXPECT_FALSE(decodeGspmGlyph(cut, kGlyphT));

    auto zero = makeImage();
    zero[kGlyphT + 1] = 0x00; // сразу конец: строк нет
    EXPECT_FALSE(decodeGspmGlyph(zero, kGlyphT));
}

TEST(GspmFonts, ReadsBothHeaderVariants) {
    const auto image = makeImage();
    const auto fonts = readGspmFontsAt(image, kBase, kSection, kFontA);
    ASSERT_TRUE(fonts) << fonts.error().message;
    ASSERT_EQ(fonts->size(), 2u);

    const auto& a = (*fonts)[0];
    EXPECT_EQ(a.height, 2);
    EXPECT_FALSE(a.byteCount);
    ASSERT_EQ(a.glyphs.size(), 3u);
    EXPECT_TRUE(a.glyphs[0]);
    EXPECT_FALSE(a.glyphs[1]);
    EXPECT_TRUE(a.glyphs[2]);
    EXPECT_EQ(a.presentCount(), 2u);

    const auto& b = (*fonts)[1];
    EXPECT_TRUE(b.byteCount);
    ASSERT_EQ(b.glyphs.size(), 1u);
    EXPECT_EQ(b.glyphs[0]->fingerprint(), a.glyphs[2]->fingerprint());
}

TEST(GspmFonts, RejectsBrokenIndex) {
    auto pointer = makeImage();
    put(pointer, kSection + 2, 0x7FFFFF, 3);
    EXPECT_FALSE(readGspmFontsAt(pointer, kBase, kSection, kFontA));

    auto count = makeImage();
    put(count, kSection, 1000, 2);
    EXPECT_FALSE(readGspmFontsAt(count, kBase, kSection, kFontA));

    auto glyphs = makeImage();
    put(glyphs, kFontA + 3, 0x7FFFFF, 3); // ни один из заголовков не разрешится
    EXPECT_FALSE(readGspmFontsAt(glyphs, kBase, kSection, kFontA));

    EXPECT_FALSE(readGspmFontsAt(makeImage(), kBase, kSection, 0x10000));
}
