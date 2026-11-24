/**
 * @file containerTest.cpp
 * @brief Тесты общей рамки контейнера (arch 8, 9, 10, 12, 14, 16) на синтетике по описанию формата.
 *
 * Настоящие дампы в git не кладём (support/remotes-plan.md); здесь контейнеры собираются
 * на месте по reference/config-format/container.md проекта harmony-explorations. Тесты
 * проверяют, что разбор и проверки следуют описанию, а не что описание совпадает с пультом.
 */

#include "dumpFiles.h"

#include "harmony/core/formats/binaryHid.h"
#include "harmony/core/formats/container.h"
#include "harmony/core/formats/ezhex.h"
#include "harmony/core/formats/remoteCapture.h"
#include "harmony/core/remoteCatalog.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

using namespace harmony::formats;

namespace {

using Blob = std::vector<std::uint8_t>;

constexpr std::uint32_t kBase = 0x030000; ///< Любой правдоподобный адрес флеш
constexpr std::size_t kSectionStep = 0x20;

void put(Blob& blob, std::size_t at, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        blob[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

void putStr(Blob& blob, std::size_t at, std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        blob[at + i] = static_cast<std::uint8_t>(text[i]);
    }
}

/// Контейнер семейства @p family: слоты 0..present-1 указывают на тела секций по порядку,
/// последние два слота пусты (в обоих базовых слотах 18/19 NULL у всех семейств).
/// @p mutate правит байты до запечатывания суммой и концевым маркером.
Blob makeContainer(const ContainerFamilyInfo& family, const std::function<void(Blob&)>& mutate = {}) {
    const std::size_t marker = family.markerOffset();
    const std::size_t present = family.slots - 2;
    const std::size_t bodyEnd = marker + 4 + kSectionStep * present + 0x40;
    Blob blob(bodyEnd + 6, 0);
    putStr(blob, 0, family.cookie);
    blob[9] = static_cast<std::uint8_t>(family.slots); // N в байте 0x09
    for (std::size_t slot = 0; slot < present; ++slot) {
        put(blob, kContainerTableOffset + 4 * slot + 1,
            kBase + static_cast<std::uint32_t>(marker + 4 + kSectionStep * slot), 3);
    }
    putStr(blob, marker, family.marker);
    for (std::size_t at = marker + 4; at < bodyEnd; ++at) {
        blob[at] = static_cast<std::uint8_t>(at * 7 + 1); // содержимое без нулей и маркеров
    }
    if (mutate) {
        mutate(blob);
    }
    // Запечатывание: end_addr проставляется до суммы — сумма покрывает и его.
    const std::size_t end = blob.size() - 4;
    put(blob, 4, kBase + static_cast<std::uint32_t>(end), 4);
    put(blob, end - 2, containerChecksum(blob, end - 2), 2);
    putStr(blob, end, family.endMarker);
    return blob;
}

const ContainerFamilyInfo& familyOf(ContainerFamily name) {
    for (const auto& info : containerFamilies()) {
        if (info.family == name) {
            return info;
        }
    }
    ADD_FAILURE() << "the family table lacks the family " << static_cast<int>(name);
    return containerFamilies().front();
}

} // namespace

TEST(Container, EveryFamilyInTableParses) {
    for (const auto& info : containerFamilies()) {
        const auto blob = makeContainer(info);
        const auto parsed = parseContainer(blob);
        ASSERT_TRUE(parsed) << info.cookie << ": " << parsed.error().message;
        EXPECT_EQ(parsed->family, &info);
        EXPECT_EQ(parsed->base, kBase);
        EXPECT_EQ(parsed->sectionCount(), info.slots - 2);
    }
}

TEST(Container, CookieAndSlotCountNameTheArchitecture) {
    EXPECT_EQ(findFamily("TPTP", 21)->family, ContainerFamily::Arch8);
    EXPECT_EQ(findFamily("AHCM", 20)->family, ContainerFamily::Arch9);
    EXPECT_EQ(findFamily("TPTP", 23)->family, ContainerFamily::Arch10);
    EXPECT_EQ(findFamily("GSPM", 22)->family, ContainerFamily::Arch12);
    EXPECT_EQ(findFamily("GSPM", 20)->family, ContainerFamily::Arch14);
    EXPECT_EQ(findFamily("GSPM", 15)->family, ContainerFamily::Arch16);
    EXPECT_EQ(findFamily("BMBM", 20), nullptr); // arch 7: читатель его не принимает
    EXPECT_EQ(findFamily("GSPM", 21), nullptr);
}

TEST(Container, SectionsAndRangesOfArch14) {
    const auto& info = familyOf(ContainerFamily::Arch14);
    const auto blob = makeContainer(info);
    const auto parsed = parseContainer(blob);
    ASSERT_TRUE(parsed) << parsed.error().message;
    EXPECT_EQ(parsed->endOffset, blob.size() - 4);
    EXPECT_EQ(parsed->checksumOffset, blob.size() - 6);
    EXPECT_EQ(parsed->checksumStored, containerChecksum(blob, parsed->checksumOffset));

    const auto first = containerSectionRange(*parsed, 0);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->begin, info.markerOffset() + 4);
    EXPECT_EQ(first->end - first->begin, kSectionStep);

    const auto last = containerSectionRange(*parsed, info.slots - 3);
    ASSERT_TRUE(last);
    EXPECT_EQ(last->end, parsed->checksumOffset); // последняя секция — до суммы
    EXPECT_FALSE(containerSectionRange(*parsed, info.slots - 2));
    EXPECT_FALSE(containerSectionRange(*parsed, info.slots));
}

TEST(Container, RejectsForeignCookieAndSlotCount) {
    const auto& info = familyOf(ContainerFamily::Arch14);
    auto cookie = makeContainer(info);
    cookie[0] = 'X';
    EXPECT_FALSE(parseContainer(cookie));

    auto slots = makeContainer(info);
    slots[9] = 21; // GSPM с 21 слотом — такой пары в таблице нет
    EXPECT_FALSE(parseContainer(slots));

    EXPECT_FALSE(parseContainer(Blob(64, 0))); // один ноль — тоже отказ
}

TEST(Container, RejectsBrokenTable) {
    const auto& info = familyOf(ContainerFamily::Arch14);

    // Ненулевой заполнитель перед указателем.
    auto spare = makeContainer(info, [](Blob& b) { b[kContainerTableOffset + 4] = 1; });
    EXPECT_FALSE(parseContainer(spare));

    // Указатель за пределами образа.
    auto outside = makeContainer(info, [](Blob& b) { put(b, kContainerTableOffset + 1, 0xFFFFFF, 3); });
    EXPECT_FALSE(parseContainer(outside));

    // Указатели не растут: два слота смотрят в одно место.
    auto shared = makeContainer(info, [](Blob& b) {
        for (int i = 0; i < 3; ++i) {
            b[kContainerTableOffset + 5 + static_cast<std::size_t>(i)] = b[kContainerTableOffset + 1 + static_cast<std::size_t>(i)];
        }
    });
    EXPECT_FALSE(parseContainer(shared));

    // Последний слот обязан быть пуст: на его нулы опирается маркер за таблицей.
    auto lastSlot = makeContainer(info, [marker = info.markerOffset(), slots = info.slots](Blob& b) {
        put(b, kContainerTableOffset + 4 * (slots - 1) + 1,
            kBase + static_cast<std::uint32_t>(marker + 4 + kSectionStep * (slots - 2)), 3);
    });
    EXPECT_FALSE(parseContainer(lastSlot));
}

TEST(Container, RefusesChecksumMismatch) {
    const auto& info = familyOf(ContainerFamily::Arch14);
    auto blob = makeContainer(info);
    blob[info.markerOffset() + 8] ^= 0xFF; // одно изменённое тело секции — сумму ловим здесь
    const auto parsed = parseContainer(blob);
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, harmony::ErrorCode::Format);
}

// --- I6: вариант TPTP с 11 слотами (RF Wireless Extender C-RE4A) ----------------------

// Красное на этапе А: строки варианта в таблице нет — CLI и разбор отказывают обоим дампам.
// Ожидалось отдельное значение семейства; существующий зелёный тест
// `RemoteFamilyTest.EveryContainerFamilyHasRemoteFamily` показал, что ключ описания —
// архитектура, а число слотов — поле строки. Поэтому вариант остаётся arch 10 со своим N.
TEST(Container, TableKnowsTheExtenderVariant) {
    const auto* info = findFamily("TPTP", 11);
    ASSERT_NE(info, nullptr) << "варианта 11 слотов в таблице семейств нет";
    EXPECT_EQ(info->family, ContainerFamily::Arch10);
    EXPECT_EQ(info->architecture, 10);
    EXPECT_EQ(info->marker, "WLWL");
    EXPECT_EQ(info->endMarker, "DKDK");
    EXPECT_EQ(info->slots, 11u);
    EXPECT_FALSE(info->semanticsVerified) << "семантику секций 890/895 применять нельзя: на данных экстендера она не проверена";
}

// Красное на этапе А: структура варианта разбирается (проверки рамки, а не допущения).
TEST(Container, ExtenderVariantStructureParses) {
    const auto* info = findFamily("TPTP", 11);
    ASSERT_NE(info, nullptr);
    const auto blob = makeContainer(*info);
    const auto parsed = parseContainer(blob);
    ASSERT_TRUE(parsed) << parsed.error().message;
    EXPECT_EQ(parsed->family, info);
    EXPECT_EQ(parsed->base, kBase);
    EXPECT_EQ(parsed->sectionCount(), info->slots - 2);   // следует из конструкции фикстуры
}

// Красное на этапе А: сумма варианта проверяется, а не предполагается.
TEST(Container, RefusesBrokenChecksumOfTheExtenderVariant) {
    const auto* info = findFamily("TPTP", 11);
    ASSERT_NE(info, nullptr);
    auto blob = makeContainer(*info);
    blob[info->markerOffset() + 8] ^= 0xFF;
    const auto parsed = parseContainer(blob);
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, harmony::ErrorCode::Format);
    EXPECT_TRUE(parsed.error().message.contains("checksum")) << parsed.error().message;
}

// Красное на этапе А: импорт не применяет семантику непроверенного варианта — он называет
// вариант и семейство, чьё описание не используется (правило 18).
TEST(Container, SemanticsOfAnUnverifiedVariantAreNotApplied) {
    const auto* info = findFamily("TPTP", 11);
    ASSERT_NE(info, nullptr);
    const auto imported = harmony::formats::importBinaryHid(makeContainer(*info), "896");
    ASSERT_FALSE(imported) << "импорт не должен выдумывать устройства по описанию 890/895";
    EXPECT_EQ(imported.error().code, harmony::ErrorCode::Unsupported);
    EXPECT_TRUE(imported.error().message.contains("TPTP")) << imported.error().message;
    EXPECT_TRUE(imported.error().message.contains("11")) << imported.error().message;
    EXPECT_TRUE(imported.error().message.contains("10")) << "названо семейство, чья семантика не применена";
}

// Красное на этапе А: разрешение на заполнение после концевого маркера — поле таблицы,
// а не строка «TPTP» и число 23 в коде импорта.
TEST(Container, TrailingPaddingIsAPropertyOfTheFamily) {
    EXPECT_TRUE(findFamily("TPTP", 23)->trailingPaddingAllowed);
    EXPECT_FALSE(findFamily("TPTP", 11)->trailingPaddingAllowed);

    auto padded = makeContainer(*findFamily("TPTP", 23));
    padded.insert(padded.end(), 8, 0);
    const auto parsed = parseContainer(padded);
    ASSERT_TRUE(parsed) << parsed.error().message;
    EXPECT_EQ(parsed->endOffset, padded.size() - 4 - 8);

    auto other = makeContainer(familyOf(ContainerFamily::Arch14));
    other.insert(other.end(), 8, 0);
    EXPECT_FALSE(parseContainer(other));
}

// Факт: вариант на 23 слота остаётся отдельной строкой таблицы.
TEST(Container, TwentyThreeSlotVariantStaysDistinct) {
    const auto* arch10 = findFamily("TPTP", 23);
    const auto* extender = findFamily("TPTP", 11);
    ASSERT_NE(arch10, nullptr);
    ASSERT_NE(extender, nullptr);
    EXPECT_EQ(arch10->family, ContainerFamily::Arch10);
    EXPECT_EQ(arch10->slots, 23u);
    EXPECT_EQ(extender->architecture, arch10->architecture);
    EXPECT_NE(extender, arch10) << "11 и 23 слота — две строки одной архитектуры";
    EXPECT_TRUE(arch10->semanticsVerified);
}

// Факт (пропускается без локальных дампов): оба файла экстендера дают один и тот же образ.
// Модели пульта для skin 24 каталог не знает: экстендер — не пульт (I7), поэтому «896» остаётся
// записью снятого файла, а не решением каталога. При расхождении программа называет оба
// значения (правило 18): каталог — ничего, файл — «896».
TEST(Container, ExtenderDumpsGiveOneImageAndNoRemoteModel) {
    const auto ezhex = harmony::test::dumpFile("Json/C-RE4A", "c-re4a-20261124.EZHex");
    const auto json = harmony::test::dumpFile("Json/C-RE4A", "c-re4a-20261124.remote.json");
    if (ezhex.empty() || json.empty()) {
        GTEST_SKIP() << "Локальные дампы экстендера не установлены: " << ezhex << " / " << json;
    }

    const harmony::test::Bytes bytes = harmony::test::readFile(ezhex);
    const auto file = harmony::formats::parseEzHex(bytes);
    ASSERT_TRUE(file) << file.error().message;
    const auto capture = harmony::formats::parseRemoteCapture(harmony::test::readText(json));
    ASSERT_TRUE(capture) << capture.error().message;

    // Снятие хранит файл как есть (B3): тот же заголовок и те же байты данных.
    EXPECT_EQ(capture->data, bytes) << "снятый JSON содержит файл как есть";
    EXPECT_EQ(capture->identity.skin, file->identity.skin);
    EXPECT_EQ(capture->container, CaptureContainer::EzHex);

    const auto frame = parseContainer(file->payload);
    ASSERT_TRUE(frame) << frame.error().message;
    EXPECT_EQ(frame->family, findFamily("TPTP", 11));

    // Тот же образ, разобранный из снятого JSON, даёт то же решение таблицы семейств.
    const auto fromCapture = harmony::formats::parseEzHex(capture->data);
    ASSERT_TRUE(fromCapture) << fromCapture.error().message;
    const auto same = parseContainer(fromCapture->payload);
    ASSERT_TRUE(same) << same.error().message;
    EXPECT_EQ(same->family, frame->family);
    EXPECT_EQ(same->base, frame->base);

    const auto* model = harmony::catalogModelBySkin(file->identity.skin);
    EXPECT_EQ(model, nullptr) << "каталог моделей пультов не знает skin 24 (I7)";
    EXPECT_EQ(harmony::catalogModel(harmony::normalizedModelId(capture->modelId)), nullptr)
        << "имя в снятом файле — запись файла, а не модель каталога (правило 18)";
}
