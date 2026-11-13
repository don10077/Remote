/**
 * @file irSignalTest.cpp
 * @brief Тесты ИК-сигнала: запись SsIr.bin, обучение, распознавание протоколов.
 */

#include "harmony/core/ir/irSignal.h"

#include <gtest/gtest.h>

using namespace harmony::ir;

namespace {

/// Настоящий сигнал, выученный Harmony 900 с родного пульта (кнопка «1»).
IrSignal learnedNec() {
    const std::vector<int> marks = {9005, 547, 547, 573, 573, 573, 547, 573, 572, 547, 574, 574, 573, 572, 547, 547,
                                    574, 574, 573, 572, 547, 547, 574, 573, 546, 573, 572, 547, 547, 573, 573, 573,
                                    573, 547};
    const std::vector<int> spaces = {4558, 1718, 1717, 1692, 1692, 585, 586, 1692, 1718, 1718, 1691, 1691,
                                     1692, 586, 585, 1718, 1691, 1691, 1692, 586, 1718, 585, 559, 586,
                                     586, 560, 586, 1718, 585, 1692, 1692, 1691, 1718, 32221};
    IrSignal signal;
    signal.frequency = 37978;
    for (std::size_t i = 0; i < marks.size(); ++i) {
        signal.durations.push_back(marks[i]);
        signal.durations.push_back(-spaces[i]);
    }
    return signal;
}

} // namespace

TEST(IrSignal, DecodesLearnedExtendedNec) {
    const auto decoded = decode(learnedNec());

    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->protocol, "NEC (extended)");
    EXPECT_EQ(decoded->address, 0xCFCFU);
    EXPECT_EQ(decoded->command, 0x0BU);
    EXPECT_EQ(decoded->text(), "NEC (extended) · address 0xCFCF, command 0x0B");
}

TEST(IrSignal, RawEntryRoundTrip) {
    const IrSignal signal = learnedNec();

    const auto entry = encodeRawEntry(signal);
    auto back = decodeRawEntry(entry);

    ASSERT_TRUE(back) << back.error().message;
    EXPECT_EQ(back->durations, signal.durations);
    EXPECT_NEAR(static_cast<double>(back->frequency), 37978.0, 5.0); // период хранится в наносекундах
    EXPECT_EQ(entry.size(), 6U + signal.durations.size() * 2);
}

TEST(IrSignal, LongPausesAreSplitIntoEqualWords) {
    IrSignal signal;
    signal.durations = {500, -40000};

    const auto entry = encodeRawEntry(signal);

    ASSERT_EQ(entry.size(), 6U + 3 * 2); // импульс + два слова паузы по 20000
    EXPECT_EQ(entry[8] | (entry[9] << 8), 20000);
    EXPECT_EQ(decodeRawEntry(entry)->durations, signal.durations);
}

TEST(IrSignal, ReadsEntryFormatOfRemote) {
    // Заголовок из описания формата: период 26500 нс (37,7 кГц), 2 слова: импульс 53, пауза 1719.
    const harmony::formats::Bytes entry = {0x84, 0x67, 0x00, 0x00, 0x02, 0x00, 0x35, 0x80, 0xB7, 0x06};
    auto signal = decodeRawEntry(entry);
    ASSERT_TRUE(signal);
    EXPECT_EQ(signal->durations, (std::vector<int>{53, -1719}));
    EXPECT_NEAR(static_cast<double>(signal->frequency), 37736.0, 2.0);
    EXPECT_FALSE(decodeRawEntry(harmony::formats::Bytes{1, 2, 3}));
}

TEST(IrSignal, ConvertsMarkSegmentsFromLearning) {
    const auto signal = fromMarkSegments(38000, {9000, 13500, 560, 2250, 560, 1120});
    EXPECT_EQ(signal.durations, (std::vector<int>{9000, -4500, 560, -1690, 560, -560}));
}

TEST(IrSignal, DecodesNecAndSony) {
    IrSignal nec;
    nec.durations = {9000, -4500};
    const std::uint32_t frame = 0x04U | (0xFBU << 8) | (0x08U << 16) | (0xF7U << 24);
    for (int bit = 0; bit < 32; ++bit) {
        nec.durations.push_back(560);
        nec.durations.push_back(((frame >> bit) & 1U) != 0 ? -1690 : -560);
    }
    nec.durations.push_back(560);
    EXPECT_EQ(decode(nec)->text(), "NEC · address 0x04, command 0x08");

    IrSignal sony;
    sony.durations = {2400, -600};
    const std::uint32_t code = 0x15U | (0x01U << 7); // команда 21 (Power), адрес 1 (ТВ)
    for (int bit = 0; bit < 12; ++bit) {
        sony.durations.push_back(((code >> bit) & 1U) != 0 ? 1200 : 600);
        sony.durations.push_back(bit == 11 ? -25000 : -600);
    }
    const auto decoded = decode(sony);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->protocol, "Sony SIRC 12");
    EXPECT_EQ(decoded->address, 1U);
    EXPECT_EQ(decoded->command, 0x15U);
}

TEST(IrSignal, DescribesUnknownSignal) {
    IrSignal signal;
    signal.frequency = 36000;
    signal.durations = {300, -300, 300, -900};
    EXPECT_EQ(describe(signal), "protocol not recognised · pulses: 2, carrier 36.0 kHz");
}
