/**
 * @file configCheckTest.cpp
 * @brief Тесты проверки конфигурации, автоматического исправления и сравнения конфигураций.
 *
 * Основа — обезличенная настоящая конфигурация (в ней проблем нет, кроме
 * устройства без команд); проблемы вносятся правкой XML или через API.
 */

#include "harmony/core/configDiff.h"
#include "harmony/core/configDocument.h"
#include "harmony/core/ir/irEncode.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <string>

using namespace harmony;

namespace {

const std::filesystem::path kSampleDir =
    std::filesystem::path(HARMONY_TEST_DATA_DIR) / "userconfig";

std::string sampleXml() {
    std::ifstream stream(kSampleDir / "UserConfiguration.xml", std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

/// Заменяет первое @p from на @p to внутри элемента <Device>/<Activity> с id @p id.
std::string replaceIn(std::string xml, const std::string& tag, const std::string& id,
                      const std::string& from, const std::string& to) {
    const auto begin = xml.find("<" + tag + "><Id>" + id + "</Id>");
    EXPECT_NE(begin, std::string::npos) << id;
    const auto end = xml.find("</" + tag + ">", begin);
    const auto at = xml.find(from, begin);
    EXPECT_TRUE(at != std::string::npos && at < end) << from;
    if (at != std::string::npos && at < end) {
        xml.replace(at, from.size(), to);
    }
    return xml;
}

ConfigDocument open(const std::string& xml) {
    ConfigDocument::Files files;
    files.userConfiguration = xml;
    for (auto [name, target] : {std::pair{"IrProto.bin", &files.irProto}, std::pair{"SsIr.bin", &files.ssIr}}) {
        std::ifstream stream(kSampleDir / name, std::ios::binary);
        *target = {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }
    auto document = ConfigDocument::fromFiles(files, "test");
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

std::size_t errors(const std::vector<Problem>& problems) {
    return countProblems(problems, Problem::Severity::Error);
}

/// Есть проблема с таким шаблоном текста.
bool has(const std::vector<Problem>& problems, const std::string& format) {
    return std::ranges::any_of(problems, [&](const Problem& p) { return p.what.format == format; });
}

} // namespace

TEST(ConfigCheck, RealConfigurationHasNoErrors) {
    const auto problems = open(sampleXml()).check();
    EXPECT_EQ(errors(problems), 0U);
    // Единственное предупреждение — устройство без команд (PlayStation 4).
    ASSERT_EQ(problems.size(), 1U);
    EXPECT_EQ(problems.front().what.format,
              "the device has no IR commands: its buttons do nothing");
}

TEST(ConfigCheck, TargetRemoteSwitchPreservesCommandsAndArchiveIdentity) {
    auto document = ConfigDocument::open(kSampleDir);
    ASSERT_TRUE(document);
    const auto before = document->snapshot();
    const auto identity = document->identity();
    const auto devices = document->model().devices.size();
    const auto activities = document->model().activities.size();
    const auto id = document->model().devices.front().id;
    const auto original = document->commandDetails(id, "PowerToggle");
    ASSERT_TRUE(original);
    ASSERT_TRUE(document->setTargetRemote("665"));
    EXPECT_EQ(document->targetRemote(), "665");
    EXPECT_EQ(document->identity().skin, identity.skin);
    EXPECT_EQ(document->model().devices.size(), devices);
    EXPECT_EQ(document->model().activities.size(), activities);
    const auto after = document->commandDetails(id, "PowerToggle");
    ASSERT_TRUE(after);
    EXPECT_EQ(after->rawData(), original->rawData());
    EXPECT_FALSE(document->toEzHex(true));
    const auto saved = document->toEzHex(false);
    ASSERT_TRUE(saved);
    const auto reopened = ConfigDocument::fromEzHex(*saved, "experimental project");
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->targetRemote(), "665");
    EXPECT_FALSE(reopened->toEzHex(true));
    ASSERT_TRUE(document->restore(before));
    EXPECT_EQ(document->targetRemote(), "900");
    EXPECT_TRUE(document->toEzHex(true));
    EXPECT_FALSE(document->setTargetRemote("unsupported"));
    EXPECT_EQ(document->targetRemote(), "900");
}

TEST(ConfigCheck, MissingIrTablesAreErrorsAndBlockRemotePackage) {
    auto broken = ConfigDocument::fromXml(sampleXml(), "missing tables");
    ASSERT_TRUE(broken);
    EXPECT_GT(errors(broken->check()), 0U);
    EXPECT_FALSE(broken->toEzHex(true));
    EXPECT_TRUE(broken->toEzHex(false)); // damaged documents can still be backed up
}

TEST(ConfigCheck, RecoverTablesFromCompatibleBackupAndUndo) {
    auto donor = ConfigDocument::open(kSampleDir);
    ASSERT_TRUE(donor);
    auto broken = ConfigDocument::fromXml(sampleXml(), "missing tables");
    ASSERT_TRUE(broken);
    const auto snapshot = broken->snapshot();
    const auto recovered = broken->recoverIrTables(*donor);
    ASSERT_TRUE(recovered) << recovered.error().message;
    EXPECT_EQ(errors(broken->check()), 0U);
    EXPECT_TRUE(broken->toEzHex(true));
    const auto package = broken->toEzHex(false);
    ASSERT_TRUE(package);
    auto reopened = ConfigDocument::fromEzHex(*package, "reopened");
    ASSERT_TRUE(reopened);
    EXPECT_EQ(errors(reopened->check()), 0U);
    ASSERT_TRUE(broken->restore(snapshot));
    EXPECT_GT(errors(broken->check()), 0U);
}

TEST(ConfigCheck, RejectsIncompatibleRecoveryWithoutChanges) {
    auto donor = ConfigDocument::open(kSampleDir);
    ASSERT_TRUE(donor);
    auto xml = sampleXml();
    const auto hash = xml.find("0x8595F4FF");
    ASSERT_NE(hash, std::string::npos);
    xml.replace(hash, 10, "0x00000000");
    auto broken = ConfigDocument::fromXml(xml, "different hash");
    ASSERT_TRUE(broken);
    EXPECT_FALSE(broken->recoverIrTables(*donor));
    EXPECT_FALSE(broken->commandDetails(broken->model().devices.front().id, "PowerToggle"));
}

TEST(ConfigCheck, MissingCommandBreaksButtonsAndFixAllFreesThem) {
    // У ресивера пропала команда Mute — кнопки, которые её шлют, не сработают.
    auto document = open(
        replaceIn(sampleXml(), "Device", "82969357", "<Name>Mute</Name>", "<Name>MuteX</Name>"));
    const auto problems = document.check();
    EXPECT_GT(errors(problems), 1U);
    EXPECT_TRUE(has(problems, "the key sends “%1”, %2 has no such command"));
    EXPECT_EQ(problems.front().severity, Problem::Severity::Error); // ошибки — первыми
    const auto broken =
        std::ranges::find_if(problems, [](const Problem& p) { return !p.hardKey.empty(); });
    ASSERT_NE(broken, problems.end());
    EXPECT_EQ(broken->hardKey, "VolumeMute");
    EXPECT_EQ(broken->fix, Problem::Fix::FreeHardKey);

    auto fixed = document.fixAll();
    ASSERT_TRUE(fixed) << fixed.error().message;
    EXPECT_GT(*fixed, 1U);
    EXPECT_EQ(errors(document.check()), 0U);
}

TEST(ConfigCheck, SameActivityNamesAreRenamed) {
    auto document = open(sampleXml());
    const auto& activities = document.model().activities;
    const std::string first = activities[1].label;
    ASSERT_TRUE(document.setActivityLabel(activities[2].id, first));
    auto problems = document.check();
    EXPECT_EQ(std::ranges::count_if(problems,
                                    [](const Problem& p) { return p.fix == Problem::Fix::Rename; }),
              2);

    ASSERT_TRUE(document.fixProblem(*std::ranges::find_if(
        problems, [](const Problem& p) { return p.fix == Problem::Fix::Rename; })));
    EXPECT_FALSE(has(document.check(), "another activity has the same name"));
    EXPECT_NE(document.model().activities[1].label, document.model().activities[2].label);
}

TEST(ConfigCheck, IncompletePowerMapIsCompleted) {
    auto document = open(replaceIn(sampleXml(), "Activity", "62303160", "<Off>90581251</Off>", ""));
    auto problems = document.check();
    const auto power = std::ranges::find_if(
        problems, [](const Problem& p) { return p.fix == Problem::Fix::CompletePowerMap; });
    ASSERT_NE(power, problems.end());
    EXPECT_EQ(power->id, "62303160");
    ASSERT_TRUE(document.fixProblem(*power));
    EXPECT_FALSE(has(document.check(), "not set whether these devices are on or off: %1"));
}

TEST(ConfigCheck, DeletedDeviceReferencesAreRemoved) {
    // Активность включает устройство, которого нет.
    auto document =
        open(replaceIn(sampleXml(), "Activity", "79016674", "<On>", "<On>11111111</On><On>"));
    auto problems = document.check();
    EXPECT_TRUE(has(problems, "switches on a device that was deleted"));
    ASSERT_TRUE(document.fixAll());
    problems = document.check();
    EXPECT_FALSE(has(problems, "switches on a device that was deleted"));
    EXPECT_EQ(errors(problems), 0U);
}

TEST(ConfigCheck, StateWithBothActionKindsIsReported) {
    // Питание телевизора — переключателем; добавляем ещё и прямые «вкл»/«выкл».
    auto document = open(replaceIn(sampleXml(), "Device", "92555930", "</RelativeActions>",
                                   "</RelativeActions><DiscreteActions></DiscreteActions>"));
    const auto problems = document.check();
    EXPECT_TRUE(has(problems, "the state has both step-by-step and direct actions: the remote uses only "
                              "the step-by-step ones, direct selection is ignored"));
    EXPECT_EQ(errors(problems), 0U); // это предупреждение
}

TEST(ConfigCheck, LearnedToggleProtocolIsReported) {
    auto document = open(sampleXml());
    const std::string tv = "92555930";
    auto signal = ir::encode("RC5", 0x05, 0x10);
    ASSERT_TRUE(signal);
    ASSERT_TRUE(document.learnCommand(tv, "Rc5Test", *signal));
    EXPECT_TRUE(has(document.check(),
                    "%1 is stored as a recorded signal: its toggle bit never changes, so the device may take "
                    "two presses in a row for one long press"));
}

TEST(ConfigCheck, ManualOnlyProblemsAreNotFixed) {
    // Две команды с одним именем: какую оставить — решает пользователь.
    auto document = open(
        replaceIn(sampleXml(), "Device", "82969357", "<Name>Mute</Name>", "<Name>VolumeUp</Name>"));
    auto problems = document.check();
    const auto twice = std::ranges::find_if(problems, [](const Problem& p) {
        return p.what.format == "two commands are called “%1”, buttons cannot tell them apart";
    });
    ASSERT_NE(twice, problems.end());
    EXPECT_FALSE(twice->fixable());
    EXPECT_EQ(document.fixProblem(*twice).error().code, ErrorCode::Unsupported);
}

TEST(ConfigDiff, SameConfigurationHasNoChanges) {
    EXPECT_TRUE(compareConfigurations(open(sampleXml()), open(sampleXml())).empty());
}

TEST(ConfigDiff, ShowsRenamedDeviceAndRemovedActivity) {
    const auto before = open(sampleXml());
    auto after = open(sampleXml());
    const auto& device = after.model().devices.front();
    ASSERT_TRUE(after.setDeviceLabel(device.id, "Kitchen TV"));
    const std::string removed = after.model().activities.back().label;
    ASSERT_TRUE(after.removeActivity(after.model().activities.back().id));

    const auto changes = compareConfigurations(before, after);
    ASSERT_EQ(changes.size(), 2U);
    EXPECT_EQ(changes[0].area, ConfigChange::Area::Device);
    EXPECT_EQ(changes[0].kind, ConfigChange::Kind::Changed);
    EXPECT_EQ(changes[0].item, "Kitchen TV");
    ASSERT_EQ(changes[0].details.size(), 1U);
    EXPECT_EQ(changes[0].details[0].format, "name: %1 → %2");
    EXPECT_EQ(changes[1].area, ConfigChange::Area::Activity);
    EXPECT_EQ(changes[1].kind, ConfigChange::Kind::Removed);
    EXPECT_EQ(changes[1].item, removed);
}

TEST(ConfigDiff, ListsCommandChangesWithCount) {
    const auto before = open(sampleXml());
    const auto after = open(
        replaceIn(sampleXml(), "Device", "82969357", "<Name>Mute</Name>", "<Name>MuteX</Name>"));
    const auto changes = compareConfigurations(before, after);
    const auto receiver = std::ranges::find_if(
        changes, [](const ConfigChange& c) { return c.area == ConfigChange::Area::Device; });
    ASSERT_NE(receiver, changes.end());
    ASSERT_GE(receiver->details.size(), 2U);
    EXPECT_EQ(receiver->details[0].format, "new commands: %1");
    EXPECT_EQ(receiver->details[0].args[0], "MuteX");
    EXPECT_EQ(receiver->details[1].format, "commands removed: %1");
}
