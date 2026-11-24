/**
 * @file openEntryTest.cpp
 * @brief I4: один вход «открыть файл конфигурации» — что открывается в редакторе.

Тесты фиксируют решение входа: всё, что умеет таблица `configReaders()` (I1), открывается;
остальное — остаётся файлом. Вход решает, что за файл (папка, снятый JSON, .EZHex); таблица
решает, чем является содержимое контейнера.

Красных тестов здесь быть не могло: поведение входа уже обеспечивают `B4` (модель по identity
через каталог) и `M2` (модель без свойства цели). Проверяется сам вход — файлы и снятый JSON.
 */

#include "configFixtures.h"
#include "formatFixtures.h"
#include "zipFixtures.h"

#include "harmony/core/configDocument.h"
#include "harmony/core/formats/remoteCapture.h"
#include "harmony/core/remoteCatalog.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace {

using harmony::ConfigDocument;
using harmony::formats::Bytes;
using harmony::formats::CaptureContainer;

std::string text(const Bytes& bytes) { return std::string(bytes.begin(), bytes.end()); }

// Вход открывает то же, что читатель таблицы: тот же документ, та же модель по каталогу.
// Ожидания следуют из конструкции фикстуры (правило 19), имена форматов в тесте не сравниваются.
void checkEntryOpensTheSameAsTheReader(const Bytes& file, int skin) {
    harmony::test::TempFolder folder("open-entry");
    const auto path = folder.path() / "config.EZHex";
    harmony::test::writeFile(path, text(file));

    const auto byEntry = ConfigDocument::open(path);
    const auto direct = ConfigDocument::fromEzHex(file, "direct");
    ASSERT_TRUE(byEntry) << byEntry.error().message;
    ASSERT_TRUE(direct) << direct.error().message;

    EXPECT_EQ(byEntry->model().devices.size(), direct->model().devices.size());
    EXPECT_EQ(byEntry->backend()->format(), direct->backend()->format());
    // Имя — из конструкции: вход берёт имя файла, прямой вызов — переданное имя.
    EXPECT_EQ(byEntry->name(), path.stem().string());
    EXPECT_EQ(direct->name(), "direct");
    // B4: модель отвечает каталог по skin, а не имя в файле.
    const auto* model = harmony::catalogModelBySkin(skin);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(byEntry->targetRemote(), harmony::normalizedModelId(model->id));
    EXPECT_EQ(direct->targetRemote(), harmony::normalizedModelId(model->id));
}

TEST(OpenEntry, ZipConfigurationOpensThroughTheEntry) {
    checkEntryOpensTheSameAsTheReader(harmony::test::zipEzHex(harmony::test::kConfigXml), 61);
}

TEST(OpenEntry, BinaryImageOpensThroughTheEntry) {
    checkEntryOpensTheSameAsTheReader(harmony::test::minimalEzHex(14, 72), 72);
}

// Снят файл для анализа (`*.remote.json`): открывается тем же путём, что файл, потому что
// в данных есть контейнер. Место на диске не запоминается — «Сохранить» не затрёт снятый файл.
TEST(OpenEntry, RemoteCaptureOpensThroughTheSameReader) {
    harmony::test::TempFolder folder("open-entry-capture");

    harmony::formats::RemoteCapture capture;
    capture.modelId = "650";
    capture.model = "Harmony 650";
    capture.identity.architecture = 14;
    capture.identity.skin = 72;
    capture.container = CaptureContainer::EzHex;
    capture.data = harmony::test::minimalEzHex(14, 72);

    const auto json = harmony::formats::writeRemoteCapture(capture);
    ASSERT_TRUE(json) << json.error().message;
    const auto path = folder.path() / "Harmony-650-20261124-1200.remote.json";
    harmony::test::writeFile(path, *json);

    const auto byEntry = ConfigDocument::open(path);
    const auto direct = ConfigDocument::fromEzHex(capture.data, "direct");
    ASSERT_TRUE(byEntry) << byEntry.error().message;
    ASSERT_TRUE(direct) << direct.error().message;
    EXPECT_EQ(byEntry->model().devices.size(), direct->model().devices.size());
    EXPECT_EQ(byEntry->backend()->format(), direct->backend()->format());
    EXPECT_TRUE(byEntry->location().empty());
    EXPECT_TRUE(direct->location().empty());
    // Имя — из конструкции файла: «.remote» убирается.
    EXPECT_EQ(byEntry->name(), "Harmony-650-20261124-1200");
}

// Данные без контейнера (старый `*.hid.json`, вид Raw): читателя таблицы нет, поэтому файл
// остаётся файлом. Текст называет файл и модель (правило 18).
TEST(OpenEntry, CaptureWithoutContainerStaysAFile) {
    harmony::test::TempFolder folder("open-entry-raw");

    harmony::formats::RemoteCapture capture;
    capture.modelId = "650";
    capture.model = "Harmony 650";
    capture.identity.architecture = 14;
    capture.identity.skin = 72;
    capture.container = CaptureContainer::EzHex;
    capture.data = harmony::test::minimalEzHex(14, 72);

    const auto json = harmony::formats::writeRemoteCapture(capture);
    ASSERT_TRUE(json) << json.error().message;
    auto root = nlohmann::json::parse(*json);
    root["data"]["container"] = "raw"; // вид без контейнера: читателя таблицы нет

    const auto path = folder.path() / "unknown.remote.json";
    harmony::test::writeFile(path, root.dump());

    const auto opened = ConfigDocument::open(path);
    ASSERT_FALSE(opened) << "файл для анализа без контейнера не открывается в редакторе";
    EXPECT_EQ(opened.error().code, harmony::ErrorCode::Unsupported);
    EXPECT_TRUE(opened.error().message.contains("cannot be opened in the editor yet"))
        << opened.error().message;
    EXPECT_TRUE(opened.error().message.contains("Harmony 650")) << "названа модель (правило 18)";
    EXPECT_TRUE(opened.error().message.contains(path.string())) << "назван файл (правило 18)";
}

// Снят ZIP пультов MH: читателя таблицы у него нет, поэтому он тоже остаётся файлом.
TEST(OpenEntry, CaptureOfAnotherContainerStaysAFile) {
    harmony::test::TempFolder folder("open-entry-mh");

    harmony::formats::RemoteCapture capture;
    capture.modelId = "730";
    capture.model = "Harmony 730";
    capture.identity.architecture = 16;
    capture.identity.skin = 99;
    capture.container = CaptureContainer::Mh;
    capture.data = harmony::test::zipEzHex(harmony::test::kConfigXml,
                                          harmony::formats::RemoteIdentity{.architecture = 16, .skin = 99});

    const auto json = harmony::formats::writeRemoteCapture(capture);
    ASSERT_TRUE(json) << json.error().message;
    const auto path = folder.path() / "harmony-730.remote.json";
    harmony::test::writeFile(path, *json);

    const auto opened = ConfigDocument::open(path);
    ASSERT_FALSE(opened);
    EXPECT_EQ(opened.error().code, harmony::ErrorCode::Unsupported);
    EXPECT_TRUE(opened.error().message.contains("Harmony 730")) << "названа модель (правило 18)";
}

} // namespace
