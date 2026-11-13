/**
 * @file nativeProtocolTest.cpp
 * @brief Протоколы в IrProto.bin: исполнитель программ пульта и сгенерированные команды.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/formats/ezhex.h"
#include "harmony/core/formats/irTables.h"
#include "harmony/core/formats/userConfiguration.h"
#include "harmony/core/formats/zipArchive.h"
#include "harmony/core/ir/carrierVm.h"
#include "harmony/core/ir/irEncode.h"
#include "harmony/core/ir/irProtocol.h"
#include "harmony/core/ir/nativeProtocol.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <tuple>

using namespace harmony;

namespace {

const std::filesystem::path kData(HARMONY_TEST_DATA_DIR);

formats::Bytes readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

struct Case {
    const char* protocol;
    std::uint32_t address;
    std::uint32_t command;
};

constexpr Case kCases[] = {
    {"NEC", 0x04, 0x08},         {"NEC (extended)", 0x1E7, 0x45}, {"Onkyo", 0xD2D2, 0x1234},
    {"Samsung32", 0x0707, 0x02}, {"Sony SIRC 12", 0x01, 0x15},    {"Sony SIRC 15", 0x97, 0x2A},
    {"Sony SIRC 20", 0x1A3, 0x3C}, {"JVC", 0x03, 0x17},           {"RC5", 0x05, 0x35},
    {"RC5", 0x10, 0x4C},       {"RC6", 0x00, 0x0C},             {"RC6", 0xA5, 0x5A},
};

ir::CarrierRun run(const ir::NativeCommand& command, unsigned toggle = 0, unsigned held = 0) {
    auto result = ir::runCarrier(command.protocol, command.code, toggle, held);
    EXPECT_TRUE(result) << result.error().message;
    return result ? std::move(*result) : ir::CarrierRun{};
}

} // namespace

TEST(NativeProtocol, EveryCommandExecutesAsTheSameProtocolAddressAndCommand) {
    for (const Case& c : kCases) {
        SCOPED_TRACE(c.protocol);
        auto native = ir::nativeCommand(c.protocol, c.address, c.command);
        ASSERT_TRUE(native) << native.error().message;
        const auto pressed = run(*native);
        const auto decoded = ir::decode(pressed.signal);
        const auto expected = ir::decode(*ir::encode(c.protocol, c.address, c.command));
        ASSERT_TRUE(decoded && expected);
        EXPECT_EQ(decoded->protocol, expected->protocol);
        EXPECT_EQ(decoded->address, c.address);
        EXPECT_EQ(decoded->command, c.command);
    }
}

TEST(NativeProtocol, CodesMatchAfterglowForHardwareProvenFamilies) {
    // afterglow nec_code(0x04, 0x08) и samsung_code(0x07, 0x02) (builder/codes.py).
    auto nec = ir::nativeCommand("NEC", 0x04, 0x08);
    ASSERT_TRUE(nec);
    EXPECT_EQ(ir::codeText(nec->code, 0), "0x0000E80301010020DF10EF010100");
    auto samsung = ir::nativeCommand("Samsung32", 0x0707, 0x02);
    ASSERT_TRUE(samsung);
    EXPECT_EQ(ir::codeText(samsung->code, 3), "0x0300E803010100E0E040BF0101E0E040BF");
}

TEST(NativeProtocol, NecHoldSendsTheShortRepeatCode) {
    auto native = ir::nativeCommand("NEC", 0x04, 0x08);
    ASSERT_TRUE(native);
    const auto held = run(*native, 0, 2);
    ASSERT_EQ(held.stages, (std::vector<int>{2, 3, 3, 3}));
    // Повтор — 8990 импульс, 2230 пауза, 568 импульс, а не кадр данных.
    for (std::size_t i = 1; i < held.stageDurations.size(); ++i) {
        ASSERT_GE(held.stageDurations[i].size(), 3U);
        EXPECT_EQ(held.stageDurations[i][0], 8990);
        EXPECT_EQ(held.stageDurations[i][1], -2230);
        EXPECT_EQ(held.stageDurations[i][2], 568);
    }
}

TEST(NativeProtocol, JvcRepeatHasNoLeader) {
    auto native = ir::nativeCommand("JVC", 0x03, 0x17);
    ASSERT_TRUE(native);
    const auto held = run(*native, 0, 1);
    ASSERT_EQ(held.stages, (std::vector<int>{2, 3, 3}));
    EXPECT_EQ(held.stageDurations[0].front(), 8400);
    EXPECT_EQ(held.stageDurations[1].front(), 500);
}

TEST(NativeProtocol, SonyPressIsThreeFrames) {
    auto native = ir::nativeCommand("Sony SIRC 12", 0x01, 0x15);
    ASSERT_TRUE(native);
    EXPECT_EQ(run(*native).stages, (std::vector<int>{3, 3, 3}));
}

TEST(NativeProtocol, Rc5ToggleIsSetByTheRemote) {
    auto native = ir::nativeCommand("RC5", 0x05, 0x35);
    ASSERT_TRUE(native);
    const auto off = run(*native, 0);
    const auto on = run(*native, 1);
    EXPECT_NE(off.signal.durations, on.signal.durations);
    const auto first = ir::decode(off.signal);
    const auto second = ir::decode(on.signal);
    ASSERT_TRUE(first && second);
    EXPECT_EQ(first->address, second->address);
    EXPECT_EQ(first->command, second->command);
}

TEST(NativeProtocol, Rc6Mode0ToggleAndHold) {
    auto native = ir::nativeCommand("RC6", 0xA5, 0x5A);
    ASSERT_TRUE(native) << native.error().message;
    const auto off = run(*native, 0);
    const auto on = run(*native, 1);
    ASSERT_EQ(off.stages, (std::vector<int>{3}));
    EXPECT_NE(off.signal.durations, on.signal.durations);
    for (const auto& signal : {off.signal, on.signal}) {
        const auto decoded = ir::decode(signal);
        ASSERT_TRUE(decoded);
        EXPECT_EQ(decoded->protocol, "RC6");
        EXPECT_EQ(decoded->address, 0xA5U);
        EXPECT_EQ(decoded->command, 0x5AU);
    }
    const auto held = run(*native, 0, 2);
    EXPECT_EQ(held.stages, (std::vector<int>{3, 3, 3}));
}

TEST(NativeProtocol, OtherProtocolsAreNotNative) {
    EXPECT_FALSE(ir::hasNativeProtocol("Panasonic"));
    EXPECT_EQ(ir::nativeCommand("Panasonic", 0x80, 0x3D).error().code, ErrorCode::Unsupported);
    EXPECT_EQ(ir::nativeCommand("NEC", 0x100, 0x01).error().code, ErrorCode::Format);
}

TEST(CarrierVm, AgreesWithTheSimpleRendererOnRealCommands) {
    // Каждая команда по протоколу из настоящей конфигурации: исполнитель узнаёт то же,
    // что простой разбор кода.
    auto config = formats::readUserConfiguration(kData / "userconfig" / "UserConfiguration.xml");
    auto table = formats::parseIrProto(readFile(kData / "userconfig" / "IrProto.bin"));
    ASSERT_TRUE(config && table);
    int compared = 0;
    for (const auto& device : config->devices) {
        for (const auto& command : device.commands) {
            if (command.protocol < 0 || static_cast<std::size_t>(command.protocol) >= table->entries.size()) {
                continue;
            }
            SCOPED_TRACE(device.displayName() + " · " + command.name);
            const auto& entry = table->entries[static_cast<std::size_t>(command.protocol)];
            auto code = ir::parseProtocolCode(command.code);
            auto simple = code ? ir::renderProtocol(entry, *code) : std::unexpected(code.error());
            const auto simpleDecoded = simple ? ir::decode(*simple) : std::nullopt;
            if (!simpleDecoded) {
                continue;
            }
            auto bytes = ir::codeBytes(command.code);
            ASSERT_TRUE(bytes);
            auto vm = ir::runCarrier(entry, *bytes);
            ASSERT_TRUE(vm) << vm.error().message;
            const auto vmDecoded = ir::decode(vm->signal);
            ASSERT_TRUE(vmDecoded);
            EXPECT_EQ(vmDecoded->protocol, simpleDecoded->protocol);
            EXPECT_EQ(vmDecoded->address, simpleDecoded->address);
            EXPECT_EQ(vmDecoded->command, simpleDecoded->command);
            ++compared;
        }
    }
    EXPECT_GT(compared, 0);
}

TEST(CarrierVm, RejectsBrokenCode) {
    auto native = ir::nativeCommand("NEC", 0x04, 0x08);
    ASSERT_TRUE(native);
    auto code = native->code;
    code[6] = 7; // элемент 7 — в протоколе их два
    EXPECT_EQ(ir::runCarrier(native->protocol, code).error().code, ErrorCode::Format);
    EXPECT_FALSE(ir::codeBytes("0xABC"));
}

TEST(NativeProtocol, DocumentStoresProtocolCommandsInIrProto) {
    auto document = ConfigDocument::createNew("test");
    auto id = document.createDevice({"TV", "Television", "Acme", "X1"});
    ASSERT_TRUE(id);
    auto nec = document.setProtocolCommand(*id, "PowerToggle", "NEC", 0x04, 0x08);
    ASSERT_TRUE(nec) << nec.error().message;
    EXPECT_TRUE(*nec);
    auto rc5 = document.setProtocolCommand(*id, "VolumeUp", "RC5", 0x05, 0x10);
    ASSERT_TRUE(rc5 && *rc5);
    auto rc6 = document.setProtocolCommand(*id, "VolumeDown", "RC6", 0xA5, 0x5A);
    ASSERT_TRUE(rc6 && *rc6);
    auto panasonic = document.setProtocolCommand(*id, "Mute", "Panasonic", 0x80, 0x3D);
    ASSERT_TRUE(panasonic);
    EXPECT_FALSE(*panasonic); // сигналом
    EXPECT_EQ(document.setProtocolCommand(*id, "Mute", "NEC", 0x100, 1).error().code, ErrorCode::Format);

    const auto* device = document.model().findDevice(*id);
    ASSERT_NE(device, nullptr);
    EXPECT_GE(device->findCommand("PowerToggle")->protocol, 0);
    EXPECT_GE(device->findCommand("VolumeUp")->protocol, 0);
    EXPECT_GE(device->findCommand("VolumeDown")->protocol, 0);
    EXPECT_EQ(device->findCommand("Mute")->protocol, -1);
    // Питание описано, как у выученной команды.
    EXPECT_TRUE(document.deviceStates(*id)->power.toggle.size() > 0);

    // После записи в .EZHex и чтения обратно команды узнаются, хеш протоколов сходится.
    auto ezhex = document.toEzHex(false);
    ASSERT_TRUE(ezhex) << ezhex.error().message;
    auto reopened = ConfigDocument::fromEzHex(*ezhex, "reopened");
    ASSERT_TRUE(reopened) << reopened.error().message;
    for (const auto& [name, protocol, address, command] :
         {std::tuple{"PowerToggle", "NEC", 0x04U, 0x08U}, std::tuple{"VolumeUp", "RC5", 0x05U, 0x10U},
          std::tuple{"VolumeDown", "RC6", 0xA5U, 0x5AU}}) {
        auto details = reopened->commandDetails(*id, name);
        ASSERT_TRUE(details) << name << ": " << details.error().message;
        ASSERT_TRUE(details->decoded) << name;
        EXPECT_EQ(details->decoded->protocol, protocol);
        EXPECT_EQ(details->decoded->address, address);
        EXPECT_EQ(details->decoded->command, command);
        EXPECT_FALSE(details->learned);
    }
    auto container = formats::parseEzHex(*ezhex);
    auto zip = formats::readZip(container->payload);
    const auto irProto = formats::entryContent(*zip->find("userconfig/IrProto.bin"));
    ASSERT_TRUE(irProto);
    EXPECT_EQ(reopened->model().property("ProtocolCacheHash"), formats::protocolHash(*irProto));
    // Для пульта собирается: каждая команда даёт непустой сигнал.
    EXPECT_TRUE(reopened->toEzHex(true)) << reopened->toEzHex(true).error().message;
}

TEST(NativeProtocol, LearnedHoldOfANativeProtocolIsRecognisedWhole) {
    for (const auto& [protocol, address, command] : kCases) {
        SCOPED_TRACE(protocol);
        auto native = ir::nativeCommand(protocol, address, command);
        ASSERT_TRUE(native);
        // Как выучит Harmony при удержании: кадры повтора, бит переключения — какой угодно.
        const auto found = ir::nativeProtocolOf(run(*native, 1, 2).signal);
        ASSERT_TRUE(found);
        EXPECT_EQ(found->protocol, protocol); // RC5X — под именем «RC5»
        EXPECT_EQ(found->address, address);
        EXPECT_EQ(found->command, command);
    }
}

TEST(NativeProtocol, LearnedSignalWithAForeignFrameIsNotNative) {
    auto first = ir::encode("NEC", 0x04, 0x08);
    auto second = ir::encode("NEC", 0x04, 0x09);
    ASSERT_TRUE(first && second);
    ir::IrSignal both = *first;
    both.durations.insert(both.durations.end(), second->durations.begin(), second->durations.end());
    EXPECT_FALSE(ir::nativeProtocolOf(both)); // кнопка из двух разных кадров
    EXPECT_TRUE(ir::nativeProtocolOf(*first));
    EXPECT_FALSE(ir::nativeProtocolOf(*ir::encode("Panasonic", 0x80, 0x3D))); // пульт так не хранит
    EXPECT_FALSE(ir::nativeProtocolOf(ir::IrSignal{38000, {9000, -4500, 560, -560}}));
}

TEST(NativeProtocol, DocumentStoresLearnedCommandsAsProtocolWhenPossible) {
    auto document = ConfigDocument::createNew("test");
    auto id = document.createDevice({"TV", "Television", "Acme", "X1"});
    ASSERT_TRUE(id);
    auto native = ir::nativeCommand("RC6", 0xA5, 0x5A);
    ASSERT_TRUE(native);
    const ir::IrSignal learned = run(*native, 1, 1).signal;
    auto rc6 = document.storeLearned(*id, "VolumeUp", learned);
    ASSERT_TRUE(rc6) << rc6.error().message;
    EXPECT_TRUE(*rc6);
    auto panasonic = document.storeLearned(*id, "Mute", *ir::encode("Panasonic", 0x80, 0x3D));
    ASSERT_TRUE(panasonic) << panasonic.error().message;
    EXPECT_FALSE(*panasonic); // сигналом
    EXPECT_EQ(document.storeLearned(*id, "", learned).error().code, ErrorCode::Format);
    EXPECT_EQ(document.storeLearned("no-such-device", "Mute", learned).error().code, ErrorCode::NotFound);

    const auto* device = document.model().findDevice(*id);
    ASSERT_NE(device, nullptr);
    EXPECT_GE(device->findCommand("VolumeUp")->protocol, 0);
    EXPECT_EQ(device->findCommand("Mute")->protocol, -1);
    auto details = document.commandDetails(*id, "VolumeUp");
    ASSERT_TRUE(details) << details.error().message;
    ASSERT_TRUE(details->decoded);
    EXPECT_EQ(details->decoded->protocol, "RC6");
    EXPECT_EQ(details->decoded->address, 0xA5U);
    EXPECT_EQ(details->decoded->command, 0x5AU);
    EXPECT_FALSE(details->learned);
    EXPECT_TRUE(document.toEzHex(true)) << document.toEzHex(true).error().message;
}
