/**
 * @file gspmTest.cpp
 * @brief Тесты разбора образа GSPM (Harmony One) на синтетическом образе, собранном по описанию формата.
 *
 * Настоящего дампа One у нас нет: образ собирается здесь же по docs/harmony_one/gspm.md
 * проектов afterglow и re-harmony. Тесты проверяют, что разбор следует описанию,
 * а не что описание совпадает с пультом.
 */

#include "harmony/core/formats/gspm.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <vector>

using namespace harmony::formats;

namespace {

using Image = std::vector<std::uint8_t>;

constexpr std::size_t kSectionsAt = 0x80;
constexpr std::size_t kSectionStep = 0x20;
constexpr std::size_t kDeviceTable = 0x300;
constexpr std::size_t kRecordA = 0x320;
constexpr std::size_t kRecordB = 0x360;
constexpr std::size_t kSignals = 0x3A0;

void put(Image& image, std::size_t at, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        image[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

/// Образ с одним устройством и двумя командами: первая обычная с удержанием и отпусканием,
/// вторая переключающая (режим 2). @p base — адрес отображения; @p mutate правит байты до закрытия.
Image makeImage(std::uint32_t base, const std::function<void(Image&)>& mutate = {}) {
    Image image(kSignals + 16, 0);
    image[0] = 'G'; image[1] = 'S'; image[2] = 'P'; image[3] = 'M';
    put(image, 8, 0x1600, 4);
    for (std::size_t i = 0; i < kGspmSectionCount; ++i) {
        if (i == kGspmAbsentSection) {
            continue;
        }
        put(image, 0x0C + 4 * i, base + static_cast<std::uint32_t>(kSectionsAt + kSectionStep * i), 4);
    }
    image[0x63] = 'L'; image[0x64] = 'W'; image[0x65] = 'J'; image[0x66] = 'L';
    image[0x67] = 55;

    // Секция 5: одно устройство, его таблица лежит ниже.
    const std::size_t section5 = kSectionsAt + kSectionStep * 5;
    image[section5] = 1;
    put(image, section5 + 1, base + static_cast<std::uint32_t>(kDeviceTable), 3);

    image[kDeviceTable] = 0;
    put(image, kDeviceTable + 1, 2, 2);
    put(image, kDeviceTable + 3, base + static_cast<std::uint32_t>(kRecordA + 11), 3);
    put(image, kDeviceTable + 6, base + static_cast<std::uint32_t>(kRecordB + 11), 3);

    image[kRecordA + 15] = 1;
    put(image, kRecordA + 16, base + static_cast<std::uint32_t>(kSignals), 3);
    put(image, kRecordA + 19, base + static_cast<std::uint32_t>(kSignals + 4), 3);
    put(image, kRecordA + 22, base + static_cast<std::uint32_t>(kSignals + 8), 3);

    image[kRecordB + 15] = 2;
    put(image, kRecordB + 16, base + static_cast<std::uint32_t>(kSignals), 3);

    if (mutate) {
        mutate(image);
    }

    // Закрытие: сумма из двух дорожек по полным парам, затем PTYY; указатель конца — на PTYY.
    // Указатель конца пишется до суммы: сумма покрывает и его (так делает gspm.seal в afterglow).
    const std::size_t sumAt = image.size();
    image.resize(sumAt + 6, 0);
    put(image, 4, base + static_cast<std::uint32_t>(sumAt + 2), 4);
    std::uint8_t low = 0x21;
    std::uint8_t high = 0x43;
    // В тестах размер тела чётный, нечётный хвост здесь не проверяется.
    for (std::size_t i = 0; i + 2 <= sumAt; i += 2) {
        low ^= image[i];
        high ^= image[i + 1];
    }
    image[sumAt] = low;
    image[sumAt + 1] = high;
    image[sumAt + 2] = 'P'; image[sumAt + 3] = 'T'; image[sumAt + 4] = 'Y'; image[sumAt + 5] = 'Y';
    return image;
}

} // namespace

TEST(Gspm, ParsesStructureOfSyntheticImage) {
    const auto image = makeImage(0x040000);
    const auto parsed = parseGspm(image);
    ASSERT_TRUE(parsed) << parsed.error().message;
    EXPECT_EQ(parsed->base, 0x040000u);
    EXPECT_EQ(parsed->buttonCount, 55);
    EXPECT_EQ(parsed->sectionCount(), 19u);
    EXPECT_FALSE(parsed->sections[kGspmAbsentSection]);
    EXPECT_EQ(parsed->checksumOffset, image.size() - 6);

    const auto range = gspmSectionRange(*parsed, 5);
    ASSERT_TRUE(range);
    EXPECT_EQ(range->begin, kSectionsAt + kSectionStep * 5);
    EXPECT_EQ(range->end - range->begin, kSectionStep);
    EXPECT_FALSE(gspmSectionRange(*parsed, kGspmAbsentSection));
    EXPECT_FALSE(gspmSectionRange(*parsed, 20));
}

TEST(Gspm, BaseIsDerivedFromEndPointerNotFixed) {
    // Заводская конфигурация прошивки отображена по 0x2000, а не по 0x040000.
    const auto image = makeImage(0x2000);
    const auto parsed = parseGspm(image);
    ASSERT_TRUE(parsed) << parsed.error().message;
    EXPECT_EQ(parsed->base, 0x2000u);
    const auto commands = readGspmCommands(image, *parsed);
    ASSERT_TRUE(commands) << commands.error().message;
    EXPECT_EQ(commands->commandCount(), 2u);
}

TEST(Gspm, ReadsCommandIndexOfSection5) {
    const auto image = makeImage(0x040000);
    const auto parsed = parseGspm(image);
    ASSERT_TRUE(parsed);
    const auto commands = readGspmCommands(image, *parsed);
    ASSERT_TRUE(commands) << commands.error().message;
    ASSERT_EQ(commands->devices.size(), 1u);
    ASSERT_EQ(commands->devices[0].size(), 2u);

    const auto& first = commands->devices[0][0];
    EXPECT_EQ(first.id(), 0x0000);
    EXPECT_FALSE(first.toggles);
    EXPECT_EQ(first.record, kRecordA);
    EXPECT_EQ(first.press, kSignals);
    ASSERT_TRUE(first.hold);
    EXPECT_EQ(*first.hold, kSignals + 4);
    ASSERT_TRUE(first.release);
    EXPECT_EQ(*first.release, kSignals + 8);

    const auto& second = commands->devices[0][1];
    EXPECT_EQ(second.id(), 0x0001);
    EXPECT_TRUE(second.toggles);
    EXPECT_FALSE(second.hold);
    EXPECT_FALSE(second.release);
}

TEST(Gspm, RejectsDamagedImages) {
    auto flipped = makeImage(0x040000);
    flipped[kSignals] ^= 0xFF;
    const auto bad = parseGspm(flipped);
    ASSERT_FALSE(bad);
    EXPECT_EQ(bad.error().code, harmony::ErrorCode::Format);

    auto noSignature = makeImage(0x040000, [](Image& i) { i[0] = 'X'; });
    EXPECT_FALSE(parseGspm(noSignature));

    auto noMarker = makeImage(0x040000, [](Image& i) { i[0x63] = 'X'; });
    EXPECT_FALSE(parseGspm(noMarker));

    auto noEnd = makeImage(0x040000);
    noEnd[noEnd.size() - 1] = 'X';
    EXPECT_FALSE(parseGspm(noEnd));

    EXPECT_FALSE(parseGspm(Image(16, 0)));
}

TEST(Gspm, RejectsBrokenMasterIndex) {
    // Секция указывает за пределы образа.
    auto outside = makeImage(0x040000, [](Image& i) { put(i, 0x0C, 0x7F0000, 4); });
    EXPECT_FALSE(parseGspm(outside));

    // Две секции с одним адресом.
    auto shared = makeImage(0x040000, [](Image& i) {
        const auto first = static_cast<std::uint32_t>(i[0x0C]) | (static_cast<std::uint32_t>(i[0x0D]) << 8) |
                           (static_cast<std::uint32_t>(i[0x0E]) << 16) | (static_cast<std::uint32_t>(i[0x0F]) << 24);
        put(i, 0x10, first, 4);
    });
    EXPECT_FALSE(parseGspm(shared));

    // Появилась секция 8, которой в образах One нет.
    auto withEighth = makeImage(0x040000, [](Image& i) { put(i, 0x0C + 4 * 8, 0x040000 + 0x2F0, 4); });
    EXPECT_FALSE(parseGspm(withEighth));
}

TEST(Gspm, CommandIndexStaysInsideImage) {
    auto image = makeImage(0x040000, [](Image& i) {
        put(i, kRecordA + 16, 0x7F0000, 3); // сигнал нажатия вне образа
    });
    const auto parsed = parseGspm(image);
    ASSERT_TRUE(parsed) << parsed.error().message;
    EXPECT_FALSE(readGspmCommands(image, *parsed));

    auto cut = makeImage(0x040000, [](Image& i) {
        put(i, kDeviceTable + 1, 60000, 2); // таблица заявляет больше команд, чем есть места
    });
    const auto parsedCut = parseGspm(cut);
    ASSERT_TRUE(parsedCut);
    EXPECT_FALSE(readGspmCommands(cut, *parsedCut));
}
