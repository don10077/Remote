/**
 * @file zoneEditingTest.cpp
 * @brief Тесты логики редактора зон: проверка, разбор и точечная правка JSON.
 */

#include "../app/src/zoneEditing.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

namespace ze = harmony::app::zoneEditing;

namespace {

TEST(ZoneEditing, RectRoundTrip) {
    QJsonObject zone;
    const QRectF rect(1.234, 5.678, 90.126, 40.999);
    ze::setRect(zone, rect);
    const QRectF parsed = ze::rectOf(zone);
    EXPECT_DOUBLE_EQ(parsed.x(), 1.23);
    EXPECT_DOUBLE_EQ(parsed.y(), 5.68);
    EXPECT_DOUBLE_EQ(parsed.width(), 90.13);
    EXPECT_DOUBLE_EQ(parsed.height(), 41.0);
}

TEST(ZoneEditing, InvalidRectReturnsEmpty) {
    EXPECT_TRUE(ze::rectOf(QJsonObject()).isEmpty());
    EXPECT_TRUE(ze::rectOf(QJsonObject{{"rect", QJsonArray{1, 2, 3}}}).isEmpty());
    EXPECT_TRUE(ze::rectOf(QJsonObject{{"rect", "not an array"}}).isEmpty());
    EXPECT_TRUE(ze::rectOf(QJsonObject{{"rect", QJsonArray{1, 2, -10, 20}}}).isEmpty());
}

TEST(ZoneEditing, CheckBodyDetectsErrors) {
    QJsonArray zones;
    QJsonObject zone1;
    zone1["id"] = "PowerOff";
    zone1["kind"] = "Key";
    zone1["rect"] = QJsonArray{10, 20, 30, 40};
    zone1["radius"] = 4.0;
    zones.append(zone1);

    QJsonObject zone2;
    zone2["id"] = "PowerOff"; // duplicate
    zone2["kind"] = "Key";
    zone2["rect"] = QJsonArray{50, 60, 30, 40};
    zone2["radius"] = 4.0;
    zones.append(zone2);

    QJsonObject zone3;
    zone3["id"] = "OK";
    zone3["kind"] = "UnknownKind";
    zone3["rect"] = QJsonArray{100, 100, 20, 20};
    zone3["radius"] = 3.0;
    zones.append(zone3);

    const auto issues = ze::checkBody(zones, QSizeF(200, 300), std::nullopt);
    int errors = 0;
    for (const auto& issue : issues)
        if (issue.level == ze::Issue::Level::Error) ++errors;
    EXPECT_GE(errors, 2); // duplicate id + unknown kind
}

TEST(ZoneEditing, CheckBodyDetectsMissingKey) {
    QJsonArray zones;
    QJsonObject zone;
    zone["id"] = "Display";
    zone["kind"] = "System";
    zone["rect"] = QJsonArray{10, 20, 100, 50};
    zone["radius"] = 0.0;
    zones.append(zone);

    const auto issues = ze::checkBody(zones, QSizeF(200, 300), std::nullopt);
    bool foundWarning = false;
    for (const auto& issue : issues) {
        if (issue.level == ze::Issue::Level::Warning && issue.text.contains("Key")) {
            foundWarning = true;
            break;
        }
    }
    EXPECT_TRUE(foundWarning);
}

TEST(ZoneEditing, CheckScreenDetectsInvalidButton) {
    QJsonArray zones;
    QJsonObject slot;
    slot["id"] = "Slot1";
    slot["kind"] = "ScreenSlot";
    slot["rect"] = QJsonArray{10, 10, 40, 40};
    slot["button"] = "NonExistentButton";
    zones.append(slot);

    const QSet<QString> bodyIds{"PowerOff", "VolumeUp"};
    const QSet<QString> modes{"Home", "Menu"};
    const auto issues = ze::checkScreen(zones, QSizeF(100, 100), bodyIds, modes);
    bool foundButtonWarning = false;
    for (const auto& issue : issues) {
        if (issue.text.contains("NonExistentButton")) {
            foundButtonWarning = true;
            break;
        }
    }
    EXPECT_TRUE(foundButtonWarning);
}

TEST(ZoneEditing, ZoneLineFormat) {
    QJsonObject zone;
    zone["id"] = "TestZone";
    zone["rect"] = QJsonArray{1.5, 2.5, 30, 40};
    zone["kind"] = "Key";
    zone["radius"] = 4.0;
    const QByteArray line = ze::zoneLine(zone);
    EXPECT_TRUE(line.contains("\"id\": \"TestZone\""));
    EXPECT_TRUE(line.contains("\"rect\": [1.5, 2.5, 30, 40]"));
    EXPECT_TRUE(line.contains("\"kind\": \"Key\""));
    EXPECT_TRUE(line.contains("\"radius\": 4"));
}

TEST(ZoneEditing, ParseZonesFromArray) {
    const QByteArray text = R"([
        {"id": "Zone1", "rect": [10, 20, 30, 40], "kind": "Key"},
        {"id": "Zone2", "rect": [50, 60, 30, 40], "kind": "Key"}
    ])";
    const QJsonArray parsed = ze::parseZones(text);
    EXPECT_EQ(parsed.size(), 2);
    EXPECT_EQ(parsed[0].toObject()["id"].toString(), "Zone1");
    EXPECT_EQ(parsed[1].toObject()["id"].toString(), "Zone2");
}

TEST(ZoneEditing, ParseZonesFromSingleObject) {
    const QByteArray text = R"({"id": "SingleZone", "rect": [10, 20, 30, 40], "kind": "Key"})";
    const QJsonArray parsed = ze::parseZones(text);
    EXPECT_EQ(parsed.size(), 1);
    EXPECT_EQ(parsed[0].toObject()["id"].toString(), "SingleZone");
}

TEST(ZoneEditing, ParseZonesFromLines) {
    const QByteArray text = R"(
        {"id": "Zone1", "rect": [10, 20, 30, 40], "kind": "Key"},
        {"id": "Zone2", "rect": [50, 60, 30, 40], "kind": "Key"}
    )";
    const QJsonArray parsed = ze::parseZones(text);
    EXPECT_EQ(parsed.size(), 2);
}

TEST(ZoneEditing, ParseZonesReturnsEmptyOnInvalid) {
    EXPECT_TRUE(ze::parseZones("not json").isEmpty());
    EXPECT_TRUE(ze::parseZones("123").isEmpty());
    EXPECT_TRUE(ze::parseZones("\"string\"").isEmpty());
}

/// Документ profile.json в миниатюре: зоны корпуса, рамка дисплея и два экрана с ячейками.
const QByteArray kRemote = R"({
    "id": "880",
    "hardware": {
        "size": [400, 800],
        "zones": [
            {"id": "Old1", "rect": [1, 2, 3, 4]},
            {"id": "Old2", "rect": [5, 6, 7, 8]}
        ]
    },
    "display": {"rect": [100, 50, 200, 150], "tint": "#ffffff"},
    "screens": [
        {
            "id": "Home",
            "ref": [240, 320],
            "cells": [
                {"id": "A", "rect": [1, 2, 3, 4]},
                {"id": "B", "rect": [5, 6, 7, 8]}
            ]
        },
        {
            "id": "Menu",
            "ref": [240, 320],
            "cells": [
                {"id": "C", "rect": [9, 10, 11, 12]}
            ]
        }
    ]
})";

TEST(ZoneEditing, PatchZonesReplacesBodyZones) {
    const auto patched = ze::patchZones(kRemote, QString(),
        {R"({"id": "New1", "rect": [10, 20, 30, 40]})", R"({"id": "New2", "rect": [50, 60, 70, 80]})"});
    ASSERT_TRUE(patched.has_value());
    const auto root = QJsonDocument::fromJson(*patched).object();
    const auto zones = root["hardware"].toObject()["zones"].toArray();
    ASSERT_EQ(zones.size(), 2);
    EXPECT_EQ(zones[0].toObject()["id"].toString(), "New1");
    EXPECT_EQ(zones[1].toObject()["id"].toString(), "New2");
    // Экраны не тронуты.
    EXPECT_EQ(root["screens"].toArray()[0].toObject()["cells"].toArray().size(), 2);
}

TEST(ZoneEditing, PatchZonesReplacesCellsOfOneScreen) {
    const auto patched = ze::patchZones(kRemote, "Home", {R"({"id": "N", "rect": [20, 20, 60, 60]})"});
    ASSERT_TRUE(patched.has_value());
    const auto root = QJsonDocument::fromJson(*patched).object();
    const auto screens = root["screens"].toArray();
    const auto home = screens[0].toObject()["cells"].toArray();
    ASSERT_EQ(home.size(), 1);
    EXPECT_EQ(home[0].toObject()["id"].toString(), "N");
    EXPECT_EQ(screens[1].toObject()["cells"].toArray().size(), 1);
    EXPECT_EQ(root["hardware"].toObject()["zones"].toArray().size(), 2);
}

TEST(ZoneEditing, PatchZonesUnknownScreenReturnsNothing) {
    EXPECT_FALSE(ze::patchZones(kRemote, "Nope", {}).has_value());
}

TEST(ZoneEditing, PatchGridWritesColsRowsAndKeepsGap) {
    QByteArray text = kRemote;
    text.replace(R"("id": "Home",)", R"("id": "Home", "grid": {"cols": 2, "rows": 1, "gap": 3},)");
    const auto patched = ze::patchGrid(text, "Home", 1, 3);
    ASSERT_TRUE(patched.has_value());
    const auto grid = QJsonDocument::fromJson(*patched).object()["screens"].toArray()[0].toObject()["grid"].toObject();
    EXPECT_EQ(grid["cols"].toInt(), 1);
    EXPECT_EQ(grid["rows"].toInt(), 3);
    EXPECT_EQ(grid["gap"].toInt(), 3);
    // Экран без сетки не меняется, неизвестный экран — ошибка.
    const auto untouched = ze::patchGrid(kRemote, "Menu", 2, 2);
    ASSERT_TRUE(untouched.has_value());
    EXPECT_TRUE(*untouched == kRemote);
    EXPECT_FALSE(ze::patchGrid(kRemote, "Nope", 2, 2).has_value());
}

TEST(ZoneEditing, PatchGridWritesNewGapOnlyWhenNotDefault) {
    QByteArray text = kRemote;
    text.replace(R"("id": "Home",)", R"("id": "Home", "grid": {"cols": 2, "rows": 1},)");
    const auto gridOf = [](const QByteArray& patched) {
        return QJsonDocument::fromJson(patched).object()["screens"].toArray()[0].toObject()["grid"].toObject();
    };
    // Зазор по умолчанию в файл не дописывается.
    const auto same = ze::patchGrid(text, "Home", 2, 1, ze::kDefaultGap);
    ASSERT_TRUE(same.has_value());
    EXPECT_FALSE(gridOf(*same).contains("gap"));
    const auto wider = ze::patchGrid(text, "Home", 2, 1, 6.5);
    ASSERT_TRUE(wider.has_value());
    EXPECT_DOUBLE_EQ(gridOf(*wider)["gap"].toDouble(), 6.5);
}

TEST(ZoneEditing, PatchGridKeepsTileArea) {
    QByteArray text = kRemote;
    text.replace(R"("id": "Home",)", R"("id": "Home", "grid": {"cols": 2, "rows": 1, "area": [10, 20, 100, 50]},)");
    const auto patched = ze::patchGrid(text, "Home", 3, 2);
    ASSERT_TRUE(patched.has_value());
    const auto grid = QJsonDocument::fromJson(*patched).object()["screens"].toArray()[0].toObject()["grid"].toObject();
    EXPECT_EQ(grid["cols"].toInt(), 3);
    EXPECT_EQ(grid["area"].toArray(), (QJsonArray{10, 20, 100, 50}));
    // Зона пишется отдельно, внутрь существующего grid.
    const auto area = ze::patchValue(*patched, "Home", {"grid", "area"}, QJsonArray{0, 0, 50, 50});
    ASSERT_TRUE(area.has_value());
    const auto changed = QJsonDocument::fromJson(*area).object()["screens"].toArray()[0].toObject()["grid"].toObject();
    EXPECT_EQ(changed["area"].toArray(), (QJsonArray{0, 0, 50, 50}));
    EXPECT_EQ(changed["rows"].toInt(), 2);
}

TEST(ZoneEditing, PatchValueReplacesAddsAndRemovesKeepingTheRest) {
    // Существующее значение заменяется на месте, остальной текст байт в байт тот же.
    const auto tint = ze::patchValue(kRemote, {}, {"display", "tint"}, "#123456");
    ASSERT_TRUE(tint.has_value());
    QByteArray expected = kRemote;
    expected.replace(R"("tint": "#ffffff")", R"("tint": "#123456")");
    EXPECT_EQ(*tint, expected);

    // Отсутствующий ключ дописывается в многострочный объект с отступом соседей.
    const auto name = ze::patchValue(kRemote, {}, {"name"}, "Custom");
    ASSERT_TRUE(name.has_value());
    EXPECT_TRUE(name->contains("\n    \"name\": \"Custom\"\n}"));
    EXPECT_EQ(QJsonDocument::fromJson(*name).object()["name"].toString(), "Custom");

    // Недостающие объекты на пути создаются.
    const auto tiles = ze::patchValue(kRemote, {}, {"tiles", "border"}, "#000000");
    ASSERT_TRUE(tiles.has_value());
    EXPECT_EQ(QJsonDocument::fromJson(*tiles).object()["tiles"].toObject()["border"].toString(), "#000000");

    // Удаление последнего ключа убирает и опустевший объект; отсутствующий ключ — без изменений.
    const auto removed = ze::patchValue(*tiles, {}, {"tiles", "border"}, QJsonValue(QJsonValue::Undefined));
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(*removed, kRemote);
    const auto nothing = ze::patchValue(kRemote, {}, {"panel", "color"}, QJsonValue(QJsonValue::Undefined));
    ASSERT_TRUE(nothing.has_value());
    EXPECT_EQ(*nothing, kRemote);
}

TEST(ZoneEditing, PatchValueInsideOneScreen) {
    const auto patched = ze::patchValue(kRemote, "Menu", {"panel", "color"}, "#202020");
    ASSERT_TRUE(patched.has_value());
    const auto screens = QJsonDocument::fromJson(*patched).object()["screens"].toArray();
    EXPECT_FALSE(screens[0].toObject().contains("panel"));
    EXPECT_EQ(screens[1].toObject()["panel"].toObject()["color"].toString(), "#202020");
    // Удаление ключа в середине объекта убирает его вместе с запятой.
    const auto noRef = ze::patchValue(kRemote, "Home", {"ref"}, QJsonValue(QJsonValue::Undefined));
    ASSERT_TRUE(noRef.has_value());
    const auto home = QJsonDocument::fromJson(*noRef).object()["screens"].toArray()[0].toObject();
    EXPECT_FALSE(home.contains("ref"));
    EXPECT_EQ(home["cells"].toArray().size(), 2);
    EXPECT_FALSE(ze::patchValue(kRemote, "Nope", {"ref"}, 1).has_value());
    // На пути стоит не объект — ошибка, а не порча файла.
    EXPECT_FALSE(ze::patchValue(kRemote, {}, {"screens", "x"}, 1).has_value());
}

TEST(ZoneEditing, PatchBackgroundZones) {
    // Шапка создаётся вложенными объектами, цвет дописывается рядом с высотой.
    const auto height = ze::patchValue(kRemote, "Home", {"background", "header", "height"}, 30);
    ASSERT_TRUE(height.has_value());
    const auto color = ze::patchValue(*height, "Home", {"background", "header", "color"}, "#112233");
    ASSERT_TRUE(color.has_value());
    const auto header = QJsonDocument::fromJson(*color).object()["screens"].toArray()[0].toObject()
                            ["background"].toObject()["header"].toObject();
    EXPECT_EQ(header["height"].toInt(), 30);
    EXPECT_EQ(header["color"].toString(), "#112233");
    // «Прозрачная»: цвет убирается, высота остаётся.
    const auto clear = ze::patchValue(*color, "Home", {"background", "header", "color"}, QJsonValue(QJsonValue::Undefined));
    ASSERT_TRUE(clear.has_value());
    const auto kept = QJsonDocument::fromJson(*clear).object()["screens"].toArray()[0].toObject()
                          ["background"].toObject()["header"].toObject();
    EXPECT_FALSE(kept.contains("color"));
    EXPECT_EQ(kept["height"].toInt(), 30);
    // Шапка убирается целиком; у экрана не остаётся пустого background.
    const auto gone = ze::patchValue(*clear, "Home", {"background", "header"}, QJsonValue(QJsonValue::Undefined));
    ASSERT_TRUE(gone.has_value());
    EXPECT_FALSE(QJsonDocument::fromJson(*gone).object()["screens"].toArray()[0].toObject().contains("background"));
}

TEST(ZoneEditing, AppendAndRemoveScreen) {
    const QJsonObject screen{{"id", "Extra"}, {"ref", QJsonArray{240, 320}}, {"role", "numpad"},
                             {"cells", QJsonArray{QJsonObject{{"id", "S"}, {"rect", QJsonArray{1, 2, 3, 4}}, {"kind", "ScreenSlot"}}}}};
    const auto added = ze::appendScreen(kRemote, screen);
    ASSERT_TRUE(added.has_value());
    const auto screens = QJsonDocument::fromJson(*added).object()["screens"].toArray();
    ASSERT_EQ(screens.size(), 3);
    EXPECT_EQ(screens[2].toObject(), screen);
    // Прежние экраны текстом не тронуты, ячейки нового — по одной в строке.
    EXPECT_TRUE(added->startsWith(kRemote.left(kRemote.lastIndexOf("}\n    ]"))));
    EXPECT_TRUE(added->contains(R"({"id": "S", "rect": [1, 2, 3, 4], "kind": "ScreenSlot"})"));

    const auto removed = ze::removeScreen(*added, "Extra");
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(*removed, kRemote);
    const auto first = ze::removeScreen(kRemote, "Home");
    ASSERT_TRUE(first.has_value());
    const auto left = QJsonDocument::fromJson(*first).object()["screens"].toArray();
    ASSERT_EQ(left.size(), 1);
    EXPECT_EQ(left[0].toObject()["id"].toString(), "Menu");
    EXPECT_FALSE(ze::removeScreen(kRemote, "Nope").has_value());
}

TEST(ZoneEditing, PatchDisplayChangesRect) {
    const auto patched = ze::patchDisplay(kRemote, QRectF(120, 60, 180, 140));
    ASSERT_TRUE(patched.has_value());
    const auto display = QJsonDocument::fromJson(*patched).object()["display"].toObject();
    const auto rect = display["rect"].toArray();
    ASSERT_EQ(rect.size(), 4);
    EXPECT_DOUBLE_EQ(rect[0].toDouble(), 120.0);
    EXPECT_DOUBLE_EQ(rect[1].toDouble(), 60.0);
    EXPECT_DOUBLE_EQ(rect[2].toDouble(), 180.0);
    EXPECT_DOUBLE_EQ(rect[3].toDouble(), 140.0);
    EXPECT_EQ(display["tint"].toString(), "#ffffff");
}

TEST(ZoneEditing, PatchDisplayCreatesMissingDisplay) {
    // Заготовка нового пульта (createStub) без display: рамка экрана создаётся.
    const QByteArray stub = "{\n  \"id\": \"x\",\n  \"hardware\": {\"zones\": []},\n  \"screens\": []\n}\n";
    const auto patched = ze::patchDisplay(stub, QRectF(1, 2, 3, 4));
    ASSERT_TRUE(patched.has_value());
    const auto rect = QJsonDocument::fromJson(*patched).object()["display"].toObject()["rect"].toArray();
    ASSERT_EQ(rect.size(), 4);
    EXPECT_DOUBLE_EQ(rect[3].toDouble(), 4.0);
    // Снять рамку там, где её не было, — файл не меняется.
    EXPECT_EQ(ze::patchDisplay(stub, std::nullopt), stub);
}

TEST(ZoneEditing, PatchDisplayToNull) {
    const auto patched = ze::patchDisplay(kRemote, std::nullopt);
    ASSERT_TRUE(patched.has_value());
    EXPECT_TRUE(QJsonDocument::fromJson(*patched).object()["display"].toObject()["rect"].isNull());
}

TEST(ZoneEditing, ZoneTextsOfBodyAndScreen) {
    const auto body = ze::zoneTexts(kRemote);
    ASSERT_TRUE(body.has_value());
    ASSERT_EQ(body->size(), 2);
    EXPECT_TRUE(body->at(0).contains("Old1"));
    const auto menu = ze::zoneTexts(kRemote, "Menu");
    ASSERT_TRUE(menu.has_value());
    ASSERT_EQ(menu->size(), 1);
    EXPECT_TRUE(menu->at(0).contains("\"C\""));
    EXPECT_FALSE(ze::zoneTexts(kRemote, "Nope").has_value());
}


TEST(ZoneEditing, SaveFilesDetectsConflictAndWritesWholeSet) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QHash<QString, QByteArray> originals{{"profile.json", "old profile"}, {"zones.json", "old zones"}};
    for (auto it = originals.cbegin(); it != originals.cend(); ++it) {
        QFile file(QDir(root.path()).filePath(it.key()));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(it.value()), it.value().size());
    }
    const QHash<QString, QByteArray> changed{{"profile.json", "new profile"}, {"zones.json", "new zones"}};
    auto incorrect = originals;
    incorrect["zones.json"] = "outdated";
    EXPECT_FALSE(ze::saveFiles(root.path(), incorrect, changed).isEmpty());
    for (auto it = originals.cbegin(); it != originals.cend(); ++it) {
        QFile file(QDir(root.path()).filePath(it.key()));
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        EXPECT_EQ(file.readAll(), it.value());
    }
    ASSERT_TRUE(ze::saveFiles(root.path(), originals, changed).isEmpty());
    for (auto it = changed.cbegin(); it != changed.cend(); ++it) {
        QFile file(QDir(root.path()).filePath(it.key()));
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        EXPECT_EQ(file.readAll(), it.value());
    }
    EXPECT_TRUE(QDir(root.path()).entryList({".zone-editor-backup-*"}, QDir::Dirs | QDir::Hidden).isEmpty());
}

TEST(ZoneEditing, SaveOnTopKeepsExternalRemoteChanges) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    // Пока редактор был открыт, profile.json поправили вручную.
    QByteArray current = kRemote;
    current.replace("\"id\": \"880\"", "\"id\": \"880\", \"name\": \"Living room\"");
    ASSERT_NE(current, kRemote);
    {
        QFile file(QDir(root.path()).filePath("profile.json"));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(current), current.size());
    }
    // «Сохранить мои правки поверх»: правки применяются к текущему файлу.
    auto patched = ze::patchDisplay(current, QRectF(10, 20, 30, 40));
    ASSERT_TRUE(patched.has_value());
    patched = ze::patchZones(*patched, QString(), {R"({"id": "B", "rect": [1, 1, 5, 5]})"});
    ASSERT_TRUE(patched.has_value());
    const QHash<QString, QByteArray> expected{{"profile.json", current}};
    const QHash<QString, QByteArray> updates{{"profile.json", *patched}};
    ASSERT_TRUE(ze::saveFiles(root.path(), expected, updates).isEmpty());
    QFile file(QDir(root.path()).filePath("profile.json"));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto saved = QJsonDocument::fromJson(file.readAll()).object();
    EXPECT_EQ(saved.value("name").toString(), "Living room"); // ручная правка сохранилась
    EXPECT_EQ(saved["display"].toObject()["rect"].toArray(), (QJsonArray{10, 20, 30, 40}));
    EXPECT_EQ(saved["hardware"].toObject()["zones"].toArray().size(), 1);
}

TEST(ZoneEditing, AutoZonesAssignIdsRadiusAndSkipOverlaps) {
    QJsonArray existing;
    QJsonObject off;
    off["id"] = "Off";
    off["rect"] = QJsonArray{20, 41, 30, 30};
    off["kind"] = "System";
    existing.append(off);

    QJsonArray boxes;
    boxes.append(QJsonArray{20, 41, 30, 30}); // совпадает с Off — отбросить
    boxes.append(QJsonArray{512, 51, 34, 44}); // «капсула»: radius = 0.30 * 34
    boxes.append(QJsonArray{593, 51, 35, 35}); // квадрат: круг (radius = -0.5)
    boxes.append(QJsonArray{512, 96, 34});     // не рамка — отбросить
    boxes.append(42);                          // не рамка — отбросить

    const QJsonArray zones = ze::autoZones(boxes, existing, "Auto");
    ASSERT_EQ(zones.size(), 2);
    EXPECT_EQ(zones[0].toObject().value("id"), "Auto");
    EXPECT_EQ(zones[1].toObject().value("id"), "Auto2");
    EXPECT_EQ(zones[0].toObject().value("kind"), "Key");
    EXPECT_EQ(zones[1].toObject().value("kind"), "Key");
    EXPECT_DOUBLE_EQ(zones[0].toObject().value("radius").toDouble(), 10.2);
    EXPECT_DOUBLE_EQ(zones[1].toObject().value("radius").toDouble(), -0.5);
    EXPECT_EQ(zones[0].toObject().value("rect"), (QJsonArray{512, 51, 34, 44}));
    EXPECT_EQ(zones[1].toObject().value("rect"), (QJsonArray{593, 51, 35, 35}));
}

TEST(ZoneEditing, AutoZonesKeepExistingIds) {
    QJsonArray existing;
    QJsonObject off;
    off["id"] = "Auto"; // занято: новые id начинаются с Auto2
    off["rect"] = QJsonArray{10, 10, 20, 20};
    existing.append(off);

    QJsonArray boxes;
    boxes.append(QJsonArray{100, 100, 30, 30});
    const QJsonArray zones = ze::autoZones(boxes, existing, "Auto");
    ASSERT_EQ(zones.size(), 1);
    EXPECT_EQ(zones[0].toObject().value("id"), "Auto2");
}

TEST(ZoneEditing, AutoZonesRespectsOnlyZonesPassedAsExisting) {
    // Распознавание опирается на переданный список зон (то, что на экране редактора),
    // а не на файл: удалили зону с экрана — её рамка распознаётся заново.
    QJsonArray boxes;
    boxes.append(QJsonArray{10, 10, 40, 40});
    boxes.append(QJsonArray{200, 200, 40, 40});

    QJsonArray both;
    QJsonObject first;
    first["id"] = "First";
    first["rect"] = QJsonArray{12, 12, 40, 40};
    both.append(first);
    QJsonObject second;
    second["id"] = "Second";
    second["rect"] = QJsonArray{202, 202, 40, 40};
    both.append(second);

    // Обе зоны на экране — обе рамки пропущены.
    EXPECT_TRUE(ze::autoZones(boxes, both, "Auto").isEmpty());

    // «Second» удалён с экрана — её рамка добавлена, «First» всё ещё блокирует свою.
    QJsonArray onlyFirst;
    onlyFirst.append(first);
    const QJsonArray added = ze::autoZones(boxes, onlyFirst, "Auto");
    ASSERT_EQ(added.size(), 1);
    EXPECT_EQ(added[0].toObject().value("rect"), (QJsonArray{200, 200, 40, 40}));

    // Экран пуст — распознаётся всё, как будто зон на пульте нет.
    const QJsonArray all = ze::autoZones(boxes, QJsonArray{}, "Auto");
    ASSERT_EQ(all.size(), 2);
    EXPECT_EQ(all[0].toObject().value("id"), "Auto");
    EXPECT_EQ(all[1].toObject().value("id"), "Auto2");
}

TEST(ZoneEditing, AutoZonesNeverOverlap) {
    QJsonArray existing;
    QJsonObject off;
    off["id"] = "Off";
    off["rect"] = QJsonArray{0, 0, 40, 40};
    existing.append(off);

    QJsonArray boxes;
    boxes.append(QJsonArray{35, 35, 20, 20});   // малый нахлёст с Off — всё равно пересечение
    boxes.append(QJsonArray{40, 0, 20, 20});    // касается Off краем — допустимо
    boxes.append(QJsonArray{50, 10, 20, 20});   // пересекает только что принятую рамку
    boxes.append(QJsonArray{110, 110, 20, 20}); // внутри экрана
    boxes.append(QJsonArray{200, 0, 20, 20});   // свободное место

    const QJsonArray zones = ze::autoZones(boxes, existing, "Auto", QRectF(100, 100, 50, 50));
    ASSERT_EQ(zones.size(), 2);
    EXPECT_EQ(zones[0].toObject().value("rect"), (QJsonArray{40, 0, 20, 20}));
    EXPECT_EQ(zones[1].toObject().value("rect"), (QJsonArray{200, 0, 20, 20}));
}

} // namespace
