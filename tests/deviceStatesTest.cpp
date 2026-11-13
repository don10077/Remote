/**
 * @file deviceStatesTest.cpp
 * @brief Тесты питания и входов устройства: чтение, запись без потерь, правка, заготовка.
 */

#include "harmony/core/configDocument.h"

#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>

using namespace harmony;

namespace {

const std::filesystem::path kSampleDir =
    std::filesystem::path(HARMONY_TEST_DATA_DIR) / "userconfig";
constexpr const char* kTv = "68367800"; ///< Panasonic: отдельные вкл/выкл, входы через InputType

ConfigDocument openSample() {
    auto document = ConfigDocument::open(kSampleDir);
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

/// Устройство, у которого питание — переключатель.
std::string toggleDevice(const ConfigDocument& document) {
    for (const auto& device : document.model().devices) {
        auto states = document.deviceStates(device.id);
        if (states && states->power.mode == PowerSetup::Mode::Toggle) {
            return device.id;
        }
    }
    return {};
}

} // namespace

TEST(DeviceStates, ReadsDiscretePowerAndInputs) {
    const ConfigDocument document = openSample();
    auto states = document.deviceStates(kTv);
    ASSERT_TRUE(states) << states.error().message;
    EXPECT_EQ(states->power.mode, PowerSetup::Mode::Discrete);
    EXPECT_EQ(states->power.delayMs, 8000);
    ASSERT_EQ(states->power.on.size(), 1U);
    EXPECT_EQ(states->power.on[0].command, "PowerOn");
    EXPECT_EQ(states->power.on[0].durationMs, 2000);
    EXPECT_EQ(states->power.off[0].command, "PowerOff");

    EXPECT_EQ(states->input.mode, InputSetup::Mode::Discrete);
    ASSERT_EQ(states->input.inputs.size(), 11U);
    EXPECT_EQ(states->input.inputs[3].name, "HDMI1");
    // Вход Logitech описал через вспомогательное состояние InputType.
    ASSERT_EQ(states->input.inputs[3].select.size(), 1U);
    EXPECT_EQ(states->input.inputs[3].select[0].kind, StateStep::Kind::SetValue);
    EXPECT_EQ(states->input.inputs[3].select[0].state, "InputType");
    EXPECT_EQ(states->input.inputs[3].select[0].value, "HDMI1");
}

TEST(DeviceStates, WritingWhatWasReadKeepsFileIntact) {
    ConfigDocument document = openSample();
    const std::string before = document.toXml();
    // Id — заранее: каждая правка заново разбирает модель, ссылки на устройства устаревают.
    std::vector<std::string> ids;
    for (const auto& device : document.model().devices) {
        ids.push_back(device.id);
    }
    for (const auto& id : ids) {
        auto states = document.deviceStates(id);
        ASSERT_TRUE(states) << id;
        if (states->power.mode == PowerSetup::Mode::Manual &&
            states->input.mode == InputSetup::Mode::None) {
            continue; // нечего записывать: «вручную» ещё и меняет «кнопку Off»
        }
        ASSERT_TRUE(document.setDeviceStates(id, *states)) << id;
    }
    EXPECT_EQ(document.toXml(), before);
}

TEST(DeviceStates, ToggleDeviceIsRead) {
    const ConfigDocument document = openSample();
    const std::string id = toggleDevice(document);
    ASSERT_FALSE(id.empty());
    auto states = document.deviceStates(id);
    ASSERT_FALSE(states->power.toggle.empty());
    EXPECT_EQ(states->power.toggle[0].kind, StateStep::Kind::Command);
}

TEST(DeviceStates, ManualPowerLeavesOffButtonAndBack) {
    ConfigDocument document = openSample();
    auto states = document.deviceStates(kTv);
    ASSERT_TRUE(states);
    const DeviceStates original = *states;

    states->power = PowerSetup{};
    ASSERT_TRUE(document.setDeviceStates(kTv, *states));
    EXPECT_FALSE(document.model().findDevice(kTv)->powerControlled);
    std::string xml = document.toXml();
    const std::string offButton = xml.substr(0, xml.find("</Activity>"));
    EXPECT_EQ(offButton.find(std::string(kTv) + "</Parameter><Parameter name=\"State\">Power"),
              std::string::npos);
    EXPECT_EQ(document.deviceStates(kTv)->power.mode, PowerSetup::Mode::Manual);
    EXPECT_EQ(document.deviceStates(kTv)->input, original.input); // входы не тронуты

    // Снова с командами — «кнопка Off» опять выключает устройство.
    ASSERT_TRUE(document.setDeviceStates(kTv, original));
    EXPECT_TRUE(document.model().findDevice(kTv)->powerControlled);
    xml = document.toXml();
    EXPECT_NE(xml.substr(0, xml.find("</Activity>"))
                  .find(std::string(kTv) + "</Parameter><Parameter name=\"State\">Power"),
              std::string::npos);
    EXPECT_EQ(document.deviceStates(kTv)->power, original.power);
}

TEST(DeviceStates, ChangesInputsToCycle) {
    ConfigDocument document = openSample();
    auto states = document.deviceStates(kTv);
    states->input.mode = InputSetup::Mode::Cycle;
    states->input.inputs = {{"TV", {}}, {"HDMI1", {}}};
    states->input.next = {StateStep::press("AV"), StateStep::pause(500)};
    ASSERT_TRUE(document.setDeviceStates(kTv, *states));
    auto read = document.deviceStates(kTv);
    EXPECT_EQ(read->input.mode, InputSetup::Mode::Cycle);
    EXPECT_EQ(read->input.next, states->input.next);
    EXPECT_EQ(document.model().findDevice(kTv)->inputs, (std::vector<std::string>{"TV", "HDMI1"}));
}

TEST(DeviceStates, RejectsBadSetups) {
    ConfigDocument document = openSample();
    const std::string before = document.toXml();
    auto states = *document.deviceStates(kTv);

    DeviceStates bad = states;
    bad.power.on = {StateStep::press("NoSuchCommand")};
    EXPECT_EQ(document.setDeviceStates(kTv, bad).error().code, ErrorCode::NotFound);
    bad = states;
    bad.power.off.clear();
    EXPECT_EQ(document.setDeviceStates(kTv, bad).error().code, ErrorCode::Format);
    bad = states;
    bad.input.inputs[1].name = bad.input.inputs[0].name;
    EXPECT_EQ(document.setDeviceStates(kTv, bad).error().code, ErrorCode::Format);
    bad = states;
    bad.input.inputs.clear();
    EXPECT_EQ(document.setDeviceStates(kTv, bad).error().code, ErrorCode::Format);
    EXPECT_EQ(document.setDeviceStates("nope", states).error().code, ErrorCode::NotFound);
    EXPECT_EQ(document.toXml(), before);
}

TEST(DeviceStates, NewDeviceGetsStatesFromItsCommands) {
    ConfigDocument document = openSample();
    auto id = document.createDevice({"Проектор", "Projector", "Epson", "TW"});
    ASSERT_TRUE(id);
    const ir::IrSignal signal{38000, {9000, -4500, 560, -560, 560, -1690, 560, -40000}};
    for (const char* name : {"PowerToggle", "InputHdmi1", "InputHdmi2", "InputVga"}) {
        ASSERT_TRUE(document.learnCommand(*id, name, signal));
    }
    std::vector<std::string> names;
    for (const auto& command : document.model().findDevice(*id)->commands) {
        names.push_back(command.name);
    }
    const DeviceStates guess = guessDeviceStates(names, "Projector");
    EXPECT_EQ(guess.power.mode, PowerSetup::Mode::Toggle);
    EXPECT_EQ(guess.power.delayMs, 8000);
    ASSERT_EQ(guess.input.inputs.size(), 3U);
    EXPECT_EQ(guess.input.inputs[0].name, "HDMI1");
    EXPECT_EQ(guess.input.inputs[2].name, "VGA");

    ASSERT_TRUE(document.setDeviceStates(*id, guess));
    EXPECT_EQ(*document.deviceStates(*id), guess);
    EXPECT_EQ(document.model().findDevice(*id)->inputs,
              (std::vector<std::string>{"HDMI1", "HDMI2", "VGA"}));
}

TEST(DeviceStates, GuessPrefersSeparateCommandsAndCyclesWithoutNamedInputs) {
    const DeviceStates guess =
        guessDeviceStates({"PowerOn", "PowerOff", "PowerToggle", "InputNext"}, "Receiver");
    EXPECT_EQ(guess.power.mode, PowerSetup::Mode::Discrete);
    EXPECT_EQ(guess.power.delayMs, 3000);
    EXPECT_EQ(guess.input.mode, InputSetup::Mode::Cycle);
    EXPECT_EQ(guess.input.next[0].command, "InputNext");
    EXPECT_EQ(guessDeviceStates({"Play"}, "Dvd"), DeviceStates{});
}
