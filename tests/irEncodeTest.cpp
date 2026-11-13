/**
 * @file irEncodeTest.cpp
 * @brief Тесты сборки ИК-сигнала по протоколу: распознавание узнаёт собранное.
 */

#include "harmony/core/ir/irEncode.h"

#include "harmony/core/configDocument.h"

#include <gtest/gtest.h>

using namespace harmony;

namespace {

/// Протокол, адрес и команда, которые должно узнать распознавание.
struct Case {
    const char* protocol;
    std::uint32_t address;
    std::uint32_t command;
    const char* decodedAs; ///< nullptr — так же, как protocol
};

} // namespace

TEST(IrEncode, DecoderRecognisesEveryProtocol) {
    const Case cases[] = {
        {"NEC", 0x04, 0x08, nullptr},          {"NEC (extended)", 0x1E7, 0x45, nullptr},
        {"Samsung32", 0x0707, 0x02, nullptr},  {"Sony SIRC 12", 0x01, 0x15, nullptr},
        {"Sony SIRC 15", 0x97, 0x2A, nullptr}, {"Sony SIRC 20", 0x1A3, 0x3C, nullptr},
        {"RC5", 0x05, 0x35, nullptr},          {"RC5", 0x10, 0x4C, "RC5X"},
        {"RC6", 0x00, 0x0C, nullptr},          {"Panasonic", 0x080, 0x3D, nullptr},
        {"LG", 0x04, 0x10EF, nullptr},         {"JVC", 0x03, 0x17, nullptr},
        {"Onkyo", 0xD2D2, 0x1234, nullptr},    {"Denon", 0x02, 0xE1, nullptr},
        {"Sharp", 0x01, 0x16, nullptr},        {"RCA", 0x0F, 0x54, nullptr},
    };
    for (const Case& c : cases) {
        auto signal = ir::encode(c.protocol, c.address, c.command);
        ASSERT_TRUE(signal) << c.protocol << ": " << signal.error().message;
        const auto decoded = ir::decode(*signal);
        ASSERT_TRUE(decoded.has_value()) << c.protocol;
        EXPECT_EQ(decoded->protocol, c.decodedAs != nullptr ? c.decodedAs : c.protocol);
        EXPECT_EQ(decoded->address, c.address) << c.protocol;
        EXPECT_EQ(decoded->command, c.command) << c.protocol;
        // Сигнал начинается импульсом, чередуется и кончается паузой.
        ASSERT_FALSE(signal->durations.empty());
        EXPECT_GT(signal->durations.front(), 0) << c.protocol;
        EXPECT_LT(signal->durations.back(), 0) << c.protocol;
        for (std::size_t i = 1; i < signal->durations.size(); ++i) {
            EXPECT_NE(signal->durations[i] > 0, signal->durations[i - 1] > 0)
                << c.protocol << " at " << i;
        }
    }
}

TEST(IrEncode, NecMatchesIrremoteRawData) {
    // IRremote печатает NEC адрес 0x04, команда 0x08 как 0xF708FB04.
    auto signal = ir::encode("NEC", 0x04, 0x08);
    ASSERT_TRUE(signal);
    EXPECT_EQ(ir::decode(*signal)->rawData(), "0xF708FB04");
    EXPECT_EQ(signal->frequency, 38000U);
}

TEST(IrEncode, RejectsUnknownProtocolAndValuesTooLarge) {
    EXPECT_EQ(ir::encode("Morse", 1, 1).error().code, ErrorCode::Unsupported);
    EXPECT_EQ(ir::encode("NEC", 0x100, 1).error().code, ErrorCode::Format);
    EXPECT_EQ(ir::encode("Sony SIRC 12", 1, 0x80).error().code, ErrorCode::Format);
    EXPECT_FALSE(ir::encodableProtocols().empty());
}

TEST(IrEncode, CommandByProtocolIsStoredAndRecognised) {
    auto document = ConfigDocument::createNew("test");
    auto id = document.createDevice({"TV", "Television", "Acme", "X1"});
    ASSERT_TRUE(id);
    auto signal = ir::encode("Samsung32", 0x0707, 0x02);
    ASSERT_TRUE(signal);
    ASSERT_TRUE(document.learnCommand(*id, "PowerToggle", *signal));
    auto details = document.commandDetails(*id, "PowerToggle");
    ASSERT_TRUE(details) << details.error().message;
    ASSERT_TRUE(details->decoded.has_value());
    EXPECT_EQ(details->decoded->protocol, "Samsung32");
    EXPECT_EQ(details->decoded->address, 0x0707U);
    EXPECT_EQ(details->decoded->command, 0x02U);
    // Питание описалось само: команда с привычным именем.
    EXPECT_TRUE(document.model().findDevice(*id)->powerControlled);
}

TEST(IrEncode, SameSignalIgnoresRepeatsAndToggleBit) {
    auto nec = ir::encode("NEC", 0x04, 0x08);
    auto necOther = ir::encode("NEC", 0x04, 0x09);
    ASSERT_TRUE(nec && necOther);
    EXPECT_TRUE(ir::sameSignal(*nec, *nec));
    EXPECT_FALSE(ir::sameSignal(*nec, *necOther));
    // Кнопку держали дольше — больше повторов, та же команда.
    ir::IrSignal held = *nec;
    held.durations.insert(held.durations.end(), {9000, -2250, 560, -96000});
    EXPECT_TRUE(ir::sameSignal(*nec, held));

    // Нераспознанный сигнал — по первому кадру с допуском.
    ir::IrSignal raw{38000, {3000, -1000, 700, -700, 700, -1500, 700, -50000}};
    ir::IrSignal noisy{38000, {3100, -950, 760, -650, 690, -1550, 700, -50000, 3000, -1000}};
    ir::IrSignal other{38000, {3000, -1000, 700, -1500, 700, -700, 700, -50000}};
    EXPECT_TRUE(ir::sameSignal(raw, noisy));
    EXPECT_FALSE(ir::sameSignal(raw, other));
    EXPECT_FALSE(ir::sameSignal(raw, *nec));
}

TEST(IrEncode, FullBitsRoundTripEveryProtocol) {
    for (const auto& protocol : ir::encodableProtocols()) {
        for (const auto command : {0U, 7U, (1U << protocol.commandBits) - 1}) {
            SCOPED_TRACE(protocol.name + " command " + std::to_string(command));
            const auto signal = ir::encode(protocol.name, (1U << protocol.addressBits) - 1, command);
            ASSERT_TRUE(signal);
            const auto decoded = ir::decode(*signal);
            ASSERT_TRUE(decoded);
            const auto rebuilt = ir::encodeBits(protocol.name, decoded->bits);
            ASSERT_TRUE(rebuilt) << rebuilt.error().message;
            EXPECT_TRUE(ir::sameSignal(*signal, *rebuilt));
        }
    }
}

TEST(IrEncode, FullBitsRejectWrongLengthAndChecksum) {
    auto decoded = ir::decode(*ir::encode("NEC", 4, 8));
    ASSERT_TRUE(decoded);
    auto broken = decoded->bits;
    broken.pop_back();
    EXPECT_FALSE(ir::encodeBits("NEC", broken));
    broken = decoded->bits;
    broken[31] = !broken[31];
    EXPECT_FALSE(ir::encodeBits("NEC", broken));
    EXPECT_FALSE(ir::encodeBits("unknown", decoded->bits));
}
