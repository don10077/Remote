/**
 * @file undoEditsTest.cpp
 * @brief Отмена и повтор правок, которые меняют не только XML: настройки пульта (архив),
 *        восстановление таблиц ИК-данных, пометка «не отправлять», замена устройства.
 *
 * Основа — конфигурация и таблицы, собранные в коде (`configFixtures.h`, `formatFixtures.h`),
 * поэтому ожидания следуют из конструкции фикстуры (правило 19).
 */

#include "configFixtures.h"

#include "harmony/core/undo/documentEdit.h"
#include "harmony/core/undo/undoStack.h"

#include <gtest/gtest.h>
#include <functional>
#include <string>

using namespace harmony;

namespace {

using harmony::test::kActivityId;
using harmony::test::kConfigXml;
using harmony::test::kPlayerId;
using harmony::test::kTvId;

/// Выполняет правку как шаг истории.
void edit(undo::UndoStack& stack, ConfigDocument& document,
          const std::function<Result<void>(ConfigDocument&)>& change) {
    auto command = undo::editDocument(document, "edit", change);
    ASSERT_TRUE(command) << command.error().message;
    stack.push(std::move(*command));
}

} // namespace

TEST(UndoEdits, RemotePreferenceAndItsXmlCopy) {
    auto document = harmony::test::openFixture(kConfigXml);
    ASSERT_TRUE(document.setRemotePreference("backlight_level", "20"));
    ASSERT_TRUE(document.setRemotePreference("time_format", "Civilian"));
    const auto level = document.remotePreference("backlight_level");
    const auto format = document.remotePreference("time_format");
    const std::string xml = document.toXml();
    undo::UndoStack stack;

    edit(stack, document, [](ConfigDocument& d) { return d.setRemotePreference("backlight_level", "33"); });
    // Формат часов хранится и в XML: отмена возвращает оба места.
    edit(stack, document, [&](ConfigDocument& d) { return d.setRemotePreference("time_format", "Military"); });

    stack.undo();
    stack.undo();
    EXPECT_EQ(document.remotePreference("backlight_level"), level);
    EXPECT_EQ(document.remotePreference("time_format"), format);
    EXPECT_EQ(document.toXml(), xml);

    stack.redo();
    stack.redo();
    EXPECT_EQ(document.remotePreference("backlight_level"), "33");
    EXPECT_EQ(document.remotePreference("time_format"), "Military");
}

TEST(UndoEdits, RecoveredIrTables) {
    const auto hash = formats::protocolHash(harmony::test::codeTables().irProto);
    const ConfigDocument donor = harmony::test::openFixture(harmony::test::withHash(kConfigXml, hash));
    auto broken = harmony::test::openFixture(harmony::test::withHash(kConfigXml, hash), false);
    ASSERT_FALSE(broken.checkIrData());
    undo::UndoStack stack;

    edit(stack, broken, [&](ConfigDocument& d) -> Result<void> {
        auto recovered = d.recoverIrTables(donor);
        if (!recovered) {
            return std::unexpected(recovered.error());
        }
        return {};
    });
    EXPECT_TRUE(broken.checkIrData());

    stack.undo();
    EXPECT_FALSE(broken.checkIrData());
    stack.redo();
    EXPECT_TRUE(broken.checkIrData());
}

TEST(UndoEdits, NotSentMark) {
    ConfigDocument document = harmony::test::openFixture(kConfigXml);
    const std::string xml = document.toXml();
    undo::UndoStack stack;

    edit(stack, document, [](ConfigDocument& d) { return d.setDeviceNotSent(kPlayerId, true); });
    ASSERT_TRUE(document.model().findDevice(kPlayerId)->notSent);

    stack.undo();
    EXPECT_FALSE(document.model().findDevice(kPlayerId)->notSent);
    EXPECT_EQ(document.toXml(), xml);
    stack.redo();
    EXPECT_TRUE(document.model().findDevice(kPlayerId)->notSent);
}

TEST(UndoEdits, DeviceReplacement) {
    ConfigDocument document = harmony::test::openFixture(kConfigXml);
    auto id = document.createDevice(
        {.label = "New TV", .type = "Television", .manufacturer = "", .model = ""});
    ASSERT_TRUE(id);
    // Сигнал задан здесь же: один кадр NEC, устройство 0x05, команда 0x10.
    const ir::IrSignal signal{38000, {9000, -4500, 560, -560, 560, -1690, 560, -40000}};
    for (const char* name : {"PowerToggle", "VolumeUp", "VolumeDown", "Mute"}) {
        ASSERT_TRUE(document.learnCommand(*id, name, signal)) << name;
    }
    const std::string xml = document.toXml();
    undo::UndoStack stack;

    edit(stack, document, [&](ConfigDocument& d) -> Result<void> {
        DeviceReplacement replacement;
        replacement.oldId = kTvId;
        replacement.newId = *id;
        replacement.removeOld = true;
        auto report = d.replaceDevice(replacement);
        if (!report) {
            return std::unexpected(report.error());
        }
        return {};
    });
    ASSERT_EQ(document.model().findDevice(kTvId), nullptr);

    stack.undo();
    EXPECT_NE(document.model().findDevice(kTvId), nullptr);
    EXPECT_EQ(document.toXml(), xml);
    stack.redo();
    EXPECT_EQ(document.model().findDevice(kTvId), nullptr);
}

TEST(UndoEdits, EmptySnapshotIsReported) {
    ConfigDocument document = harmony::test::openFixture(kConfigXml);
    const std::string xml = document.toXml();
    const auto restored = document.restore(ConfigDocument::Snapshot());
    ASSERT_FALSE(restored);
    EXPECT_EQ(restored.error().code, ErrorCode::NotFound);
    EXPECT_EQ(document.toXml(), xml);
}

TEST(UndoEdits, UndoRestoresExactTextOfAnEdit) {
    ConfigDocument document = harmony::test::openFixture(kConfigXml);
    const std::string xml = document.toXml();
    undo::UndoStack stack;

    edit(stack, document,
         [&](ConfigDocument& d) { return d.setActivityLabel(kActivityId, "ТВ в гостиной"); });
    EXPECT_NE(document.toXml(), xml);

    stack.undo();
    EXPECT_EQ(document.toXml(), xml);
    stack.redo();
    EXPECT_NE(document.toXml(), xml);
}
