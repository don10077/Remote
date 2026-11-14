/**
 * @file ssirSequencesTest.cpp
 * @brief Тесты разбора SsIr.bin пульта 1100 на файле, собранном по описанию afterglow.
 *
 * Настоящего SsIr.bin 1100 у нас нет: проверяется следование описанию, а не пульту.
 */

#include "harmony/core/formats/ssirSequences.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <iterator>
#include <vector>

using namespace harmony::formats;

namespace {

using Image = std::vector<std::uint8_t>;

void put(Image& image, std::size_t at, std::uint32_t value, int bytes) {
    if (image.size() < at + static_cast<std::size_t>(bytes)) {
        image.resize(at + static_cast<std::size_t>(bytes), 0);
    }
    for (int i = 0; i < bytes; ++i) {
        image[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

// Раскладка: заголовок 0..4, сигналы, несущая, запись, таблица устройства, корень.
constexpr std::size_t kPress = 0x10;   // нажатие: пауза 500 мс + кадр + конец сегмента
constexpr std::size_t kHold = 0x30;    // удержание: старт + повторяемый сегмент
constexpr std::size_t kCarrier = 0x60;
constexpr std::size_t kRecordPress = 0x70;
constexpr std::size_t kRecordHold = 0x90;
constexpr std::size_t kDevice = 0xB0;
constexpr std::size_t kRoot = 0xC0;

Image makeFile() {
    Image f(0xD0, 0);
    put(f, 0, 1, 2);
    put(f, 2, kRoot, 3);

    // Нажатие: пауза 0x7FFF + 0x1B58 (длинная, двумя словами), импульс 9000, пауза 4500, импульс 560, 1, 0.
    const std::uint16_t press[] = {0x7FFF, 0x1B58, 0x8000 | 9000, 4500, 0x8000 | 560, 0x0001, 0x0000};
    for (std::size_t i = 0; i < std::size(press); ++i) put(f, kPress + 2 * i, press[i], 2);

    // Удержание: пауза, импульс 9000, пауза 2250, импульс 560, 1, 0 | повтор: импульс 9000, пауза 2250, 1, 0.
    const std::uint16_t hold[] = {1000, 0x8000 | 9000, 2250, 0x8000 | 560, 0x0001, 0x0000,
                                  0x8000 | 9000, 2250, 0x0001, 0x0000};
    for (std::size_t i = 0; i < std::size(hold); ++i) put(f, kHold + 2 * i, hold[i], 2);

    put(f, kCarrier, 0, 1);
    put(f, kCarrier + 1, 26316, 3); // 38 кГц
    put(f, kCarrier + 4, 13158, 3);

    // Запись: n = 1 несущая, m = 1 вариант.
    put(f, kRecordPress, 1, 1);
    put(f, kRecordPress + 1, kCarrier, 3);
    put(f, kRecordPress + 4, 1, 1);
    put(f, kRecordPress + 5, kPress, 3);
    put(f, kRecordPress + 8, 0, 3);

    put(f, kRecordHold, 1, 1);
    put(f, kRecordHold + 1, kCarrier, 3);
    put(f, kRecordHold + 4, 1, 1);
    put(f, kRecordHold + 5, kHold, 3);
    put(f, kRecordHold + 8, kHold + 12, 3); // повтор — начало второго сегмента

    // Устройство: 0, число последовательностей, указатели.
    put(f, kDevice, 0, 1);
    put(f, kDevice + 1, 2, 2);
    put(f, kDevice + 3, kRecordPress, 3);
    put(f, kDevice + 6, kRecordHold, 3);

    put(f, kRoot, 1, 1);
    put(f, kRoot + 1, kDevice, 3);
    return f;
}

} // namespace

TEST(SsirSequences, ParsesDeviceSequencesAndCarrier) {
    const auto parsed = parseSsirSequences(makeFile());
    ASSERT_TRUE(parsed) << parsed.error().message;
    ASSERT_EQ(parsed->devices.size(), 1u);
    ASSERT_EQ(parsed->devices[0].size(), 2u);
    EXPECT_EQ(parsed->sequenceCount(), 2u);

    const auto& press = parsed->devices[0][0];
    EXPECT_EQ(press.carrierPeriodNs, 26316u);
    EXPECT_EQ(press.carrierOnNs, 13158u);
    EXPECT_EQ(press.frequency(), 38000u);
    ASSERT_EQ(press.variants.size(), 1u);
    EXPECT_FALSE(press.variants[0].repeatAt);
}

TEST(SsirSequences, HoldKeepsRepeatPointInsideWaveform) {
    const auto parsed = parseSsirSequences(makeFile());
    ASSERT_TRUE(parsed);
    const auto& hold = parsed->devices[0][1];
    ASSERT_EQ(hold.variants.size(), 1u);
    ASSERT_TRUE(hold.variants[0].repeatAt);
    EXPECT_EQ(*hold.variants[0].repeatAt, 6u); // шестое слово — начало второго сегмента
    EXPECT_TRUE(hold.variants[0].sharedRepeat.empty());
}

TEST(SsirSequences, FirstFrameJoinsLongSpacesAndDropsLeadIn) {
    const auto parsed = parseSsirSequences(makeFile());
    ASSERT_TRUE(parsed);
    const auto signal = firstFrameSignal(parsed->devices[0][0]);
    ASSERT_TRUE(signal);
    EXPECT_EQ(signal->frequency, 38000u);
    const std::vector<int> expected{9000, -4500, 560};
    EXPECT_EQ(signal->durations, expected);

    const auto hold = firstFrameSignal(parsed->devices[0][1]);
    ASSERT_TRUE(hold);
    const std::vector<int> holdExpected{9000, -2250, 560};
    EXPECT_EQ(hold->durations, holdExpected);
}

TEST(SsirSequences, RejectsWrongVersionAndBrokenPointers) {
    auto wrongVersion = makeFile();
    put(wrongVersion, 0, 2, 2);
    EXPECT_FALSE(parseSsirSequences(wrongVersion));

    auto badRoot = makeFile();
    put(badRoot, 2, 0xFFFFF, 3);
    EXPECT_FALSE(parseSsirSequences(badRoot));

    auto badWaveform = makeFile();
    put(badWaveform, kRecordPress + 5, 0xFFFFF, 3);
    EXPECT_FALSE(parseSsirSequences(badWaveform));

    auto badCount = makeFile();
    put(badCount, kDevice + 1, 60000, 2);
    EXPECT_FALSE(parseSsirSequences(badCount));

    EXPECT_FALSE(parseSsirSequences(Image{}));
    EXPECT_FALSE(parseSsirSequences(Image{1, 0, 0}));
}
