/**
 * @file remoteCatalogTest.cpp
 * @brief Каталог моделей: поиск по skin, семейство прошивки, уникальность и связь с профилями.
 */

#include "harmony/core/remoteCatalog.h"
#include "harmony/core/remoteProfile.h"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <string_view>

using namespace harmony;

TEST(RemoteCatalog, FindsModelBySkin) {
    const auto* model = catalogModelBySkin(61);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->id, "900");

    ASSERT_NE(catalogModelBySkin(72), nullptr);
    EXPECT_EQ(catalogModelBySkin(72)->id, "650");

    for (const int skin : {62, 63}) {
        ASSERT_NE(catalogModelBySkin(skin), nullptr) << skin;
        EXPECT_EQ(catalogModelBySkin(skin)->id, "1100") << skin;
    }

    EXPECT_EQ(catalogModelBySkin(9999), nullptr);
}

// Строка, которой программа называет конфигурацию хаба (I5): файл хаба не несёт аппаратного id,
// поэтому имя берётся из каталога, а не из ветки кода. Строка одна — иначе выбор был бы скрыт.
TEST(RemoteCatalog, HubModelIsOneCatalogRow) {
    const auto* hub = hubModel();
    ASSERT_NE(hub, nullptr);
    EXPECT_EQ(hub->link, LinkKind::Hub);
    EXPECT_TRUE(hub->hubDefault);
    std::size_t marked = 0;
    for (const auto& model : remoteCatalog()) {
        if (model.hubDefault) {
            ++marked;
            EXPECT_EQ(&model, hub);
        }
    }
    EXPECT_EQ(marked, 1U) << "строка хаба должна быть одна";
}

// Красное на этапе А: правило «модель с суффиксом i — нет» (владелец, 24.11) доходит до
// настоящего пульта только через skin, потому что пульт отвечает skin, а не именем. Skin 64
// справочник skins Logitech называет «Harmony 1100i» (reference/remotes/README.md строка 144),
// поэтому он должен отвечать строке `1100i`. До этого он отвечал строке `1100`, а строка `1100i`
// стояла без skins, то есть правило про i не могло сработать ни для одного устройства.
TEST(RemoteCatalog, Skin64AnswersTheIModel) {
    const auto* model = catalogModelBySkin(64);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->id, "1100i");
    EXPECT_FALSE(model->supportsExtender) << "skin 64 — правило про i, а не ответ источника";

    // Следствие переноса, названное: ответы для skin 64 — значения строки `1100i`, а она говорит,
    // что модель этого ряда никогда не читалась (`support/14-ЧТО-НЕ-ХВАТАЕТ.md` строка 46).
    EXPECT_FALSE(model->readable) << "read — значение строки";
}

// Красное на этапе А: каталог — список моделей пультов. Skin 24 пультом не является:
// справочник skins Logitech называет skins 20 и 24 «RF Wireless Extender», то есть
// приёмник, а не пульт (`reference/remotes/README.md`, строки 115 и 117), и
// `reference/architectures/harmony-890-895/misc.md` прямо говорит, что это не пульты.
// Строки каталога для skins 20 и 24 убираются, поэтому модель по ним не отвечает.
TEST(RemoteCatalog, ExtenderSkinIsNotARemoteModel) {
    for (const int skin : {20, 24}) {
        EXPECT_EQ(catalogModelBySkin(skin), nullptr) << skin;
    }
}

// Красное на этапе А: ответ «может ли модель иметь модуль Z-Wave» — поле строки каталога
// (правило 8), а не имя модели в коде. Строки решения владельца 24.11: 890, 895, 890 Pro,
// 1000, 1100. Отрицательное для 1000i — из источника: клиент Logitech относит skin 52
// (Harmony 1000i, Australia) к `Cognac.NoZwave` (`reference/architectures/README.md`
// строка 100, `tests/logitech/test_host_client.py:1300`).
// Разные ответы для `1100` и `1100i` — это проверка, что ответ берётся из строки каталога,
// а не из суффикса имени: `normalizedModelId` приводит их к одному значению.
TEST(RemoteCatalog, ExtenderSupportIsARowField) {
    for (const char* id : {"890", "895", "890-pro", "1000", "1100"}) {
        const auto* model = catalogModel(id);
        ASSERT_NE(model, nullptr) << id;
        EXPECT_TRUE(model->supportsExtender) << id;
    }
    EXPECT_FALSE(catalogModel("1000i")->supportsExtender) << "skin 52 — Cognac.NoZwave";
    EXPECT_FALSE(catalogModel("1100i")->supportsExtender) << "модель с суффиксом i (24.11)";
    // Ответ даёт строка каталога, а не имя: `1100` и `1100i` — две строки, и ответы у них
    // разные. Если бы решение читалось из суффикса имени, тест не мог бы их различить.
    EXPECT_NE(catalogModel("1100i"), catalogModel("1100"));
}

TEST(RemoteCatalog, ReadableModelsHaveArchitecture) {
    for (const auto& model : remoteCatalog()) {
        if (!model.readable) {
            continue;
        }
        EXPECT_NE(model.architecture, 0) << model.id;
    }
}

TEST(RemoteCatalog, SkinsAndIdsAreUnique) {
    std::set<std::string_view> ids;
    std::set<int> skins;
    for (const auto& model : remoteCatalog()) {
        EXPECT_TRUE(ids.insert(model.id).second) << model.id;
        for (const int skin : model.skins) {
            EXPECT_TRUE(skins.insert(skin).second) << model.id << ": " << skin;
        }
    }
}

TEST(RemoteCatalog, NormalizesIdCase) {
    EXPECT_EQ(normalizedModelId("1100I"), "1100i");
    EXPECT_EQ(normalizedModelId("900"), "900");
    EXPECT_EQ(normalizedModelId("Harmony-ONE"), "harmony-one");
}

TEST(RemoteCatalog, BuiltinProfilesHaveCatalogModels) {
    for (const auto& profile : bundledRemoteProfiles) {
        EXPECT_NE(catalogModel(profile.id), nullptr) << profile.id;
    }
}

// U1: модели по умолчанию нет — функции `defaultRemoteId()` и `defaultRemoteProfile()` удалены
// в U2, поэтому тест проверял то, чего больше нет. Решения U2 проверяются в
// `tests/noDefaultModelTest.cpp`; порядок списка моделей проверяет `modelIds()` —
// RemoteResourcesTest.ModelListIsOrderedById.

TEST(RemoteCatalog, MissingArchitectureMeansUnknown) {
    const RemoteModel unknown{};
    EXPECT_EQ(unknown.architecture, 0);
    EXPECT_EQ(unknown.link, LinkKind::None);
    EXPECT_FALSE(unknown.readable);
    EXPECT_FALSE(unknown.rawRestore);
    EXPECT_FALSE(unknown.configWriteTested);
}

TEST(RemoteCatalog, DisplayNameComesFromCatalogThenProfile) {
    EXPECT_EQ(modelDisplayName("525"), "Harmony 525");
    EXPECT_EQ(modelDisplayName("900"), "Harmony 900");
    EXPECT_EQ(modelDisplayName(""), "");
    EXPECT_EQ(modelDisplayName("no-such"), "no-such");

    const auto saved = remoteProfiles;
    remoteProfiles.clear();
    EXPECT_EQ(modelDisplayName("900"), "Harmony 900");
    remoteProfiles.push_back({"900", "Profile name must not replace catalog name", "", ""});
    EXPECT_EQ(modelDisplayName("900"), "Harmony 900");
    remoteProfiles.push_back({"my-remote", "My remote", "", ""});
    EXPECT_EQ(modelDisplayName("my-remote"), "My remote");
    remoteProfiles = saved;
}

// Решено владельцем 22.11: способ подключения берётся из remote.json модели, если поле задано;
// если поля нет — из каталога. Право записи при этом решает каталог (OD9, W2).
TEST(RemoteCatalog, ConnectionFollowsFileWhenTheFieldIsSet) {
    // Без поля в файле — каталог.
    EXPECT_EQ(modelLink("900"), LinkKind::UsbNet);
    EXPECT_EQ(modelLink("1000"), LinkKind::UsbNet);
    EXPECT_EQ(modelLink("xbox360"), LinkKind::UsbNet);
    EXPECT_EQ(modelLink("650"), LinkKind::UsbHid);
    EXPECT_EQ(modelLink("525"), LinkKind::UsbHid);
    EXPECT_EQ(modelLink("touch"), LinkKind::Mh);
    EXPECT_EQ(modelLink("home-hub"), LinkKind::Hub);
    EXPECT_EQ(modelLink("no-such"), LinkKind::None);

    EXPECT_TRUE(usesUsbNet("1000")) << "каталог знает 1000 без профиля";
    EXPECT_FALSE(usesUsbNet("650"));
    EXPECT_FALSE(usesUsbNet("touch"));
    EXPECT_FALSE(usesUsbNet("no-such"));

    const auto saved = remoteProfiles;
    // Значение профиля того же id: remoteProfile() берёт первую запись, поэтому менять нужно её.
    const auto setDriver = [](const std::string& id, const std::string& value) {
        for (auto& profile : remoteProfiles)
            if (profile.id == id) profile.driver = value;
    };

    // Своя модель вне каталога: каталог её не знает, работает значение из remote.json.
    remoteProfiles.push_back({"my-remote", "My remote", "", "usb-net"});
    EXPECT_EQ(modelLink("my-remote"), LinkKind::UsbNet);
    EXPECT_TRUE(usesUsbNet("my-remote"));

    // Для модели каталога заданное значение важнее каталога.
    setDriver("650", "usb-net");
    EXPECT_EQ(modelLink("650"), LinkKind::UsbNet);
    EXPECT_TRUE(usesUsbNet("650"));
    // Неизвестное имя значения не даёт: остаётся каталог.
    setDriver("650", "unknown-driver");
    EXPECT_EQ(modelLink("650"), LinkKind::UsbHid);
    // `none` в файле — тоже решение пользователя: связи нет.
    setDriver("650", "none");
    EXPECT_EQ(modelLink("650"), LinkKind::None);

    // Значение «hub» из файла для модели, которой в каталоге нет.
    remoteProfiles.push_back({"my-hub", "My hub", "", "hub"});
    EXPECT_EQ(modelLink("my-hub"), LinkKind::Hub);
    remoteProfiles = saved;
}
