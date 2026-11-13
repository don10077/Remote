/**
 * @file deviceComparisonTest.cpp
 * @brief Тесты сравнения команд устройств и копирования отмеченных команд.
 */

#include "harmony/core/configDocument.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

using namespace harmony;

namespace {

const std::filesystem::path kSampleDir = std::filesystem::path(HARMONY_TEST_DATA_DIR) / "userconfig";
constexpr const char* kTv = "68367800"; ///< Panasonic: команды по протоколу

ConfigDocument openSample() {
    auto document = ConfigDocument::open(kSampleDir);
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

/// NEC-сигнал адрес/команда.
ir::IrSignal nec(unsigned address, unsigned command) {
    ir::IrSignal s;
    s.frequency = 38000;
    s.durations = {9000, -4500};
    const unsigned value = address | ((~address & 0xFFu) << 8) | (command << 16) | ((~command & 0xFFu) << 24);
    for (int bit = 0; bit < 32; ++bit) {
        s.durations.push_back(560);
        s.durations.push_back(((value >> bit) & 1u) != 0 ? -1690 : -560);
    }
    s.durations.push_back(560);
    s.durations.push_back(-40000);
    return s;
}

const CommandComparison* find(const std::vector<CommandComparison>& list, const std::string& name) {
    const auto it = std::ranges::find(list, name, &CommandComparison::name);
    return it != list.end() ? &*it : nullptr;
}

} // namespace

TEST(DeviceComparison, StatusesAndSignals) {
    ConfigDocument document = openSample();
    auto a = document.createDevice({"A", "Television", "Acme", "1"});
    auto b = document.createDevice({"B", "Television", "Acme", "2"});
    ASSERT_TRUE(a && b);
    ASSERT_TRUE(document.learnCommand(*a, "Same", nec(4, 8)));
    ASSERT_TRUE(document.learnCommand(*b, "Same", nec(4, 8)));
    ASSERT_TRUE(document.learnCommand(*a, "Other", nec(4, 9)));
    ASSERT_TRUE(document.learnCommand(*b, "Other", nec(4, 10)));
    ASSERT_TRUE(document.learnCommand(*a, "mine", nec(4, 11)));
    ASSERT_TRUE(document.learnCommand(*b, "Theirs", nec(4, 12)));
    // Тот же сигнал, чуть иначе выученный (дрожание длительностей) — тоже «тот же».
    ir::IrSignal noisy = nec(4, 13);
    for (auto& d : noisy.durations) {
        d += d > 0 ? 40 : -40;
    }
    ASSERT_TRUE(document.learnCommand(*a, "Noisy", nec(4, 13)));
    ASSERT_TRUE(document.learnCommand(*b, "Noisy", noisy));

    const auto list = document.compareDeviceCommands(*a, *b);
    ASSERT_EQ(list.size(), 5U);
    EXPECT_EQ(list.front().name, "mine"); // по алфавиту без учёта регистра
    EXPECT_EQ(find(list, "Same")->status, CommandComparison::Status::Same);
    EXPECT_EQ(find(list, "Noisy")->status, CommandComparison::Status::Same);
    EXPECT_EQ(find(list, "Other")->status, CommandComparison::Status::Different);
    EXPECT_EQ(find(list, "mine")->status, CommandComparison::Status::OnlyFirst);
    EXPECT_EQ(find(list, "Theirs")->status, CommandComparison::Status::OnlySecond);
    EXPECT_NE(find(list, "Same")->first.find("NEC"), std::string::npos);
    EXPECT_TRUE(find(list, "mine")->second.empty());
    EXPECT_TRUE(document.compareDeviceCommands(*a, "nope").empty());
}

TEST(DeviceComparison, CopiesMarkedCommands) {
    ConfigDocument document = openSample();
    auto target = document.createDevice({"Copy", "Television", "Panasonic", "X"});
    ASSERT_TRUE(target);
    ASSERT_TRUE(document.learnCommand(*target, "VolumeUp", nec(1, 1))); // заменится
    const std::size_t errorsBefore = countProblems(document.check(), Problem::Severity::Error);

    auto copied = document.copyCommands(kTv, *target, {"VolumeUp", "PowerOn", "PowerOff", "Mute"});
    ASSERT_TRUE(copied) << copied.error().message;
    EXPECT_EQ(*copied, 4);
    const auto list = document.compareDeviceCommands(kTv, *target);
    for (const char* name : {"VolumeUp", "PowerOn", "PowerOff", "Mute"}) {
        ASSERT_NE(find(list, name), nullptr) << name;
        EXPECT_EQ(find(list, name)->status, CommandComparison::Status::Same) << name;
    }
    EXPECT_EQ(find(list, "VolumeDown")->status, CommandComparison::Status::OnlyFirst);
    // Команды питания — питание описано, как при обучении.
    auto states = document.deviceStates(*target);
    ASSERT_TRUE(states);
    EXPECT_EQ(states->power.mode, PowerSetup::Mode::Discrete);
    EXPECT_LE(countProblems(document.check(), Problem::Severity::Error), errorsBefore);
    // Сигнал скопированной команды — тот же.
    EXPECT_EQ(document.commandSignal(kTv, "Mute")->durations, document.commandSignal(*target, "Mute")->durations);
}

TEST(DeviceComparison, RejectsBadArguments) {
    ConfigDocument document = openSample();
    EXPECT_EQ(document.copyCommands(kTv, "nope", {"Mute"}).error().code, ErrorCode::NotFound);
    EXPECT_EQ(document.copyCommands(kTv, kTv, {"Mute"}).error().code, ErrorCode::Format);
    auto target = document.createDevice({"Copy", "Television", "Panasonic", "X"});
    ASSERT_TRUE(target);
    EXPECT_EQ(document.copyCommands(kTv, *target, {"NoSuchCommand"}).error().code, ErrorCode::NotFound);
}
