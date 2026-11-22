/**
 * @file configCheckTest.cpp
 * @brief Тесты проверки конфигурации, автоматического исправления и сравнения конфигураций.
 *
 * Основа — конфигурация, собранная в коде (`configFixtures.h`), и таблицы, собранные в коде
 * (`formatFixtures.h`): проблемы вносятся правкой XML или через API, а ожидания следуют из
 * конструкции фикстуры (правило 19). Настоящий дамп пульта проверяет отдельный тест через
 * `dumpFiles.h`; без `dumps/` он пропускается.
 */

#include "configFixtures.h"
#include "dumpFiles.h"
#include "formatFixtures.h"

#include "harmony/core/configDiff.h"
#include "harmony/core/configDocument.h"
#include "harmony/core/ir/irEncode.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using namespace harmony;

namespace {

using harmony::test::kActivityId;
using harmony::test::kConfigXml;
using harmony::test::kDvdActivityId;
using harmony::test::kPlayerId;
using harmony::test::kReceiverId;
using harmony::test::kTvId;

using harmony::test::replaceIn;
using harmony::test::withHash;

/// Документ из фикстуры с таблицами из кода.
ConfigDocument open(const std::string& xml) {
    return harmony::test::openFixture(xml);
}

/// Документ без ИК-таблиц — только XML.
ConfigDocument openWithoutTables(const std::string& xml) {
    return harmony::test::openFixture(xml, false);
}

std::size_t errors(const std::vector<Problem>& problems) {
    return countProblems(problems, Problem::Severity::Error);
}

/// Есть проблема с таким шаблоном текста.
bool has(const std::vector<Problem>& problems, const std::string& format) {
    return std::ranges::any_of(problems, [&](const Problem& p) { return p.what.format == format; });
}

const Problem* problem(const std::vector<Problem>& problems, Problem::Fix fix) {
    const auto it = std::ranges::find_if(problems, [fix](const Problem& p) { return p.fix == fix; });
    return it != problems.end() ? &*it : nullptr;
}

/// Места экранных кнопок активности.
std::vector<int> softPositions(const ConfigDocument& document, const std::string& activity) {
    std::vector<int> out;
    for (const auto& group : document.model().findActivity(activity)->controlGroups) {
        if (group.name != "HardButtons") {
            for (const auto& button : group.buttons) {
                out.push_back(button.position);
            }
        }
    }
    std::ranges::sort(out);
    return out;
}

} // namespace

TEST(ConfigCheck, CodeBuiltConfigurationHasNoProblems) {
    const auto problems = open(kConfigXml).check();
    EXPECT_TRUE(problems.empty()) << "фикстура собрана без проблем";
}

TEST(ConfigCheck, MissingIrTablesAreErrorsAndBlockRemotePackage) {
    const auto broken = ConfigDocument::fromXml(kConfigXml, "missing tables");
    ASSERT_TRUE(broken);
    EXPECT_GT(errors(broken->check()), 0U);
    EXPECT_FALSE(broken->toEzHex(true));
    EXPECT_TRUE(broken->toEzHex(false)); // испорченный документ можно хотя бы сохранить
}

TEST(ConfigCheck, RecoveryRefusesBackupWithDifferentHash) {
    const auto donor = open(kConfigXml);
    auto broken = ConfigDocument::fromXml(kConfigXml, "different hash");
    ASSERT_TRUE(broken);
    EXPECT_FALSE(broken->recoverIrTables(donor));
    EXPECT_EQ(broken->recoverIrTables(donor).error().message,
              "the backup protocol table has a different hash");
}

TEST(ConfigCheck, RecoveryFromCompatibleBackupAndUndo) {
    const auto hash = formats::protocolHash(harmony::test::codeTables().irProto);
    const auto donor = open(withHash(kConfigXml, hash));
    auto broken = openWithoutTables(withHash(kConfigXml, hash));
    EXPECT_GT(errors(broken.check()), 0U) << "без таблиц — ошибка";

    const auto snapshot = broken.snapshot();
    const auto recovered = broken.recoverIrTables(donor);
    ASSERT_TRUE(recovered) << recovered.error().message;
    EXPECT_EQ(errors(broken.check()), 0U);
    EXPECT_TRUE(broken.toEzHex(true));

    const auto package = broken.toEzHex(false);
    ASSERT_TRUE(package);
    const auto reopened = ConfigDocument::fromEzHex(*package, "reopened");
    ASSERT_TRUE(reopened);
    EXPECT_EQ(errors(reopened->check()), 0U);

    ASSERT_TRUE(broken.restore(snapshot));
    EXPECT_GT(errors(broken.check()), 0U) << "после отмены таблиц снова нет";
}

TEST(ConfigCheck, MissingCommandBreaksButtonsAndFixAllFreesThem) {
    // У ресивера пропала команда Mute — клавиша VolumeMute шлёт её и не сработает.
    auto document = open(replaceIn(kConfigXml, "Device", kReceiverId, "<Name>Mute</Name>",
                                   "<Name>MuteX</Name>"));
    const auto problems = document.check();
    const auto* broken = problem(problems, Problem::Fix::FreeHardKey);
    ASSERT_NE(broken, nullptr);
    EXPECT_EQ(broken->hardKey, "VolumeMute");
    EXPECT_EQ(broken->severity, Problem::Severity::Error);

    const auto fixed = document.fixAll();
    ASSERT_TRUE(fixed) << fixed.error().message;
    EXPECT_GT(*fixed, 0U);
    EXPECT_TRUE(document.check().empty());
}

TEST(ConfigCheck, SameActivityNamesAreRenamed) {
    auto document = open(kConfigXml);
    ASSERT_TRUE(document.setActivityLabel(kDvdActivityId, document.model().findActivity(kActivityId)->label));

    const auto problems = document.check();
    EXPECT_EQ(std::ranges::count_if(problems, [](const Problem& p) { return p.fix == Problem::Fix::Rename; }),
              2) << "две активности с одним именем";
    const auto rename = problem(problems, Problem::Fix::Rename);
    ASSERT_NE(rename, nullptr);
    ASSERT_TRUE(document.fixProblem(*rename));
    EXPECT_FALSE(has(document.check(), "another activity has the same name"));
    EXPECT_NE(document.model().findActivity(kActivityId)->label,
             document.model().findActivity(kDvdActivityId)->label);
}

TEST(ConfigCheck, IncompletePowerMapIsCompleted) {
    // Активность выключает плеер, но про ресивер ничего не говорит.
    auto document = open(replaceIn(kConfigXml, "Activity", kActivityId, "<On>200</On>", ""));
    const auto problems = document.check();
    const auto* power = problem(problems, Problem::Fix::CompletePowerMap);
    ASSERT_NE(power, nullptr);
    EXPECT_EQ(power->id, kActivityId);
    ASSERT_TRUE(document.fixProblem(*power));
    EXPECT_FALSE(has(document.check(), "not set whether these devices are on or off: %1"));
}

TEST(ConfigCheck, DeletedDeviceReferencesAreRemoved) {
    auto document = open(replaceIn(kConfigXml, "Activity", kActivityId, "<On>100</On>",
                                   "<On>100</On><On>11111111</On>"));
    const auto problems = document.check();
    const auto* deleted = problem(problems, Problem::Fix::RemoveDeleted);
    ASSERT_NE(deleted, nullptr);
    EXPECT_EQ(deleted->what.format, "switches on a device that was deleted");
    ASSERT_TRUE(document.fixAll());
    EXPECT_FALSE(has(document.check(), "switches on a device that was deleted"));
    EXPECT_EQ(errors(document.check()), 0U);
}

TEST(ConfigCheck, StateWithBothActionKindsIsReported) {
    // Питание телевизора — переключателем; добавляем ещё и прямые «вкл»/«выкл».
    auto document = open(replaceIn(kConfigXml, "Device", kTvId, "<RelativeActions/>",
                                   "<RelativeActions/><DiscreteActions/>"));
    const auto problems = document.check();
    EXPECT_TRUE(has(problems, "the state has both step-by-step and direct actions: the remote uses only "
                              "the step-by-step ones, direct selection is ignored"));
    EXPECT_EQ(errors(problems), 0U) << "это предупреждение";
}

TEST(ConfigCheck, BrokenSoftButtonIsRemoved) {
    // Кнопка активности шлётAspect, которой больше нет.
    auto document = open(replaceIn(kConfigXml, "Device", kTvId, "<Name>Aspect</Name>", "<Name>AspectX</Name>"));
    const auto problems = document.check();
    const auto* broken = problem(problems, Problem::Fix::RemoveSoftButton);
    ASSERT_NE(broken, nullptr);
    EXPECT_EQ(broken->id, kActivityId);
    EXPECT_EQ(broken->softPosition, 2);

    const auto copy = *broken;
    ASSERT_TRUE(document.fixProblem(copy));
    EXPECT_EQ(softPositions(document, kActivityId), (std::vector<int>{1, 4}));
    EXPECT_EQ(document.fixProblem(copy).error().code, ErrorCode::NotFound); // уже исправлено
}

TEST(ConfigCheck, SecondSoftButtonOnOnePlaceIsMoved) {
    // Кнопка с места 2 — на место 1, где уже есть кнопка.
    auto document = open(replaceIn(kConfigXml, "Activity", kActivityId, "<Position>2</Position>",
                                   "<Position>1</Position>"));
    const auto problems = document.check();
    const auto* twice = problem(problems, Problem::Fix::MoveSoftButton);
    ASSERT_NE(twice, nullptr);
    EXPECT_EQ(twice->what.format, "two screen buttons at place %1, only one is shown");

    const auto copy = *twice;
    ASSERT_TRUE(document.fixProblem(copy));
    // Первое свободное место — 0 (места кнопок считаются от нуля).
    EXPECT_EQ(softPositions(document, kActivityId), (std::vector<int>{0, 1, 4}));
    EXPECT_EQ(document.fixProblem(copy).error().code, ErrorCode::NotFound);
}

TEST(ConfigCheck, BrokenStartStepsAreRemoved) {
    // Шаг запуска шлёт телевизору Home, которой больше нет.
    auto document = open(replaceIn(kConfigXml, "Device", kTvId, "<Name>Home</Name>", "<Name>HomeX</Name>"));
    const auto problems = document.check();
    const auto* steps = problem(problems, Problem::Fix::RemoveBrokenSteps);
    ASSERT_NE(steps, nullptr);
    EXPECT_EQ(steps->id, kActivityId);

    const auto copy = *steps;
    ASSERT_TRUE(document.fixProblem(copy));
    EXPECT_EQ(document.fixProblem(copy).error().code, ErrorCode::NotFound);
    EXPECT_TRUE(document.check().empty());
    // Шаги выбора входа остались.
    EXPECT_EQ(document.model().findActivity(kActivityId)->inputs.size(), 1U);
}

TEST(ConfigCheck, UsedDeviceIsSwitchedOn) {
    // Кнопка активности шлёт плееру, а активность его выключает.
    auto document = open(replaceIn(kConfigXml, "Activity", kActivityId, "<ActionId>200_VolumeUp_Hold</ActionId>",
                                   "<ActionId>400_PowerToggle_Press</ActionId>"));
    const auto problems = document.check();
    const auto* power = problem(problems, Problem::Fix::PowerOnDevice);
    ASSERT_NE(power, nullptr);
    EXPECT_EQ(power->fixDevice, kPlayerId);
    ASSERT_TRUE(document.fixProblem(*power));

    const auto& on = document.model().findActivity(kActivityId)->powerOn;
    EXPECT_NE(std::ranges::find(on, std::string(kPlayerId)), on.end());
    EXPECT_EQ(problem(document.check(), Problem::Fix::PowerOnDevice), nullptr);
}

TEST(ConfigCheck, ManualOnlyProblemsAreNotFixed) {
    // Две команды с одним именем: какую оставить — решает пользователь.
    auto document = open(replaceIn(kConfigXml, "Device", kReceiverId, "<Name>Mute</Name>", "<Name>VolumeUp</Name>"));
    const auto problems = document.check();
    const auto twice = std::ranges::find_if(problems, [](const Problem& p) {
        return p.what.format == "two commands are called “%1”, buttons cannot tell them apart";
    });
    ASSERT_NE(twice, problems.end());
    EXPECT_FALSE(twice->fixable());
    EXPECT_EQ(document.fixProblem(*twice).error().code, ErrorCode::Unsupported);
}

TEST(ConfigCheck, SameDeviceNamesAreRenamed) {
    auto document = open(kConfigXml);
    ASSERT_TRUE(document.setDeviceLabel(kReceiverId, document.model().findDevice(kTvId)->displayName()));

    const auto problems = document.check();
    const auto rename = std::ranges::find_if(problems, [](const Problem& p) {
        return p.fix == Problem::Fix::Rename && p.where == Problem::Where::Device;
    });
    ASSERT_NE(rename, problems.end());
    EXPECT_EQ(rename->what.format, "another device has the same name");

    ASSERT_TRUE(document.fixProblem(*rename));
    EXPECT_FALSE(has(document.check(), "another device has the same name"));
    EXPECT_NE(document.model().findDevice(kTvId)->displayName(),
             document.model().findDevice(kReceiverId)->displayName());
}

TEST(ConfigCheck, RecordedToggleSignalIsReported) {
    auto document = open(kConfigXml);
    const auto signal = ir::encode("RC5", 0x05, 0x10);
    ASSERT_TRUE(signal) << signal.error().message;
    const auto learned = document.learnCommand(kTvId, "Rc5Test", *signal);
    ASSERT_TRUE(learned) << learned.error().message;

    const auto problems = document.check();
    const auto reported = std::ranges::find_if(problems, [](const Problem& p) {
        return p.what.format == "%1 is stored as a recorded signal: its toggle bit never changes, so the "
                                "device may take two presses in a row for one long press";
    });
    ASSERT_NE(reported, problems.end());
    EXPECT_EQ(reported->severity, Problem::Severity::Warning);
    EXPECT_EQ(reported->id, kTvId);
    ASSERT_EQ(reported->what.args.size(), 1U);
    EXPECT_EQ(reported->what.args[0], "RC5");
}

TEST(ConfigCheck, TargetRemoteSwitchPreservesCommandsAndArchiveIdentity) {
    auto document = open(kConfigXml);
    const auto snapshot = document.snapshot();
    const auto identity = document.identity();
    const std::string start = document.targetRemote();
    const auto original = document.commandDetails(kTvId, "PowerToggle");
    ASSERT_TRUE(original);

    // Модель, не проверенная на запись: пакет собрать нельзя, но сохранить можно.
    ASSERT_TRUE(document.setTargetRemote("665"));
    EXPECT_EQ(document.targetRemote(), "665");
    EXPECT_EQ(document.identity().skin, identity.skin);
    const auto after = document.commandDetails(kTvId, "PowerToggle");
    ASSERT_TRUE(after);
    EXPECT_EQ(after->rawData(), original->rawData());
    EXPECT_FALSE(document.toEzHex(true));

    const auto saved = document.toEzHex(false);
    ASSERT_TRUE(saved);
    const auto reopened = ConfigDocument::fromEzHex(*saved, "reopened");
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->targetRemote(), "665");

    ASSERT_TRUE(document.restore(snapshot));
    EXPECT_EQ(document.targetRemote(), start);
}

TEST(ConfigCheck, RealDumpConfigurationHasNoProblems) {
    const auto dump = harmony::test::dumpFile("EZHex/harmony900", "*.EZHex");
    if (dump.empty()) GTEST_SKIP() << "dumps/EZHex/harmony900 нет — только на машине владельца";

    const auto archive = harmony::test::archiveOf(dump);
    const std::string xml = harmony::test::entryText(archive, "userconfig/UserConfiguration.xml");
    const auto tables = harmony::test::entryData(archive, "userconfig/IrProto.bin");
    if (xml.empty() || tables.empty()) GTEST_SKIP() << "в дампе нет UserConfiguration.xml или IrProto.bin";

    ConfigDocument::Files files;
    files.userConfiguration = xml;
    files.irProto = tables;
    files.ssIr = harmony::test::entryData(archive, "userconfig/SsIr.bin");

    const auto document = ConfigDocument::fromFiles(files, "dump");
    ASSERT_TRUE(document) << document.error().message;
    EXPECT_EQ(errors(document->check()), 0U);
}

TEST(ConfigDiff, SameConfigurationHasNoChanges) {
    EXPECT_TRUE(compareConfigurations(open(kConfigXml), open(kConfigXml)).empty());
}

TEST(ConfigDiff, ShowsRenamedDeviceAndRemovedActivity) {
    const auto before = open(kConfigXml);
    auto after = open(kConfigXml);
    ASSERT_TRUE(after.setDeviceLabel(kTvId, "Kitchen TV"));
    const std::string removed = after.model().findActivity(kDvdActivityId)->label;
    ASSERT_TRUE(after.removeActivity(kDvdActivityId));

    const auto changes = compareConfigurations(before, after);
    // Сначала устройства, затем активности; переименованное устройство видно и в
    // активности, которая его включает.
    ASSERT_EQ(changes.size(), 3U);
    EXPECT_EQ(changes[0].area, ConfigChange::Area::Device);
    EXPECT_EQ(changes[0].kind, ConfigChange::Kind::Changed);
    EXPECT_EQ(changes[0].item, "Kitchen TV");
    ASSERT_EQ(changes[0].details.size(), 1U);
    EXPECT_EQ(changes[0].details[0].format, "name: %1 → %2");

    EXPECT_EQ(changes[1].area, ConfigChange::Area::Activity);
    EXPECT_EQ(changes[1].kind, ConfigChange::Kind::Changed);
    EXPECT_EQ(changes[1].item, "Смотреть ТВ");
    ASSERT_EQ(changes[1].details.size(), 1U);
    EXPECT_EQ(changes[1].details[0].format, "switches on: %1 → %2");

    EXPECT_EQ(changes[2].area, ConfigChange::Area::Activity);
    EXPECT_EQ(changes[2].kind, ConfigChange::Kind::Removed);
    EXPECT_EQ(changes[2].item, removed);
    EXPECT_TRUE(changes[2].details.empty());
}

TEST(ConfigDiff, ListsCommandChangesWithCount) {
    const auto before = open(kConfigXml);
    const auto after = open(replaceIn(kConfigXml, "Device", kReceiverId, "<Name>Mute</Name>", "<Name>MuteX</Name>"));
    const auto changes = compareConfigurations(before, after);
    const auto receiver = std::ranges::find_if(changes,
                                               [](const ConfigChange& c) { return c.area == ConfigChange::Area::Device; });
    ASSERT_NE(receiver, changes.end());
    EXPECT_EQ(receiver->item, "Ресивер");
    ASSERT_EQ(receiver->details.size(), 2U);
    EXPECT_EQ(receiver->details[0].format, "new commands: %1");
    EXPECT_EQ(receiver->details[0].args[0], "MuteX");
    EXPECT_EQ(receiver->details[1].format, "commands removed: %1");
    EXPECT_EQ(receiver->details[1].args[0], "Mute");
}
