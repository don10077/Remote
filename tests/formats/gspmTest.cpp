/**
 * @file gspmTest.cpp
 * @brief Тесты разбора образа GSPM (Harmony One) на синтетическом образе, собранном по описанию формата.
 *
 * Настоящего дампа One у нас нет: образ собирается здесь же по описанию формата
 * (docs/harmony_one/gspm.md проектов afterglow и re-harmony и reference/config-format/
 * container.md проекта harmony-explorations). Тесты проверяют, что разбор следует
 * описанию, а не что описание совпадает с пультом.
 */

#include "harmony/core/formats/gspm.h"

#include "harmony/core/formats/container.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <functional>
#include <string>
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

std::uint32_t at(const Image& image, std::size_t offset) {
    return static_cast<std::uint32_t>(image[offset]) | (static_cast<std::uint32_t>(image[offset + 1]) << 8)
           | (static_cast<std::uint32_t>(image[offset + 2]) << 16);
}

/// Заголовок, N слотов и таблица `{spare, u24}` с маркером на `0x0B + 4N`; пустые слоты — @p empty.
Image makeContainer(std::uint32_t base, std::size_t slots, std::initializer_list<std::size_t> empty) {
    Image image(kSignals + 16, 0);
    image[0] = 'G'; image[1] = 'S'; image[2] = 'P'; image[3] = 'M';
    image[9] = static_cast<std::uint8_t>(slots);
    for (std::size_t slot = 0; slot < slots; ++slot) {
        if (std::ranges::find(empty, slot) != empty.end()) {
            continue;
        }
        put(image, kContainerTableOffset + 4 * slot + 1,
            base + static_cast<std::uint32_t>(kSectionsAt + kSectionStep * slot), 3);
    }
    const std::size_t marker = kContainerTableOffset + 4 * slots;
    image[marker] = 'L'; image[marker + 1] = 'W'; image[marker + 2] = 'J'; image[marker + 3] = 'L';
    image[marker + 4] = 55;
    return image;
}

/// @p mutate правит байты до закрытия; сумма покрывает и указатель конца (так делает gspm.seal).
void seal(Image& image, std::uint32_t base, const std::function<void(Image&)>& mutate = {}) {
    if (mutate) {
        mutate(image);
    }
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
}

/// Образ One с одним устройством и двумя командами: первая обычная с удержанием и
/// отпусканием, вторая переключающая (режим 2). @p base — адрес отображения.
Image makeImage(std::uint32_t base, const std::function<void(Image&)>& mutate = {}) {
    auto image = makeContainer(base, kGspmRawSlotCount, {8, 20, 21});

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

    seal(image, base, mutate);
    return image;
}

/// Образ 600–700: тот же признак `GSPM`, но 20 слотов и пустые 18 и 19.
Image make665Image(std::uint32_t base = 0x030000) {
    auto image = makeContainer(base, 20, {18, 19});
    seal(image, base);
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
    EXPECT_EQ(parsed->checksumOffset, image.size() - 6);

    // Пустые сырые слоты: вставной 8 и базовые 18 и 19, которые у One лежат на 20 и 21.
    EXPECT_FALSE(parsed->sections[8]);
    EXPECT_FALSE(parsed->sections[20]);
    EXPECT_FALSE(parsed->sections[21]);
    // Секция 18 — та, которой нет у других архитектур; базовый слот 17 — на сыром 19.
    ASSERT_TRUE(parsed->sections[kGspmEmptySlots[1] - 2]);
    EXPECT_EQ(*parsed->sections[kGspmEmptySlots[1] - 2], kSectionsAt + kSectionStep * 18);
    ASSERT_TRUE(parsed->sections[gspmRawSlot(17)]);
    EXPECT_EQ(*parsed->sections[gspmRawSlot(17)], kSectionsAt + kSectionStep * 19);

    const auto range = gspmSectionRange(*parsed, 5);
    ASSERT_TRUE(range);
    EXPECT_EQ(range->begin, kSectionsAt + kSectionStep * 5);
    EXPECT_EQ(range->end - range->begin, kSectionStep);
    // Диапазон базового слота 16 обрывается на секции 18, а не на 17: секции идут по сырым слотам.
    const auto beforeExtra = gspmSectionRange(*parsed, 16);
    ASSERT_TRUE(beforeExtra);
    EXPECT_EQ(beforeExtra->end, kSectionsAt + kSectionStep * 18);
    EXPECT_FALSE(gspmSectionRange(*parsed, 18));
    EXPECT_FALSE(gspmSectionRange(*parsed, 19));
    EXPECT_FALSE(gspmSectionRange(*parsed, kGspmBaseSlotCount));
}

TEST(Gspm, RawSlotsOfArchitecture12) {
    // Карта слотов arch 12: пустой вставной в 8 и ещё одна секция в 18.
    EXPECT_EQ(gspmRawSlot(0), 0u);
    EXPECT_EQ(gspmRawSlot(7), 7u);
    EXPECT_EQ(gspmRawSlot(8), 9u);
    EXPECT_EQ(gspmRawSlot(16), 17u);
    EXPECT_EQ(gspmRawSlot(17), 19u);
    EXPECT_EQ(gspmRawSlot(18), 20u);
    EXPECT_EQ(gspmRawSlot(19), 21u);
    EXPECT_EQ(kGspmRawSlotCount, kGspmBaseSlotCount + 2); // вставной 8 и ещё одна секция в 18
    EXPECT_EQ(kGspmRawSlotCount - kGspmEmptySlots.size(), 19u);
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

TEST(Gspm, RejectsBrokenSectionTable) {
    // Секция указывает за пределы образа.
    auto outside = makeImage(0x040000, [](Image& i) { put(i, 0x0C, 0x7F0000, 3); });
    EXPECT_FALSE(parseGspm(outside));

    // Две секции с одним адресом.
    auto shared = makeImage(0x040000, [](Image& i) {
        put(i, kContainerTableOffset + 4 * 1 + 1, at(i, kContainerTableOffset + 1), 3);
    });
    EXPECT_FALSE(parseGspm(shared));

    // Заполнен слот 8, который arch 12 оставляет пустым; адреса при этом растут.
    auto withEighth = makeImage(0x040000, [](Image& i) {
        put(i, kContainerTableOffset + 4 * 8 + 1, 0x040000 + 0x170, 3); // между слотами 7 и 9
    });
    const auto filled = parseGspm(withEighth);
    ASSERT_FALSE(filled);
    EXPECT_NE(filled.error().message.find("leaves it empty"), std::string::npos) << filled.error().message;
}

TEST(Gspm, RejectsContainerOfAnotherArchitecture) {
    // 600–700 — тот же признак `GSPM`, но 20 слотов: One его принять не должен.
    const auto image = make665Image();
    const auto other = parseGspm(image);
    ASSERT_FALSE(other);
    EXPECT_NE(other.error().message.find("architecture 14"), std::string::npos) << other.error().message;
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
