/**
 * @file deviceStatesTest.cpp
 * @brief Тесты питания и входов устройства: чтение, запись без потерь, правка, заготовка.
 *
 * Фикстура собрана в этом файле (правило 19): значения в ожиданиях — те, что записаны в фикстуре,
 * а не прочитаны из файла реального пульта.
 */

#include "configFixtures.h"

#include "harmony/core/configDocument.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace harmony;

namespace {

/// Телевизор с дискретным питанием и дискретными входами (вход описан через InputType).
constexpr const char* kDiscreteId = "100";
constexpr const char* kToggleId = "200";

constexpr const char* kStatesXml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<Root><Properties><Property name=\"version\">1.0</Property></Properties><User><Id>0</Id></User>"
    "<Device><Id>100</Id><Type>Television</Type><Manufacturer>Panasonic</Manufacturer><Model>TH-50</Model>"
    "<Properties><Property name=\"AlwaysOn\">false</Property></Properties>"
    "<Presentation><Label>Телевизор</Label></Presentation>"
    "<States><State><Id>Power</Id><Value>Off</Value><Value>On</Value><Delay>8000</Delay>"
    "<DiscreteActions>"
    "<SetAction><Action><Target>Device</Target><Operation><Name>SendCommand</Name>"
    "<Parameter name=\"DeviceId\">100</Parameter><Parameter name=\"Command\">PowerOff</Parameter>"
    "<Parameter name=\"Modifier\">Press</Parameter></Operation></Action><Name>Off</Name></SetAction>"
    "<SetAction><Action><Target>Device</Target><Operation><Name>SendCommand</Name>"
    "<Parameter name=\"DeviceId\">100</Parameter><Parameter name=\"Command\">PowerOn</Parameter>"
    "<Parameter name=\"Modifier\">Press</Parameter>"
    "<Parameter name=\"Duration\">2000</Parameter></Operation></Action><Name>On</Name></SetAction>"
    "</DiscreteActions></State>"
    "<State><Id>Input</Id><Value>HDMI1</Value><Value>AV</Value><Delay>1000</Delay>"
    "<DiscreteActions>"
    "<SetAction><Action><Target>Device</Target><Operation><Name>SetValue</Name>"
    "<Parameter name=\"DeviceId\">100</Parameter><Parameter name=\"State\">InputType</Parameter>"
    "<Parameter name=\"Value\">HDMI1</Parameter></Operation></Action><Name>HDMI1</Name></SetAction>"
    "<SetAction><Action><Target>Device</Target><Operation><Name>SendCommand</Name>"
    "<Parameter name=\"DeviceId\">100</Parameter><Parameter name=\"Command\">InputAv</Parameter>"
    "<Parameter name=\"Modifier\">Press</Parameter></Operation></Action><Name>AV</Name></SetAction>"
    "</DiscreteActions></State>"
    "<State><Id>InputType</Id><Value>HDMI1</Value></State></States>"
    "<Commands><Command><Name>PowerOn</Name><Data><Protocol>0</Protocol>"
    "<Code>0x00000000010001000300</Code></Data></Command>"
    "<Command><Name>PowerOff</Name><Data><Protocol>0</Protocol><Code>0x00000000010001000300</Code></Data></Command>"
    "<Command><Name>InputAv</Name><Data><Protocol>0</Protocol><Code>0x00000000010001000300</Code></Data></Command>"
    "</Commands></Device>"
    "<Device><Id>200</Id><Type>Receiver</Type><Manufacturer>Denon</Manufacturer><Model>AVR-X</Model>"
    "<Properties><Property name=\"AlwaysOn\">false</Property></Properties>"
    "<Presentation><Label>Ресивер</Label></Presentation>"
    "<States><State><Id>Power</Id><Value>Off</Value><Value>On</Value><Delay>3000</Delay>"
    "<RelativeActions><NextAction><Action><Target>Device</Target><Operation><Name>SendCommand</Name>"
    "<Parameter name=\"DeviceId\">200</Parameter><Parameter name=\"Command\">PowerToggle</Parameter>"
    "<Parameter name=\"Modifier\">Press</Parameter></Operation></Action></NextAction></RelativeActions></State>"
    "<State><Id>Input</Id><Value>TV</Value><Value>HDMI1</Value><Delay>3000</Delay>"
    "<RelativeActions><NextAction><Action><Target>Device</Target><Operation><Name>SendCommand</Name>"
    "<Parameter name=\"DeviceId\">200</Parameter><Parameter name=\"Command\">InputNext</Parameter>"
    "<Parameter name=\"Modifier\">Press</Parameter></Operation></Action></NextAction></RelativeActions></State>"
    "</States>"
    "<Commands><Command><Name>PowerToggle</Name><Data><Protocol>0</Protocol>"
    "<Code>0x00000000010001000300</Code></Data></Command>"
    "<Command><Name>InputNext</Name><Data><Protocol>0</Protocol><Code>0x00000000010001000300</Code></Data></Command>"
    "</Commands></Device>"
    "<Activity><Id>-1</Id><Type>PowerOff</Type><Presentation><Label>Выключить всё</Label></Presentation>"
    "<EnterActions>"
    "<Action><Target>Device</Target><Operation><Name>SetValue</Name>"
    "<Parameter name=\"DeviceId\">100</Parameter><Parameter name=\"State\">Power</Parameter>"
    "<Parameter name=\"Value\">Off</Parameter></Operation></Action>"
    "<Action><Target>Device</Target><Operation><Name>SetValue</Name>"
    "<Parameter name=\"DeviceId\">200</Parameter><Parameter name=\"State\">Power</Parameter>"
    "<Parameter name=\"Value\">Off</Parameter></Operation></Action>"
    "</EnterActions>"
    "<Power><Off>100</Off><Off>200</Off></Power></Activity>"
    "</Root>";

ConfigDocument openStates() { return harmony::test::openFixture(kStatesXml); }

/// Есть ли в «выключить всё» шаг, выключающий устройство.
bool powerOffStep(const ConfigDocument& document, const std::string& id) {
    const std::string xml = document.toXml();
    const auto begin = xml.find("<Activity><Id>-1</Id>");
    if (begin == std::string::npos) return false;
    const auto end = xml.find("</Activity>", begin);
    const std::string activity = xml.substr(begin, end - begin);
    return activity.find("<Parameter name=\"DeviceId\">" + id + "</Parameter><Parameter name=\"State\">Power") !=
           std::string::npos;
}

} // namespace

TEST(DeviceStates, ReadsDiscretePowerAndInputs) {
    const ConfigDocument document = openStates();
    const auto states = document.deviceStates(kDiscreteId);
    ASSERT_TRUE(states) << states.error().message;

    EXPECT_EQ(states->power.mode, PowerSetup::Mode::Discrete);
    EXPECT_EQ(states->power.delayMs, 8000);
    ASSERT_EQ(states->power.on.size(), 1U);
    EXPECT_EQ(states->power.on[0].command, "PowerOn");
    EXPECT_EQ(states->power.on[0].durationMs, 2000);   // Parameter name="Duration"
    ASSERT_EQ(states->power.off.size(), 1U);
    EXPECT_EQ(states->power.off[0].command, "PowerOff");

    EXPECT_EQ(states->input.mode, InputSetup::Mode::Discrete);
    EXPECT_EQ(states->input.delayMs, 1000);
    ASSERT_EQ(states->input.inputs.size(), 2U);
    EXPECT_EQ(states->input.inputs[0].name, "HDMI1");
    // Вход описан через вспомогательное состояние InputType: это SetValue, а не команда.
    ASSERT_EQ(states->input.inputs[0].select.size(), 1U);
    EXPECT_EQ(states->input.inputs[0].select[0].kind, StateStep::Kind::SetValue);
    EXPECT_EQ(states->input.inputs[0].select[0].state, "InputType");
    EXPECT_EQ(states->input.inputs[0].select[0].value, "HDMI1");
    EXPECT_EQ(states->input.inputs[1].select[0].kind, StateStep::Kind::Command);
    EXPECT_EQ(states->input.inputs[1].select[0].command, "InputAv");
}

TEST(DeviceStates, ToggleDeviceIsRead) {
    const ConfigDocument document = openStates();
    const auto states = document.deviceStates(kToggleId);
    ASSERT_TRUE(states) << states.error().message;

    EXPECT_EQ(states->power.mode, PowerSetup::Mode::Toggle);
    EXPECT_EQ(states->power.delayMs, 3000);
    ASSERT_EQ(states->power.toggle.size(), 1U);
    EXPECT_EQ(states->power.toggle[0].kind, StateStep::Kind::Command);
    EXPECT_EQ(states->power.toggle[0].command, "PowerToggle");
    EXPECT_EQ(states->input.mode, InputSetup::Mode::Cycle);
    ASSERT_EQ(states->input.next.size(), 1U);
    EXPECT_EQ(states->input.next[0].command, "InputNext");
}

TEST(DeviceStates, WritingWhatWasReadKeepsFileIntact) {
    ConfigDocument document = openStates();

    // Id — заранее: каждая правка заново разбирает модель, ссылки на устройства устаревают.
    std::vector<std::string> ids;
    for (const auto& device : document.model().devices) {
        ids.push_back(device.id);
    }

    // Первый проход: то, что прочитали, записали обратно.
    for (const auto& id : ids) {
        const auto states = document.deviceStates(id);
        ASSERT_TRUE(states) << id;
        ASSERT_TRUE(document.setDeviceStates(id, *states)) << id;
        const auto read = document.deviceStates(id);
        ASSERT_TRUE(read) << id;
        EXPECT_EQ(*read, *states) << id;
    }

    // Повтор той же записи файл уже не меняет — он в той форме, которую даёт писатель.
    const std::string written = document.toXml();
    for (const auto& id : ids) {
        const auto states = document.deviceStates(id);
        ASSERT_TRUE(states) << id;
        ASSERT_TRUE(document.setDeviceStates(id, *states)) << id;
    }
    EXPECT_EQ(document.toXml(), written);
}

TEST(DeviceStates, ManualPowerLeavesOffButtonAndBack) {
    ConfigDocument document = openStates();
    auto states = document.deviceStates(kDiscreteId);
    ASSERT_TRUE(states);
    const DeviceStates original = *states;

    states->power = PowerSetup{};
    ASSERT_TRUE(document.setDeviceStates(kDiscreteId, *states));

    // Питание «вручную» — «выключить всё» больше не выключает это устройство,
    // но выключает другое, и входы первого не тронуты.
    EXPECT_FALSE(document.model().findDevice(kDiscreteId)->powerControlled);
    EXPECT_FALSE(powerOffStep(document, kDiscreteId));
    EXPECT_TRUE(powerOffStep(document, kToggleId));
    EXPECT_EQ(document.deviceStates(kDiscreteId)->power.mode, PowerSetup::Mode::Manual);
    EXPECT_EQ(document.deviceStates(kDiscreteId)->input, original.input);

    // Снова с командами — «выключить всё» опять выключает устройство.
    ASSERT_TRUE(document.setDeviceStates(kDiscreteId, original));
    EXPECT_TRUE(document.model().findDevice(kDiscreteId)->powerControlled);
    EXPECT_TRUE(powerOffStep(document, kDiscreteId));
    EXPECT_EQ(document.deviceStates(kDiscreteId)->power, original.power);
}

TEST(DeviceStates, ChangesInputsToCycle) {
    ConfigDocument document = openStates();
    auto states = document.deviceStates(kDiscreteId);
    ASSERT_TRUE(states);

    states->input.mode = InputSetup::Mode::Cycle;
    states->input.inputs = {{"TV", {}}, {"HDMI1", {}}};
    states->input.next = {StateStep::press("InputAv"), StateStep::pause(500)};
    ASSERT_TRUE(document.setDeviceStates(kDiscreteId, *states));

    const auto read = document.deviceStates(kDiscreteId);
    ASSERT_TRUE(read);
    EXPECT_EQ(read->input.mode, InputSetup::Mode::Cycle);
    EXPECT_EQ(read->input.next, states->input.next);
    EXPECT_EQ(document.model().findDevice(kDiscreteId)->inputs,
              (std::vector<std::string>{"TV", "HDMI1"}));
}

TEST(DeviceStates, RejectsBadSetups) {
    ConfigDocument document = openStates();
    const std::string before = document.toXml();
    const auto states = *document.deviceStates(kDiscreteId);

    DeviceStates bad = states;
    bad.power.on = {StateStep::press("NoSuchCommand")};
    EXPECT_EQ(document.setDeviceStates(kDiscreteId, bad).error().code, ErrorCode::NotFound);

    bad = states;
    bad.power.off.clear();
    EXPECT_EQ(document.setDeviceStates(kDiscreteId, bad).error().code, ErrorCode::Format);

    bad = states;
    bad.input.inputs[1].name = bad.input.inputs[0].name;
    EXPECT_EQ(document.setDeviceStates(kDiscreteId, bad).error().code, ErrorCode::Format);

    bad = states;
    bad.input.inputs.clear();
    EXPECT_EQ(document.setDeviceStates(kDiscreteId, bad).error().code, ErrorCode::Format);

    EXPECT_EQ(document.setDeviceStates("nope", states).error().code, ErrorCode::NotFound);
    EXPECT_EQ(document.toXml(), before) << "при ошибке документ не меняется";
}

TEST(DeviceStates, NewDeviceGetsStatesFromItsCommands) {
    ConfigDocument document = openStates();
    const auto id = document.createDevice({"Проектор", "Projector", "Epson", "TW"});
    ASSERT_TRUE(id);
    const ir::IrSignal signal{38000, {9000, -4500, 560, -560, 560, -1690, 560, -40000}};
    for (const char* name : {"PowerToggle", "InputHdmi1", "InputHdmi2", "InputVga"}) {
        ASSERT_TRUE(document.learnCommand(*id, name, signal)) << name;
    }

    std::vector<std::string> names;
    for (const auto& command : document.model().findDevice(*id)->commands) {
        names.push_back(command.name);
    }
    const DeviceStates guess = guessDeviceStates(names, "Projector");
    EXPECT_EQ(guess.power.mode, PowerSetup::Mode::Toggle);
    EXPECT_EQ(guess.power.delayMs, 8000);      // Television/Projector — длинная пауза
    EXPECT_EQ(guess.input.mode, InputSetup::Mode::Discrete);
    ASSERT_EQ(guess.input.inputs.size(), 3U);
    EXPECT_EQ(guess.input.inputs[0].name, "HDMI1");
    EXPECT_EQ(guess.input.inputs[2].name, "VGA");

    ASSERT_TRUE(document.setDeviceStates(*id, guess));
    const auto read = document.deviceStates(*id);
    ASSERT_TRUE(read);
    EXPECT_EQ(*read, guess);
    EXPECT_EQ(document.model().findDevice(*id)->inputs,
              (std::vector<std::string>{"HDMI1", "HDMI2", "VGA"}));
}

TEST(DeviceStates, GuessPrefersSeparateCommandsAndCyclesWithoutNamedInputs) {
    const DeviceStates guess =
        guessDeviceStates({"PowerOn", "PowerOff", "PowerToggle", "InputNext"}, "Receiver");
    EXPECT_EQ(guess.power.mode, PowerSetup::Mode::Discrete);
    EXPECT_EQ(guess.power.delayMs, 3000);       // не Television — короткая пауза
    EXPECT_EQ(guess.input.mode, InputSetup::Mode::Cycle);
    ASSERT_EQ(guess.input.next.size(), 1U);
    EXPECT_EQ(guess.input.next[0].command, "InputNext");
    EXPECT_EQ(guessDeviceStates({"Play"}, "Dvd"), DeviceStates{});
}
