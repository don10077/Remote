/**
 * @file irProtocolTest.cpp
 * @brief Тесты команд по протоколу (IrProto.bin + <Code>) и распознавания протоколов.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/formats/irTables.h"
#include "harmony/core/ir/irProtocol.h"
#include "harmony/core/ir/irSignal.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace harmony;
using namespace harmony::ir;

namespace {

const std::filesystem::path kConfig = std::filesystem::path(HARMONY_TEST_DATA_DIR) / "userconfig";

/// Сигнал с кодированием длиной паузы (биты — в порядке передачи).
IrSignal pulseDistance(int headerMark, int headerSpace, int mark, int zero, int one, const std::vector<bool>& bits) {
    IrSignal signal;
    if (headerMark > 0) {
        signal.durations = {headerMark, -headerSpace};
    }
    for (const bool bit : bits) {
        signal.durations.push_back(mark);
        signal.durations.push_back(-(bit ? one : zero));
    }
    signal.durations.push_back(mark);
    signal.durations.push_back(-40000);
    return signal;
}

/// Биты числа: lsbFirst — младший бит первым.
std::vector<bool> bitsOf(std::uint64_t value, int count, bool lsbFirst) {
    std::vector<bool> bits;
    for (int i = 0; i < count; ++i) {
        bits.push_back(((value >> (lsbFirst ? i : count - 1 - i)) & 1) != 0);
    }
    return bits;
}

/// Добавляет уровень длительностью units тактов, складывая с предыдущим того же вида.
void level(std::vector<int>& out, bool mark, int units, int unit) {
    const int duration = (mark ? 1 : -1) * units * unit;
    if (!out.empty() && (out.back() > 0) == mark) {
        out.back() += duration;
    } else {
        out.push_back(duration);
    }
}

/// RC5: 14 бит, «1» — пауза, затем импульс.
IrSignal rc5(bool s2, bool toggle, unsigned address, unsigned command) {
    std::vector<bool> bits = {true, s2, toggle};
    for (const bool bit : bitsOf(address, 5, false)) bits.push_back(bit);
    for (const bool bit : bitsOf(command, 6, false)) bits.push_back(bit);
    IrSignal signal;
    for (const bool bit : bits) {
        level(signal.durations, !bit, 1, 889);
        level(signal.durations, bit, 1, 889);
    }
    if (signal.durations.front() < 0) {
        signal.durations.erase(signal.durations.begin());
    }
    level(signal.durations, false, 100, 889);
    return signal;
}

/// RC6 режима mode: «1» — импульс, затем пауза; бит переключения — двойной ширины.
IrSignal rc6(unsigned mode, const std::vector<bool>& data) {
    IrSignal signal;
    signal.durations = {2666, -889};
    auto bit = [&](bool value, int width) {
        level(signal.durations, value, width, 444);
        level(signal.durations, !value, width, 444);
    };
    bit(true, 1);
    for (const bool b : bitsOf(mode, 3, false)) bit(b, 1);
    bit(false, 2);
    for (const bool b : data) bit(b, 1);
    level(signal.durations, false, 60, 444);
    return signal;
}

} // namespace

TEST(IrProtocol, ParsesSingleSectionCode) {
    const auto code = parseProtocolCode("0x000000000200010040040100BCBD00");

    ASSERT_TRUE(code) << code.error().message;
    EXPECT_EQ(code->protocol, 0U);
    EXPECT_EQ(code->dataFrames, 2U);
    EXPECT_FALSE(code->repeatFrame);
    ASSERT_EQ(code->sections.size(), 1U);
    EXPECT_EQ(code->sections[0].size(), 48U);
    EXPECT_EQ(bitsToHex(code->bits()), "0x40040100BCBD");
}

TEST(IrProtocol, RejectsBrokenCode) {
    EXPECT_FALSE(parseProtocolCode("0x12"));
    EXPECT_FALSE(parseProtocolCode("0xZZ0000000200010040040100BCBD00"));
    EXPECT_FALSE(parseProtocolCode("0x000000000200010040040100BCBD01")); // нет завершающего 00
}

TEST(IrProtocol, RendersCommandsOfRealConfig) {
    const auto document = ConfigDocument::open(kConfig);
    ASSERT_TRUE(document) << document.error().message;

    // Panasonic: PowerToggle — адрес 0x08, команда 0x3D (как печатает IRremote).
    const auto* tv = &document->model().devices.front();
    const auto power = document->commandDetails(tv->id, "PowerToggle");
    ASSERT_TRUE(power) << power.error().message;
    ASSERT_TRUE(power->decoded);
    EXPECT_EQ(power->decoded->protocol, "Panasonic");
    EXPECT_EQ(power->decoded->address, 0x08U);
    EXPECT_EQ(power->decoded->command, 0x3DU);
    EXPECT_EQ(power->rawData(), "0xBD3D00802002");
    EXPECT_NEAR(power->signal.frequency, 36700, 1500);

    // Все команды фикстуры собираются и узнаются.
    for (const auto& device : document->model().devices) {
        for (const auto& command : device.commands) {
            const auto details = document->commandDetails(device.id, command.name);
            ASSERT_TRUE(details) << device.displayName() << " / " << command.name;
            EXPECT_TRUE(details->decoded) << device.displayName() << " / " << command.name;
        }
    }
}

TEST(IrDecode, Nec) {
    const auto decoded = decode(pulseDistance(9000, 4500, 560, 560, 1690, bitsOf(0xF708FB04, 32, true)));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->protocol, "NEC");
    EXPECT_EQ(decoded->address, 0x04U);
    EXPECT_EQ(decoded->command, 0x08U);
    EXPECT_EQ(decoded->rawData(), "0xF708FB04");
}

TEST(IrDecode, OnkyoWithoutCommandCheck) {
    const auto decoded = decode(pulseDistance(9000, 4500, 560, 560, 1690, bitsOf(0x12345678, 32, true)));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->protocol, "Onkyo");
    EXPECT_EQ(decoded->address, 0x5678U);
    EXPECT_EQ(decoded->command, 0x1234U);
}

TEST(IrDecode, LgAndJvc) {
    const auto lg = decode(pulseDistance(8500, 4250, 560, 560, 1690, bitsOf(0x88000CC, 28, false)));
    ASSERT_TRUE(lg);
    EXPECT_EQ(lg->protocol, "LG");
    EXPECT_EQ(lg->address, 0x88U);
    EXPECT_EQ(lg->command, 0x000CU);

    const auto jvc = decode(pulseDistance(8400, 4200, 525, 525, 1575, bitsOf(0x17C3, 16, true)));
    ASSERT_TRUE(jvc);
    EXPECT_EQ(jvc->protocol, "JVC");
    EXPECT_EQ(jvc->address, 0xC3U);
    EXPECT_EQ(jvc->command, 0x17U);
}

TEST(IrDecode, SamsungAndRca) {
    const auto samsung = decode(pulseDistance(4500, 4500, 560, 560, 1690, bitsOf(0xFD020707, 32, true)));
    ASSERT_TRUE(samsung);
    EXPECT_EQ(samsung->protocol, "Samsung32");
    EXPECT_EQ(samsung->address, 0x0707U);
    EXPECT_EQ(samsung->command, 0x02U);

    const auto rca = decode(pulseDistance(4000, 4000, 500, 1000, 2000, bitsOf(0xF54'0AB, 24, false)));
    ASSERT_TRUE(rca);
    EXPECT_EQ(rca->protocol, "RCA");
    EXPECT_EQ(rca->address, 0xFU);
    EXPECT_EQ(rca->command, 0x54U);
}

TEST(IrDecode, SonyAndDenon) {
    IrSignal sony;
    sony.durations = {2400, -600};
    for (const bool bit : bitsOf(0x15 | (0x01 << 7), 12, true)) {
        sony.durations.push_back(bit ? 1200 : 600);
        sony.durations.push_back(-600);
    }
    sony.durations.back() = -25000;
    const auto decodedSony = decode(sony);
    ASSERT_TRUE(decodedSony);
    EXPECT_EQ(decodedSony->protocol, "Sony SIRC 12");
    EXPECT_EQ(decodedSony->address, 0x01U);
    EXPECT_EQ(decodedSony->command, 0x15U);

    auto denonBits = bitsOf(0x02 | (0xE1 << 5), 15, true);
    const auto denon = decode(pulseDistance(0, 0, 300, 750, 1800, denonBits));
    ASSERT_TRUE(denon);
    EXPECT_EQ(denon->protocol, "Denon");
    EXPECT_EQ(denon->address, 0x02U);
    EXPECT_EQ(denon->command, 0xE1U);
}

TEST(IrDecode, Rc5AndRc5x) {
    const auto plain = decode(rc5(true, false, 0x00, 0x0C));
    ASSERT_TRUE(plain);
    EXPECT_EQ(plain->protocol, "RC5");
    EXPECT_EQ(plain->address, 0x00U);
    EXPECT_EQ(plain->command, 0x0CU);
    EXPECT_FALSE(plain->lsbFirst);

    const auto extended = decode(rc5(false, true, 0x05, 0x01));
    ASSERT_TRUE(extended);
    EXPECT_EQ(extended->protocol, "RC5X");
    EXPECT_EQ(extended->address, 0x05U);
    EXPECT_EQ(extended->command, 0x41U);
}

TEST(IrDecode, Rc6Modes) {
    std::vector<bool> data = bitsOf(0x040C, 16, false);
    const auto mode0 = decode(rc6(0, data));
    ASSERT_TRUE(mode0);
    EXPECT_EQ(mode0->protocol, "RC6");
    EXPECT_EQ(mode0->address, 0x04U);
    EXPECT_EQ(mode0->command, 0x0CU);

    // Команда с единицей в последнем бите: последняя пауза сливается с паузой после кадра.
    const auto odd = decode(rc6(0, bitsOf(0x0401, 16, false)));
    ASSERT_TRUE(odd);
    EXPECT_EQ(odd->command, 0x01U);

    const auto mode6 = decode(rc6(6, bitsOf(0x800F0410ULL, 32, false)));
    ASSERT_TRUE(mode6);
    EXPECT_EQ(mode6->protocol, "RC6A");
    EXPECT_EQ(mode6->address, 0x0F04U);
    EXPECT_EQ(mode6->command, 0x10U);
}

TEST(IrDecode, Rc6OtherModesAndLengths) {
    // Sky (RC6-6-20): устройство 0x0C, подустройство 0, команда 0x5C — как выучил Harmony 900
    // на стенде 2026-11-04 (кадр tools/irFrames.py, излучённый RM4).
    IrSignal sky;
    sky.durations = {2496, -868, 461, -407, 460, -407, 459, -825, 461, -824, 850, -469, 461, -407, 459,
                     -424, 434, -449, 851, -434, 434, -821, 460, -438, 435, -434, 459, -408, 460, -407,
                     460, -440, 434, -434, 850, -844, 884, -410, 459, -407, 460, -825, 460, -441, 433,
                     -32335, -32768, -32768};
    const auto decodedSky = decode(sky);
    ASSERT_TRUE(decodedSky);
    EXPECT_EQ(decodedSky->protocol, "RC6-6-20");
    EXPECT_EQ(decodedSky->address, 0x0C0U);
    EXPECT_EQ(decodedSky->command, 0x5CU);

    const auto mode6of24 = decode(rc6(6, bitsOf(0x123456, 24, false)));
    ASSERT_TRUE(mode6of24);
    EXPECT_EQ(mode6of24->protocol, "RC6-6-24");
    EXPECT_EQ(mode6of24->address, 0x1234U);
    EXPECT_EQ(mode6of24->command, 0x56U);
    EXPECT_FALSE(mode6of24->lsbFirst);
}

namespace {

/// Samsung36: 16 бит адреса, разрыв 500/4500, 4 бита расширения, команда и её инверсия.
IrSignal samsung36(unsigned address, unsigned extension, unsigned command, unsigned check) {
    std::vector<bool> bits = bitsOf(address, 16, true);
    for (const bool bit : bitsOf(extension, 4, true)) bits.push_back(bit);
    for (const bool bit : bitsOf(command, 8, true)) bits.push_back(bit);
    for (const bool bit : bitsOf(check, 8, true)) bits.push_back(bit);
    IrSignal signal = pulseDistance(4500, 4500, 500, 500, 1500, bits);
    const auto gap = signal.durations.begin() + 2 + 2 * 16;
    signal.durations.insert(gap, {500, -4500});
    return signal;
}

} // namespace

TEST(IrDecode, Samsung36) {
    // Адрес 0x0400, команда 0x5A — как выучил Harmony 900 на стенде 2026-11-04
    // (кадр tools/irFrames.py, излучённый RM4).
    IrSignal s36;
    s36.durations = {4237, -4312, 486, -478, 460, -471, 460, -473, 486, -478, 486, -445, 486, -446,
                     486, -478, 460, -471, 460, -473, 486, -445, 486, -1426, 460, -489, 459, -472,
                     459, -473, 486, -445, 486, -447, 487, -4268, 487, -444, 487, -478, 460, -471,
                     459, -474, 485, -479, 486, -1409, 486, -478, 459, -1435, 461, -1436, 486,
                     -447, 486, -1440, 460, -473, 486, -1443, 485, -446, 485, -1442, 461, -470,
                     486, -479, 485, -1410, 487, -478, 460, -1435, 459, -32309, -32768, -32768};
    const auto learned = decode(s36);
    ASSERT_TRUE(learned);
    EXPECT_EQ(learned->protocol, "Samsung36");
    EXPECT_EQ(learned->address, 0x0400U);
    EXPECT_EQ(learned->command, 0x5AU);
    EXPECT_TRUE(learned->lsbFirst);

    // Расширение попадает в старшие биты команды.
    const auto extended = decode(samsung36(0xE0E0, 0x3, 0x21, 0xDE));
    ASSERT_TRUE(extended);
    EXPECT_EQ(extended->protocol, "Samsung36");
    EXPECT_EQ(extended->address, 0xE0E0U);
    EXPECT_EQ(extended->command, 0x321U);

    // Последний байт — не инверсия команды.
    EXPECT_FALSE(decode(samsung36(0x0400, 0, 0x5A, 0x5A)));
}

namespace {

/// Konka: 3000/3000, адрес и команда старшим битом вперёд, хвост 500/4000 и стоп-импульс.
IrSignal konka(unsigned address, unsigned command, int bitCount = 16) {
    std::vector<bool> bits = bitsOf(address, 8, false);
    for (const bool bit : bitsOf(command, 8, false)) bits.push_back(bit);
    bits.resize(static_cast<std::size_t>(bitCount), false);
    IrSignal signal = pulseDistance(3000, 3000, 500, 1500, 2500, bits);
    signal.durations.insert(signal.durations.end() - 1, {-4000, 500});
    return signal;
}

} // namespace

TEST(IrDecode, Konka) {
    // Адрес 0x02, команда 0x4F — как выучил Harmony 900 на стенде 2026-11-04
    // (кадр tools/irFrames.py, излучённый RM4).
    IrSignal learned;
    learned.durations = {2781, -2904, 486, -1442, 486, -1441, 460, -1437, 460, -1437, 485, -1442,
                         461, -1435, 459, -2368, 487, -1442, 459, -1435, 487, -2373, 460, -1437,
                         460, -1436, 486, -2373, 460, -2367, 486, -2374, 460, -2368, 485, -3788,
                         487, -32281, -32768, -32768};
    const auto decoded = decode(learned);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->protocol, "Konka");
    EXPECT_EQ(decoded->address, 0x02U);
    EXPECT_EQ(decoded->command, 0x4FU);
    EXPECT_FALSE(decoded->lsbFirst);
    EXPECT_EQ(decoded->rawData(), "0x024F");

    const auto other = decode(konka(0xA5, 0x3C));
    ASSERT_TRUE(other);
    EXPECT_EQ(other->protocol, "Konka");
    EXPECT_EQ(other->address, 0xA5U);
    EXPECT_EQ(other->command, 0x3CU);

    // Бит лишний или нет хвоста 500/4000 — не Konka.
    EXPECT_FALSE(decode(konka(0xA5, 0x3C, 17)));
    EXPECT_FALSE(decode(pulseDistance(3000, 3000, 500, 1500, 2500, bitsOf(0xA53C, 16, false))));
}

TEST(IrDecode, UnknownSignalIsNotDecoded) {
    IrSignal noise;
    noise.durations = {1000, -1000, 3000, -500, 700, -9000};
    EXPECT_FALSE(decode(noise));
}

// Кадры протоколов, которых программа не знает, — как их собирает tools/irFrames.py.
// Стенд 2026-11-04 показал, что декодер выдавал их за известные протоколы.

TEST(IrDecode, LongerFrameOfUnknownProtocolIsNotDecoded) {
    // Sanyo LC7461: заголовок NEC, 13 бит адреса, его инверсия, команда, её инверсия — 42 бита (был Onkyo).
    std::vector<bool> sanyo = bitsOf(0x1A5, 13, true);
    for (const bool bit : bitsOf(~0x1A5U & 0x1FFF, 13, true)) sanyo.push_back(bit);
    for (const bool bit : bitsOf(0x33, 8, true)) sanyo.push_back(bit);
    for (const bool bit : bitsOf(~0x33U & 0xFF, 8, true)) sanyo.push_back(bit);
    EXPECT_FALSE(decode(pulseDistance(9000, 4500, 560, 560, 1690, sanyo)));

    // Mitsubishi (старые ТВ): без заголовка, импульс 300, паузы 900/2100, 16 бит (был Sharp).
    EXPECT_FALSE(decode(pulseDistance(0, 0, 300, 900, 2100, bitsOf(0x47 | (0x2B << 8), 16, true))));

    // Кондиционер Mitsubishi Electric: 3400/1750, 18 байт; начало 0x23 0xCB (был Mitsubishi (Kaseikyo)).
    std::vector<bool> airConditioner;
    for (const unsigned byte : {0x23, 0xCB, 0x26, 0x01, 0x00, 0x20, 0x08, 0x06, 0x30, 0x45, 0x67, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x1F}) {
        for (const bool bit : bitsOf(byte, 8, true)) airConditioner.push_back(bit);
    }
    EXPECT_FALSE(decode(pulseDistance(3400, 1750, 450, 420, 1300, airConditioner)));

    // RC6A, после 32 бит которого кадр продолжается.
    EXPECT_FALSE(decode(rc6(6, bitsOf(0x800F0410CULL, 36, false))));
}

namespace {

/// Кадр NEC с паузой 40 мс после него.
std::vector<int> necFrame(std::uint32_t address, std::uint32_t command) {
    const std::uint32_t code = address | ((~address & 0xFFU) << 8) | (command << 16) | ((~command & 0xFFU) << 24);
    return pulseDistance(9000, 4500, 560, 560, 1690, bitsOf(code, 32, true)).durations;
}

} // namespace

TEST(IrDecode, NecButtonOfDifferentFramesIsNotDecoded) {
    // Pioneer: два кадра NEC с разными адресами (был NEC по первому кадру).
    IrSignal pioneer;
    pioneer.durations = necFrame(0xA5, 0x10);
    const auto second = necFrame(0xA8, 0x1C);
    pioneer.durations.insert(pioneer.durations.end(), second.begin(), second.end());
    EXPECT_FALSE(decode(pioneer));
}

TEST(IrDecode, NecFrameEndsWithRepeatOrSignalEnd) {
    IrSignal twice;
    const auto frame = necFrame(0x04, 0x08);
    twice.durations = frame;
    twice.durations.insert(twice.durations.end(), frame.begin(), frame.end());
    const auto repeated = decode(twice);
    ASSERT_TRUE(repeated);
    EXPECT_EQ(repeated->protocol, "NEC");
    EXPECT_EQ(repeated->address, 0x04U);

    IrSignal withRepeatCode;
    withRepeatCode.durations = necFrame(0x04, 0x08);
    withRepeatCode.durations.insert(withRepeatCode.durations.end(), {9000, -2250, 560, -96000});
    EXPECT_TRUE(decode(withRepeatCode));

    // Сигнал обрезан сразу после стоп-импульса.
    IrSignal cut;
    cut.durations = necFrame(0x04, 0x08);
    cut.durations.pop_back();
    EXPECT_TRUE(decode(cut));
}

TEST(IrProtocol, RefusesSignalsLongerThanTheLimit) {
    // 255 кадров данных из XML не должны собирать сигнал в сотни мегабайт.
    const auto document = ConfigDocument::open(kConfig);
    ASSERT_TRUE(document) << document.error().message;
    const model::Command* command = document->model().devices.front().findCommand("PowerToggle");
    ASSERT_NE(command, nullptr);
    ASSERT_GE(command->protocol, 0);
    std::ifstream stream(kConfig / "IrProto.bin", std::ios::binary);
    const formats::Bytes file{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    const auto table = formats::parseIrProto(file);
    ASSERT_TRUE(table) << table.error().message;
    const auto& entry = table->entries.at(static_cast<std::size_t>(command->protocol));

    auto code = parseProtocolCode(command->code);
    ASSERT_TRUE(code) << code.error().message;
    ASSERT_TRUE(renderProtocol(entry, *code));
    code->dataFrames = 255;
    const auto huge = renderProtocol(entry, *code);
    ASSERT_FALSE(huge);
    EXPECT_EQ(huge.error().code, ErrorCode::Format);
}
