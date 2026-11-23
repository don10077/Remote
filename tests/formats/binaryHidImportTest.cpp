#include "formatFixtures.h"
#include "dumpFiles.h"

#include "harmony/core/configDocument.h"
#include "harmony/core/formats/binaryHid.h"
#include "harmony/core/keyScans.h"
#include "harmony/core/model/remoteConfig.h"
#include "harmony/core/remoteCatalog.h"
#include "harmony/core/remoteFamily.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <utility>

namespace {

/// Имена аппаратных кнопок импорта (у экранных кнопки имени нет).
std::set<std::string> hardNames(const harmony::model::RemoteConfig& config) {
    std::set<std::string> names;
    for (const auto& device : config.devices)
        for (const auto& group : device.controlGroups)
            for (const auto& button : group.buttons)
                if (!button.name.empty())
                    names.insert(button.name);
    return names;
}

/**
 * @brief Образ с одной командой и назначениями на заданные скан-коды.
 * @param architecture Семейство рамки контейнера.
 * @param skin Модель в заголовке EZHex; 999 — неизвестная.
 * @param scans Коды физических клавиш, каждая вызывает команду 0 устройства 0.
 * @return EZHex с независимыми таблицами устройств и режима, без экранных подписей.
 */
harmony::formats::Bytes keyFixture(int architecture, int skin, std::initializer_list<unsigned> scans) {
    auto file = *harmony::formats::parseEzHex(harmony::test::minimalEzHex(architecture, skin));
    auto& data = file.payload;
    data.resize(300, 0);
    const auto put = [&](std::size_t at, std::uint32_t value, unsigned width) {
        for (unsigned i = 0; i < width; ++i)
            data[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    // Сигнал и устройство минимальной фикстуры остаются; рамка выбирается по семейству.
    const auto& families = harmony::formats::containerFamilies();
    const auto family = std::ranges::find(families, architecture,
                                         &harmony::formats::ContainerFamilyInfo::architecture);
    std::fill(data.begin(), data.begin() + 128, 0);
    std::ranges::copy(family->cookie, data.begin());
    data[9] = static_cast<std::uint8_t>(family->slots);
    std::ranges::copy(family->marker, data.begin() + family->markerOffset());
    put(4, 0x30000 + 296, 4);
    put(12 + 4 * 5, 0x30000 + 128, 3); // устройства: секция 5 во всех проверяемых семействах
    put(12 + 4 * 6, 0x30000 + 200, 3); // режимы: секция 6
    std::fill(data.begin() + 184, data.end(), 0);
    put(200, 1, 3);
    put(203, 0x30000 + 208, 3);
    put(209, 0x30000 + 220, 3); // trailer + 1 → таблица клавиш; страниц нет
    data[220] = static_cast<std::uint8_t>(scans.size());
    std::size_t at = 221;
    for (unsigned scan : scans) {
        data[at] = static_cast<std::uint8_t>(0x80 | scan);
        data[at + 3] = 0x7d; // команда 0 устройства 0
        at += 4;
    }
    std::ranges::copy(family->endMarker, data.begin() + 296);
    put(294, harmony::formats::containerChecksum(data, 294), 2);
    return *harmony::formats::writeEzHex(file);
}

/// Проверяет имена кнопок и имя команды после полного пути EZHex → документ.
void expectKeys(int architecture, int skin, std::initializer_list<unsigned> scans,
                std::initializer_list<std::string_view> expected, std::string_view command) {
    auto doc = harmony::ConfigDocument::fromEzHex(keyFixture(architecture, skin, scans), "keys");
    ASSERT_TRUE(doc) << doc.error().message;
    ASSERT_EQ(doc->model().devices.size(), 1u);
    const auto& device = doc->model().devices.front();
    ASSERT_EQ(device.controlGroups.size(), 1u);
    std::vector<std::string> names;
    for (const auto& button : device.controlGroups.front().buttons)
        names.push_back(button.name);
    EXPECT_EQ(names, (std::vector<std::string>(expected.begin(), expected.end())));
    EXPECT_EQ(device.commands.front().name, command);
}

struct TextPoint {
    unsigned x, y;
};

/**
 * @brief Образ с одной экранной кнопкой и подписями на странице.
 * @param texts Подписи рисуются знакомым кодом `?`; если словарь глифов не подтверждён,
 *        именно этот код должен стать строкой подписи.
 * @return Фикстура без настоящего алфавита: проверяется группировка, а не распознавание букв.
 */
harmony::formats::Bytes softLabelFixture(int architecture, int skin, unsigned scan,
                                         std::initializer_list<TextPoint> texts) {
    auto file = *harmony::formats::parseEzHex(harmony::test::minimalEzHex(architecture, skin));
    auto& data = file.payload;
    data.resize(300, 0);
    const auto put = [&](std::size_t at, std::uint32_t value, unsigned width) {
        for (unsigned i = 0; i < width; ++i)
            data[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    const auto& families = harmony::formats::containerFamilies();
    const auto family = std::ranges::find(families, architecture,
                                          &harmony::formats::ContainerFamilyInfo::architecture);
    if (family == families.end())
        throw std::runtime_error("the fixture family is missing from the container table");
    std::fill(data.begin(), data.begin() + 128, 0);
    std::ranges::copy(family->cookie, data.begin());
    data[9] = static_cast<std::uint8_t>(family->slots);
    std::ranges::copy(family->marker, data.begin() + family->markerOffset());
    put(4, 0x30000 + 296, 4);
    put(12 + 4 * 5, 0x30000 + 128, 3); // устройства: секция 5
    put(12 + 4 * 6, 0x30000 + 200, 3); // режимы: секция 6
    std::fill(data.begin() + 184, data.end(), 0);
    put(200, 1, 3);
    put(203, 0x30000 + 208, 3);
    put(209, 0x30000 + 220, 3); // trailer + 1 -> table of one key
    data[220] = 1;
    data[221] = static_cast<std::uint8_t>(0x80 | scan);
    data[224] = 0x7d; // action: command 0 of device 0
    // Страница: тот же ключ (для `Misc`) и программа рисования с подписями.
    const auto* desc = harmony::remoteFamily(architecture);
    if (!desc)
        throw std::runtime_error("the fixture family has no description");
    const unsigned shift = desc->commandIndexShift;
    const std::size_t pageStart = 250;
    put(212, 1, 2);
    put(214, 0x30000 + pageStart, 3);
    put(pageStart + shift, 0x30000 + 220, 3);
    put(pageStart + shift + 3, 0x30000 + 240, 3);
    std::size_t at = 240;
    for (const auto point : texts) {
        data[at++] = 5; // x, y, строка
        data[at++] = static_cast<std::uint8_t>(point.x);
        data[at++] = static_cast<std::uint8_t>(point.y);
        data[at++] = '?';
        data[at++] = 0;
    }
    data[at] = 0; // конец программы рисования
    std::ranges::copy(family->endMarker, data.begin() + 296);
    put(294, harmony::formats::containerChecksum(data, 294), 2);
    return *harmony::formats::writeEzHex(file);
}

/// Подпись экранной кнопки после импорта; `pageLabel` — ожидаемый результат S3.
std::string importedPageLabel(int architecture, int skin, unsigned scan,
                              std::initializer_list<TextPoint> texts) {
    auto document = harmony::ConfigDocument::fromEzHex(softLabelFixture(architecture, skin, scan, texts),
                                                        "labels");
    if (!document)
        return "import error: " + document.error().message;
    const auto& groups = document->model().devices.front().controlGroups;
    const auto page = std::ranges::find(groups, "Misc", &harmony::model::ControlGroup::name);
    if (page == groups.end())
        return "no screen-button group";
    if (page->buttons.size() != 1U)
        return "unexpected button count " + std::to_string(page->buttons.size());
    return page->buttons.front().label;
}

} // namespace

TEST(BinaryHidImport, ScreenSideKeysUseFamilyNames) {
    expectKeys(14, 75, {0x08, 0x09, 0x02, 0x22},
               {"SoftLeft1", "SoftLeft2", "SoftRight1", "SoftRight2"}, "SoftLeft1");
}

TEST(BinaryHidImport, OnePlayUsesItsOwnScanCode) {
    expectKeys(12, 54, {0x1e}, {"Play"}, "Play");
}

TEST(BinaryHidImport, ModelTableOverridesPartialFamilyTable) {
    const auto* model = harmony::catalogModel("885");
    ASSERT_NE(model, nullptr);
    ASSERT_FALSE(model->skins.empty());
    expectKeys(8, model->skins.front(), {0x2c, 0x37, 0x38},
               {"SoftRight4", "Pause", "Play"}, "SoftRight4");
}

TEST(BinaryHidImport, UnknownModelStillUsesFamilyTable) {
    expectKeys(8, 999, {0x2c, 0x37}, {"Scan2C", "Pause"}, "Pause");
}

TEST(BinaryHidImport, MissingFamilyTableWarnsThroughImportReportNotProperties) {
    expectKeys(9, 22, {0x2c}, {"Scan2C"}, "Command1");
    const auto warning = "Клавиша Scan2C (скан-код 44, архитектура 9) без имени в таблице семейства";
    const auto imported = harmony::formats::importBinaryHid(
        harmony::formats::parseEzHex(keyFixture(9, 22, {0x2c}))->payload);
    ASSERT_TRUE(imported) << imported.error().message;
    EXPECT_EQ(imported->report.warnings, (std::vector<std::string>{warning}))
        << "предупреждение импорта — отчёт, а не свойство конфигурации";
    EXPECT_TRUE(std::ranges::find(imported->model.properties,
                  std::pair<std::string, std::string>{"ImportWarning", warning}) ==
              imported->model.properties.end());

    auto doc = harmony::ConfigDocument::fromEzHex(keyFixture(9, 22, {0x2c}), "keys");
    ASSERT_TRUE(doc);
    EXPECT_EQ(doc->importReport().warnings, (std::vector<std::string>{warning}))
        << "документ показывает отчёт импорта после открытия";
    EXPECT_TRUE(std::ranges::find(doc->model().properties,
                  std::pair<std::string, std::string>{"ImportWarning", warning}) ==
              doc->model().properties.end());
}

TEST(BinaryHidImport, RawPayloadWithoutModelUsesFamilyOnly) {
    const auto file = harmony::formats::parseEzHex(keyFixture(8, 999, {0x2c}));
    ASSERT_TRUE(file);
    const auto imported = harmony::formats::importBinaryHid(file->payload);
    ASSERT_TRUE(imported) << imported.error().message;
    EXPECT_EQ(imported->model.devices.front().controlGroups.front().buttons.front().name, "Scan2C");
}

// S3: arch 14 остаётся прежним; без геометрии семейства подписи по четвертям не группируются.
TEST(BinaryHidImport, Arch14SoftLabelsUseTheirFamilyGeometry) {
    EXPECT_EQ(importedPageLabel(14, 75, 0x2c, {{100, 80}}), "?")
        << "поведение 600-серии не меняется";
}

TEST(BinaryHidImport, FamilyWithoutScreenGeometryDoesNotGroupSoftLabels) {
    EXPECT_EQ(importedPageLabel(12, 54, 0x2c, {{100, 80}}), "Command1")
        << "геометрия arch 12 — нули: функция подписей возвращает пустой список";
}

// Регрессия K3 на настоящих дампах (перенесена из `tests/keyNamesTest.cpp` в K6, потому что
// старый список имён удалён; имена проверяются таблицами семейств и словарём).
TEST(BinaryHidImport, ImportedNamesComeFromTheirFamilyTables) {
    // Настоящие дампы (dumps/, не в git): без них тест пропускается (правило 19).
    struct Sample { const char* folder; const char* mask; const char* catalogId; };
    std::size_t checked = 0;
    for (const Sample sample : {Sample{"EZHex/harmony650", "*.EZHex", "650"},
                                Sample{"EZHex/harmony525", "*.EZHex", "525"},
                                Sample{"EZHex/arch8", "H885-LivingRoom*.EZHex", "885"},
                                Sample{"EZHex/arch8", "H880-*.EZHex", "880"}}) {
        const auto path = harmony::test::dumpFile(sample.folder, sample.mask);
        if (path.empty()) continue;
        ++checked;
        SCOPED_TRACE(path.filename().string());
        auto document = harmony::ConfigDocument::open(path);
        ASSERT_TRUE(document) << document.error().message;

        // Имена клавиш даёт таблица семейства/модели. Профиль не участвует.
        const auto* model = harmony::catalogModel(sample.catalogId);
        ASSERT_TRUE(model) << sample.catalogId;
        EXPECT_EQ(document->targetRemote(), sample.catalogId);
        const auto names = hardNames(document->model());
        ASSERT_FALSE(names.empty());

        // Число и состав назначений файла владельца не фиксируем (правило 19).
        const auto* table = harmony::keyScanTable(model->architecture, model->id);
        for (const auto& name : names) {
            if (name.starts_with("Scan")) {
                const unsigned code = static_cast<unsigned>(std::stoul(name.substr(4), nullptr, 16));
                EXPECT_FALSE(harmony::keyNameForScan(model->architecture, model->id, code)) << name;
                EXPECT_EQ(name, harmony::scanPlaceholder(code));
            } else {
                ASSERT_NE(table, nullptr) << name;
                EXPECT_NE(std::ranges::find(table->entries, name, &harmony::KeyScan::name),
                          table->entries.end()) << name;
            }
        }
    }
    if (checked == 0) GTEST_SKIP() << "dumps/ нет — дампы бинарных семейств только на машине владельца";
}
