/**
 * @file gspmScreensTest.cpp
 * @brief Тесты экранов (секция 6) и тач-зон (секция 19) One на образе, собранном по описанию afterglow.
 */

#include "harmony/core/formats/gspmScreens.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <vector>

using namespace harmony::formats;

namespace {

using Image = std::vector<std::uint8_t>;

constexpr std::uint32_t kBase = 0x040000;
constexpr std::size_t kSectionsAt = 0x80;
constexpr std::size_t kSectionStep = 0x20;
constexpr std::size_t kSize = 0x400;

constexpr std::size_t kSection6 = kSectionsAt + kSectionStep * 6;
constexpr std::size_t kSection19 = kSectionsAt + kSectionStep * 19;
constexpr std::size_t kTrailer0 = 0x300;
constexpr std::size_t kSlot0 = 0x310;
constexpr std::size_t kRegister0 = 0x320;
constexpr std::size_t kProgram0 = 0x340;
constexpr std::size_t kTrailer1 = 0x350;
constexpr std::size_t kSlot1a = 0x360;
constexpr std::size_t kSlot1b = 0x370;
constexpr std::size_t kRegister1 = 0x380;
constexpr std::size_t kProgram1 = 0x390;
constexpr std::size_t kHeader = 0x3A0;
constexpr std::size_t kTemplate = 0x3B0;
constexpr std::size_t kZone0 = 0x3C0;
constexpr std::size_t kZone1 = 0x3D0;

void put(Image& image, std::size_t at, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        image[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

std::uint32_t address(std::size_t offset) {
    return kBase + static_cast<std::uint32_t>(offset);
}

Image makeImage(const std::function<void(Image&)>& mutate = {}) {
    Image image(kSize, 0);
    image[0] = 'G'; image[1] = 'S'; image[2] = 'P'; image[3] = 'M';
    put(image, 8, 0x1600, 4);
    for (std::size_t i = 0; i < kGspmSectionCount; ++i) {
        if (i != kGspmAbsentSection) {
            put(image, 0x0C + 4 * i, address(kSectionsAt + kSectionStep * i), 4);
        }
    }
    image[0x63] = 'L'; image[0x64] = 'W'; image[0x65] = 'J'; image[0x66] = 'L';
    image[0x67] = 55;

    // Секция 6: два экрана.
    put(image, kSection6, 2, 2);
    put(image, kSection6 + 3, address(kTrailer0), 3);
    put(image, kSection6 + 6, address(kTrailer1), 3);

    // Экран 0: один слот с регистром из двух записей.
    image[kTrailer0] = 0;
    put(image, kTrailer0 + 1, address(kHeader), 3);
    put(image, kTrailer0 + 4, 1, 2);
    put(image, kTrailer0 + 6, address(kSlot0), 3);
    image[kSlot0] = 4;
    put(image, kSlot0 + 1, address(kRegister0), 3);
    put(image, kSlot0 + 4, address(kProgram0), 3);
    image[kRegister0] = 2;
    image[kRegister0 + 1] = 0xB0; put(image, kRegister0 + 2, 0x0102, 2); image[kRegister0 + 4] = 7;
    image[kRegister0 + 5] = 0xB1; put(image, kRegister0 + 6, 0x0304, 2); image[kRegister0 + 8] = 9;

    // Экран 1: флаг 1, два слота с пустыми регистрами.
    image[kTrailer1] = 1;
    put(image, kTrailer1 + 1, address(kHeader), 3);
    put(image, kTrailer1 + 4, 2, 2);
    put(image, kTrailer1 + 6, address(kSlot1a), 3);
    put(image, kTrailer1 + 9, address(kSlot1b), 3);
    for (const std::size_t slot : {kSlot1a, kSlot1b}) {
        image[slot] = 5;
        put(image, slot + 1, address(kRegister1), 3);
        put(image, slot + 4, address(kProgram1), 3);
    }
    image[kRegister1] = 0;

    // Секция 19: один шаблон из двух зон.
    image[kSection19] = 1;
    put(image, kSection19 + 1, address(kTemplate), 3);
    image[kTemplate] = 2;
    put(image, kTemplate + 1, address(kZone0), 3);
    put(image, kTemplate + 4, address(kZone1), 3);
    const struct { std::size_t at; int x, w, y, h; std::uint8_t tag; } zones[] = {
        {kZone0, 10, 50, 20, 30, 0x2E}, {kZone1, 70, 60, 120, 40, 0x31}};
    for (const auto& z : zones) {
        put(image, z.at, static_cast<std::uint32_t>(z.x), 2);
        put(image, z.at + 2, static_cast<std::uint32_t>(z.w), 2);
        put(image, z.at + 4, static_cast<std::uint32_t>(z.y), 2);
        put(image, z.at + 6, static_cast<std::uint32_t>(z.h), 2);
        image[z.at + 8] = z.tag;
        put(image, z.at + 9, address(z.at), 3);
    }

    if (mutate) {
        mutate(image);
    }

    const std::size_t sumAt = image.size();
    image.resize(sumAt + 6, 0);
    put(image, 4, address(sumAt + 2), 4);
    std::uint8_t low = 0x21;
    std::uint8_t high = 0x43;
    for (std::size_t i = 0; i + 2 <= sumAt; i += 2) {
        low ^= image[i];
        high ^= image[i + 1];
    }
    image[sumAt] = low;
    image[sumAt + 1] = high;
    image[sumAt + 2] = 'P'; image[sumAt + 3] = 'T'; image[sumAt + 4] = 'Y'; image[sumAt + 5] = 'Y';
    return image;
}

GspmImage parsed(const Image& image) {
    const auto result = parseGspm(image);
    EXPECT_TRUE(result) << (result ? "" : result.error().message);
    return result ? *result : GspmImage{};
}

} // namespace

TEST(GspmScreens, ReadsTrailersSlotsAndKeyRegisters) {
    const auto image = makeImage();
    const auto screens = readGspmScreens(image, parsed(image));
    ASSERT_TRUE(screens) << screens.error().message;
    ASSERT_EQ(screens->size(), 2u);

    const auto& first = (*screens)[0];
    EXPECT_EQ(first.trailer, kTrailer0);
    EXPECT_EQ(first.flag, 0);
    EXPECT_EQ(first.header, kHeader);
    ASSERT_EQ(first.slots.size(), 1u);
    EXPECT_EQ(first.slots[0].templateIndex, 4);
    EXPECT_EQ(first.slots[0].keyRegister, kRegister0);
    EXPECT_EQ(first.slots[0].program, kProgram0);
    ASSERT_EQ(first.slots[0].entries.size(), 2u);
    EXPECT_EQ(first.slots[0].entries[0].keyCode, 0xB0);
    EXPECT_EQ(first.slots[0].entries[0].operand, 0x0102);
    EXPECT_EQ(first.slots[0].entries[0].category, 7);
    EXPECT_EQ(first.slots[0].entries[1].operand, 0x0304);

    const auto& second = (*screens)[1];
    EXPECT_EQ(second.flag, 1);
    ASSERT_EQ(second.slots.size(), 2u);
    EXPECT_TRUE(second.slots[0].entries.empty());
    EXPECT_EQ(second.slots[1].templateIndex, 5);
}

TEST(GspmScreens, RejectsBrokenScreenIndex) {
    {
        const auto image = makeImage([](Image& i) { put(i, kSection6 + 3, 0x7FFFFF, 3); });
        EXPECT_FALSE(readGspmScreens(image, parsed(image)));
    }
    {
        const auto image = makeImage([](Image& i) { put(i, kTrailer0 + 4, 0, 2); });
        EXPECT_FALSE(readGspmScreens(image, parsed(image)));
    }
    {
        const auto image = makeImage([](Image& i) { put(i, kSlot0 + 1, 0x7FFFFF, 3); });
        EXPECT_FALSE(readGspmScreens(image, parsed(image)));
    }
    {
        const auto image = makeImage([](Image& i) { i[kRegister0] = 200; });
        EXPECT_FALSE(readGspmScreens(image, parsed(image)));
    }
    {
        const auto image = makeImage([](Image& i) { i[kSection6 + 2] = 1; });
        EXPECT_FALSE(readGspmScreens(image, parsed(image)));
    }
}

TEST(GspmTouch, ReadsZonesAndKeyCodes) {
    const auto image = makeImage();
    const auto templates = readGspmTouchTemplates(image, parsed(image));
    ASSERT_TRUE(templates) << templates.error().message;
    ASSERT_EQ(templates->size(), 1u);
    const auto& zones = (*templates)[0];
    ASSERT_EQ(zones.size(), 2u);
    EXPECT_EQ(zones[0].x, 10);
    EXPECT_EQ(zones[0].width, 50);
    EXPECT_EQ(zones[0].y, 20);
    EXPECT_EQ(zones[0].height, 30);
    EXPECT_EQ(zones[0].tag, 0x2E);
    EXPECT_EQ(zones[0].keyCode(), 0xAE);
    EXPECT_EQ(zones[1].keyCode(), 0xB1);
}

TEST(GspmTouch, RejectsZoneThatDoesNotClose) {
    {
        const auto image = makeImage([](Image& i) { put(i, kZone1 + 9, address(kZone0), 3); });
        EXPECT_FALSE(readGspmTouchTemplates(image, parsed(image)));
    }
    {
        const auto image = makeImage([](Image& i) { put(i, kTemplate + 1, 0x7FFFFF, 3); });
        EXPECT_FALSE(readGspmTouchTemplates(image, parsed(image)));
    }
    {
        const auto image = makeImage([](Image& i) { i[kTemplate] = 250; });
        EXPECT_FALSE(readGspmTouchTemplates(image, parsed(image)));
    }
    {
        const auto image = makeImage([](Image& i) { put(i, kSection19 + 1, 0x7FFFFF, 3); });
        EXPECT_FALSE(readGspmTouchTemplates(image, parsed(image)));
    }
}
