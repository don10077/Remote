/**
 * @file undoEditsTest.cpp
 * @brief Отмена и повтор правок, которые меняют не только XML: настройки пульта (архив),
 *        восстановление таблиц ИК-данных, пометка «не отправлять», замена устройства.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/undo/documentEdit.h"
#include "harmony/core/undo/undoStack.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>

using namespace harmony;

namespace {

const std::filesystem::path kData(HARMONY_TEST_DATA_DIR);
const std::filesystem::path kSampleDir = kData / "userconfig";
constexpr const char* kTv = "92555930";     ///< Hisense — в семи активностях
constexpr const char* kUnused = "68367800"; ///< Panasonic — ни в одной

std::string readText(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

ConfigDocument openSample() {
    auto document = ConfigDocument::open(kSampleDir);
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

/// Выполняет правку как шаг истории.
void edit(undo::UndoStack& stack, ConfigDocument& document,
          const std::function<Result<void>(ConfigDocument&)>& change) {
    auto command = undo::editDocument(document, "edit", change);
    ASSERT_TRUE(command) << command.error().message;
    stack.push(std::move(*command));
}

} // namespace

TEST(UndoEdits, RemotePreferenceAndItsXmlCopy) {
    auto document = ConfigDocument::open(kData / "ezhex" / "harmony900.EZHex");
    ASSERT_TRUE(document) << document.error().message;
    const auto level = document->remotePreference("backlight_level");
    const auto format = document->remotePreference("time_format");
    const std::string xml = document->toXml();
    const std::string newFormat = format == "Civilian" ? "Military" : "Civilian";
    undo::UndoStack stack;

    edit(stack, *document,
         [](ConfigDocument& d) { return d.setRemotePreference("backlight_level", "33"); });
    // Формат часов хранится и в XML: отмена возвращает оба места.
    edit(stack, *document,
         [&](ConfigDocument& d) { return d.setRemotePreference("time_format", newFormat); });

    stack.undo();
    stack.undo();
    EXPECT_EQ(document->remotePreference("backlight_level"), level);
    EXPECT_EQ(document->remotePreference("time_format"), format);
    EXPECT_EQ(document->toXml(), xml);

    stack.redo();
    stack.redo();
    EXPECT_EQ(document->remotePreference("backlight_level"), "33");
    EXPECT_EQ(document->remotePreference("time_format"), newFormat);
}

TEST(UndoEdits, RecoveredIrTables) {
    const ConfigDocument donor = openSample();
    auto broken = ConfigDocument::fromXml(readText(kSampleDir / "UserConfiguration.xml"), "no tables");
    ASSERT_TRUE(broken);
    ASSERT_FALSE(broken->checkIrData());
    undo::UndoStack stack;

    edit(stack, *broken, [&](ConfigDocument& d) -> Result<void> {
        auto recovered = d.recoverIrTables(donor);
        if (!recovered) {
            return std::unexpected(recovered.error());
        }
        return {};
    });
    EXPECT_TRUE(broken->checkIrData());

    stack.undo();
    EXPECT_FALSE(broken->checkIrData());
    stack.redo();
    EXPECT_TRUE(broken->checkIrData());
}

TEST(UndoEdits, NotSentMark) {
    ConfigDocument document = openSample();
    const std::string xml = document.toXml();
    undo::UndoStack stack;

    edit(stack, document, [](ConfigDocument& d) { return d.setDeviceNotSent(kUnused, true); });
    ASSERT_TRUE(document.model().findDevice(kUnused)->notSent);

    stack.undo();
    EXPECT_FALSE(document.model().findDevice(kUnused)->notSent);
    EXPECT_EQ(document.toXml(), xml);
    stack.redo();
    EXPECT_TRUE(document.model().findDevice(kUnused)->notSent);
}

TEST(UndoEdits, DeviceReplacement) {
    ConfigDocument document = openSample();
    auto id = document.createDevice(
        {.label = "New TV", .type = "Television", .manufacturer = "", .model = ""});
    ASSERT_TRUE(id);
    const ir::IrSignal signal{38000, {9000, -4500, 560, -560, 560, -1690, 560, -40000}};
    for (const char* name : {"PowerToggle", "VolumeUp", "VolumeDown", "Mute"}) {
        ASSERT_TRUE(document.learnCommand(*id, name, signal)) << name;
    }
    const std::string xml = document.toXml();
    undo::UndoStack stack;

    edit(stack, document, [&](ConfigDocument& d) -> Result<void> {
        DeviceReplacement replacement;
        replacement.oldId = kTv;
        replacement.newId = *id;
        replacement.removeOld = true;
        auto report = d.replaceDevice(replacement);
        if (!report) {
            return std::unexpected(report.error());
        }
        return {};
    });
    ASSERT_EQ(document.model().findDevice(kTv), nullptr);

    stack.undo();
    EXPECT_NE(document.model().findDevice(kTv), nullptr);
    EXPECT_EQ(document.toXml(), xml);
    stack.redo();
    EXPECT_EQ(document.model().findDevice(kTv), nullptr);
}

TEST(UndoEdits, EmptySnapshotIsReported) {
    ConfigDocument document = openSample();
    const std::string xml = document.toXml();
    const auto restored = document.restore(ConfigDocument::Snapshot());
    ASSERT_FALSE(restored);
    EXPECT_EQ(restored.error().code, ErrorCode::NotFound);
    EXPECT_EQ(document.toXml(), xml);
}
